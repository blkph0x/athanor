# Pack lab stub/real APK without Gradle (DEC-0030 / 0038).
# Prerequisites: make android-so + make android-java already run.
# Version: ATN_APK_VERSION_CODE / ATN_APK_VERSION_NAME, else auto-bump lab/apk-version.txt.
param(
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$BuildTools = "34.0.0",
    [string]$Platform = "android-31"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$BT = Join-Path $SdkRoot "build-tools\$BuildTools"
$Jar = Join-Path $SdkRoot "platforms\$Platform\android.jar"
$Aapt2 = Join-Path $BT "aapt2.exe"
$D8 = Join-Path $BT "d8.bat"
$Zipalign = Join-Path $BT "zipalign.exe"
$Apksigner = Join-Path $BT "apksigner.bat"

foreach ($p in @($Aapt2, $D8, $Zipalign, $Apksigner, $Jar)) {
    if (-not (Test-Path $p)) { throw "missing tool: $p" }
}
if (-not (Test-Path "android\libatn.so")) {
    throw "android\libatn.so missing - run: make android-so"
}
if (-not (Test-Path "android\out\com\athanor\daemon\AtnDaemonService.class")) {
    throw "android\out classes missing - run: make android-java"
}

# PackageManager requires versionCode to rise on each upgrade push (DEC-0048).
$VerFile = Join-Path $Root "lab\apk-version.txt"
$LabDir = Join-Path $Root "lab"
if (-not (Test-Path $LabDir)) { New-Item -ItemType Directory -Path $LabDir | Out-Null }
$VersionCode = 0
if ($env:ATN_APK_VERSION_CODE -and $env:ATN_APK_VERSION_CODE -match '^\d+$') {
    $VersionCode = [int]$env:ATN_APK_VERSION_CODE
} else {
    $prev = 0
    if (Test-Path $VerFile) {
        $raw = (Get-Content $VerFile -Raw).Trim()
        if ($raw -match '^\d+') { $prev = [int]$Matches[0] }
    }
    $VersionCode = $prev + 1
    if ($VersionCode -lt 1) { $VersionCode = 1 }
}
if ($env:ATN_APK_VERSION_NAME -and $env:ATN_APK_VERSION_NAME.Trim().Length -gt 0) {
    $VersionName = $env:ATN_APK_VERSION_NAME.Trim()
} else {
    $VersionName = "lab.$VersionCode"
}
[System.IO.File]::WriteAllText($VerFile, "$VersionCode`n")

$Work = Join-Path $Root "android\apk-work"
if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Path $Work | Out-Null
New-Item -ItemType Directory -Path "$Work\compiled" | Out-Null
New-Item -ItemType Directory -Path "$Work\dex" | Out-Null
New-Item -ItemType Directory -Path "$Work\lib\arm64-v8a" | Out-Null

Copy-Item "android\libatn.so" "$Work\lib\arm64-v8a\libatn.so"

$AssetsSrc = Join-Path $Root "android\assets"
$DenyBin = Join-Path $AssetsSrc "atn_pwd_deny.bin"
if (-not (Test-Path $DenyBin)) {
    python (Join-Path $Root "tools\gen_pwd_deny.py")
}
if (Test-Path $AssetsSrc) {
    New-Item -ItemType Directory -Path "$Work\assets" -Force | Out-Null
    Copy-Item -Path (Join-Path $AssetsSrc "*") -Destination "$Work\assets" -Recurse -Force
}

& $Aapt2 compile --dir "android\res" -o "$Work\compiled\res.zip"
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed" }

$Linked = Join-Path $Work "linked.apk"
& $Aapt2 link -o $Linked -I $Jar --manifest "android\AndroidManifest.xml" `
    --version-code $VersionCode --version-name $VersionName `
    "$Work\compiled\res.zip" --auto-add-overlay
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

# Collect class files for d8
$ClassList = Get-ChildItem -Path "android\out" -Recurse -Filter "*.class" |
    ForEach-Object { $_.FullName }
& $D8 --lib $Jar --min-api 26 --output "$Work\dex" @ClassList
if ($LASTEXITCODE -ne 0) { throw "d8 failed" }

# Rebuild APK: copy linked, add classes.dex + native lib
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Unsigned = Join-Path $Work "unsigned.apk"
Copy-Item $Linked $Unsigned -Force
$zip = [System.IO.Compression.ZipFile]::Open($Unsigned, 'Update')
try {
    $dexEntry = $zip.CreateEntry("classes.dex")
    $fs = [System.IO.File]::OpenRead("$Work\dex\classes.dex")
    try {
        $es = $dexEntry.Open()
        try { $fs.CopyTo($es) } finally { $es.Dispose() }
    } finally { $fs.Dispose() }

    $soEntry = $zip.CreateEntry("lib/arm64-v8a/libatn.so")
    $fs2 = [System.IO.File]::OpenRead("$Work\lib\arm64-v8a\libatn.so")
    try {
        $es2 = $soEntry.Open()
        try { $fs2.CopyTo($es2) } finally { $es2.Dispose() }
    } finally { $fs2.Dispose() }

    $assetsDir = Join-Path $Work "assets"
    if (Test-Path $assetsDir) {
        Get-ChildItem -Path $assetsDir -File -Recurse | ForEach-Object {
            $rel = $_.FullName.Substring($assetsDir.Length).TrimStart('\', '/')
            $entryName = ("assets/" + ($rel -replace '\\', '/'))
            $ae = $zip.CreateEntry($entryName)
            $afs = [System.IO.File]::OpenRead($_.FullName)
            try {
                $aes = $ae.Open()
                try { $afs.CopyTo($aes) } finally { $aes.Dispose() }
            } finally { $afs.Dispose() }
        }
    }
} finally {
    $zip.Dispose()
}

$Aligned = Join-Path $Work "aligned.apk"
if (Test-Path $Aligned) { Remove-Item $Aligned -Force }
& $Zipalign -f 4 $Unsigned $Aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

$Ks = Join-Path $Root "android\debug.keystore"
if (-not (Test-Path $Ks)) {
    keytool -genkeypair -v -keystore $Ks -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=Athanor Lab,O=Athanor,C=AU"
    if ($LASTEXITCODE -ne 0) { throw "keytool failed" }
}

$OutApk = Join-Path $Root "android\athanor-lab.apk"
if (Test-Path $OutApk) { Remove-Item $OutApk -Force }
& $Apksigner sign --ks $Ks --ks-pass pass:android --key-pass pass:android `
    --out $OutApk $Aligned
if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }

Write-Output "APK_OK $OutApk versionCode=$VersionCode versionName=$VersionName"
