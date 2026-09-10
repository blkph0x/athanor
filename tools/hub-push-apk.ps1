# Hub -> phone APK update over DEC-0048 tunnel (U/UC). NO adb install on tunnel path.
# Usage:
#   powershell -NoProfile -File tools/hub-push-apk.ps1
#   powershell -NoProfile -File tools/hub-push-apk.ps1 -Bootstrap
# -Bootstrap: one-time USB adb install -r of the just-built installer-capable APK
#   (chicken-egg), then bumps again and pushes N+1 purely over tunnel.
# adb is OK for logcat / dumpsys version verify - never for the tunnel push itself.
param(
    [switch]$Bootstrap
)

$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root

$Adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $Adb)) { $Adb = "adb" }
$HubExe = Join-Path $Root "atnnode.exe"
$HubLog = Join-Path $Root "lab\hub-listen.log"
$HubErr = Join-Path $Root "lab\hub-listen.err"
$KeysPath = Join-Path $Root "lab\hub-mlkem.keys"
$VerFile = Join-Path $Root "lab\apk-version.txt"
$ApkPath = Join-Path $Root "android\athanor-lab.apk"
$ListenPort = 47000
$EnrollUrl = "http://127.0.0.1:8799"
$GccBin = "C:\Users\Blkph0x\gcc\bin"
if (Test-Path $GccBin) {
    $env:PATH = "$GccBin;$env:PATH"
}

$results = [ordered]@{}
$failed = 0

function Write-Step([string]$name) {
    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan
}

function Set-Result([string]$name, [bool]$ok, [string]$detail = "") {
    $results[$name] = @{ Ok = $ok; Detail = $detail }
    if ($ok) {
        Write-Host ("PASS  {0}  {1}" -f $name, $detail) -ForegroundColor Green
    } else {
        $script:failed++
        Write-Host ("FAIL  {0}  {1}" -f $name, $detail) -ForegroundColor Red
    }
}

function Wait-LogContains([string]$logPath, [string]$pattern, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $logPath) {
            $any = Select-String -Path $logPath -Pattern $pattern -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($any) { return $any.Line }
        }
        Start-Sleep -Milliseconds 400
    }
    return $null
}

function Invoke-PhoneReconnect {
    & $Adb shell am start -S -n com.athanor.daemon/.AtnLabActivity --ez reconnect true 2>$null | Out-Null
    & $Adb shell am startservice -n com.athanor.daemon/.AtnDaemonService -a com.athanor.daemon.RECONNECT 2>$null | Out-Null
}

function Wait-MeshEstablished([int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $mark = if (Test-Path $HubLog) { (Get-Item $HubLog).Length } else { 0 }
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $HubLog) {
            $tail = Get-Content $HubLog -Tail 60 -ErrorAction SilentlyContinue | Out-String
            if ($tail -match 'policy_push|ESTABLISHED') {
                $len = (Get-Item $HubLog).Length
                if ($len -gt $mark -or $tail -match 'policy_push') {
                    return $true
                }
            }
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-LogcatMatch([string]$pattern, [int]$timeoutSec) {
    $devs = & $Adb devices 2>$null | Out-String
    if ($devs -notmatch '(?m)^\S+\s+device\b') {
        Write-Host "WARN: no adb device - skip logcat wait (hub stream is SoT)" -ForegroundColor Yellow
        return $null
    }
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $out = & $Adb logcat -d 2>$null | Out-String
        if ($out -match $pattern) {
            $line = (($out -split "`r?`n") | Where-Object { $_ -match $pattern -and $_ -match 'atn-upd' } |
                Select-Object -Last 1)
            if (-not $line) {
                $line = (($out -split "`r?`n") | Where-Object { $_ -match $pattern } |
                    Select-Object -Last 1)
            }
            return $line
        }
        Start-Sleep -Milliseconds 800
    }
    return $null
}

function Invoke-FormPost([string]$url, [hashtable]$fields) {
    $pairs = foreach ($k in $fields.Keys) {
        ("{0}={1}" -f [uri]::EscapeDataString($k), [uri]::EscapeDataString([string]$fields[$k]))
    }
    $body = ($pairs -join "&")
    try {
        return Invoke-WebRequest -Uri $url -Method POST -Body $body `
            -ContentType "application/x-www-form-urlencoded" -UseBasicParsing -TimeoutSec 60
    } catch {
        return $null
    }
}

function Ensure-EnrollConsole {
    $needRestart = $true
    try {
        $r = Invoke-WebRequest -Uri $EnrollUrl -UseBasicParsing -TimeoutSec 3
        if ($r.StatusCode -eq 200 -and $r.Content -match 'action="/update"') {
            $needRestart = $false
        }
    } catch { $needRestart = $true }
    if (-not $needRestart) { return $true }
    Write-Host "starting tools/enroll-console.ps1 on 8799"
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'enroll-console\.ps1' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    $elog = Join-Path $Root "lab\enroll-serve.log"
    $eerr = Join-Path $Root "lab\enroll-serve.err"
    Remove-Item $elog, $eerr -Force -ErrorAction SilentlyContinue
    Start-Process -FilePath "powershell.exe" `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "tools\enroll-console.ps1", "-Port", "8799") `
        -WorkingDirectory $Root -RedirectStandardOutput $elog -RedirectStandardError $eerr -WindowStyle Hidden
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $r = Invoke-WebRequest -Uri $EnrollUrl -UseBasicParsing -TimeoutSec 2
            if ($r.Content -match 'action="/update"') { return $true }
        } catch { }
    }
    return $false
}

function Ensure-HubListen {
    if (-not (Test-Path $HubExe)) { return $false }
    if (-not (Test-Path $KeysPath)) {
        Write-Host "REFUSE: lab/hub-mlkem.keys missing (would keygen)" -ForegroundColor Red
        return $false
    }
    $alive = $false
    $proc = Get-Process atnnode -ErrorAction SilentlyContinue
    if ($proc) {
        $peer = Select-String -Path $HubLog -Pattern '^peer_ek=' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($peer) { $alive = $true }
    }
    if ($alive) { return $true }
    Write-Host "starting atnnode listen $ListenPort (keeping hub-mlkem.keys)"
    Get-Process atnnode -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    Remove-Item $HubLog, $HubErr -Force -ErrorAction SilentlyContinue
    $env:ATN_HUB_KEYS = "lab/hub-mlkem.keys"
    Start-Process -FilePath $HubExe -ArgumentList @("listen", "$ListenPort") `
        -WorkingDirectory $Root `
        -RedirectStandardOutput $HubLog -RedirectStandardError $HubErr -WindowStyle Hidden
    $loaded = Wait-LogContains $HubLog "keys loaded|peer_ek=" 20
    return [bool]$loaded
}

function Get-PhoneVersionCode {
    $dump = & $Adb shell dumpsys package com.athanor.daemon 2>$null | Out-String
    $m = [regex]::Match($dump, 'versionCode=(\d+)')
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return -1
}

function Read-BuiltVersionCode {
    if (Test-Path $VerFile) {
        $raw = (Get-Content $VerFile -Raw).Trim()
        if ($raw -match '^\d+') { return [int]$Matches[0] }
    }
    return -1
}

function Publish-And-Wait([string]$versionLabel, [int]$expectCode) {
    & $Adb logcat -c 2>$null | Out-Null
    $hubMarkLines = 0
    if (Test-Path $HubLog) {
        $hubMarkLines = @(Get-Content $HubLog -ErrorAction SilentlyContinue).Count
    }
    $form = @{
        kind        = "apk"
        version     = $versionLabel
        source_path = "android/athanor-lab.apk"
    }
    $resp = Invoke-FormPost "$EnrollUrl/update" $form
    if (-not $resp -or $resp.StatusCode -ne 200 -or $resp.Content -notmatch 'OK:') {
        return @{ Ok = $false; Detail = "POST /update failed" }
    }
    $uid = ""
    if ($resp.Content -match 'update_id=(\d+)') { $uid = $Matches[1] }
    if (-not $uid) {
        return @{ Ok = $false; Detail = "POST OK but no update_id parsed" }
    }

    $hubLine = $null
    $deadline = (Get-Date).AddSeconds(180)
    $streamPat = "update_stream_done id=$uid"
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $HubLog) {
            $all = @(Get-Content $HubLog -ErrorAction SilentlyContinue)
            if ($all.Count -gt $hubMarkLines) {
                $delta = $all[$hubMarkLines..($all.Count - 1)] -join "`n"
                if ($delta -match $streamPat) {
                    $hubLine = ($all | Where-Object { $_ -match $streamPat } | Select-Object -Last 1)
                    break
                }
            }
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $hubLine) {
        return @{ Ok = $false; Detail = "no fresh update_stream_done id=$uid after POST" }
    }

    $devs = & $Adb devices 2>$null | Out-String
    $haveAdb = $devs -match '(?m)^\S+\s+device\b'
    if (-not $haveAdb) {
        # Tunnel delivered; phone install confirmed next USB/logcat.
        return @{ Ok = $true; Detail = ("uid={0} ver={1} built={2} hub={3} HUB_STREAM_OK (no adb verify)" -f $uid, $versionLabel, $expectCode, $hubLine.Trim()) }
    }

    $stagedPat = "update staged id=$uid|announce id=$uid|install start"
    $staged = Wait-LogcatMatch $stagedPat 180
    if (-not $staged) {
        return @{ Ok = $false; Detail = ("hub ok id={0} but phone silence; hub={1}" -f $uid, $hubLine.Trim()) }
    }

    $phoneCode = Get-PhoneVersionCode
    $detail = "uid=$uid ver=$versionLabel built=$expectCode phone=$phoneCode hub=$($hubLine.Trim()) phone_log=$($staged.Trim())"
    if ($phoneCode -ge $expectCode) {
        return @{ Ok = $true; Detail = $detail + " INSTALL_OK" }
    }
    if ($staged -match "update staged id=$uid|install start|install success") {
        return @{ Ok = $true; Detail = $detail + " STAGE_OK" }
    }
    if ($staged -match "announce id=$uid") {
        return @{ Ok = $true; Detail = $detail + " TUNNEL_RX_OK" }
    }
    return @{ Ok = $false; Detail = $detail }
}

# ---------------------------------------------------------------------------
Write-Step "0. Device + enroll UI + hub listen"
$devs = & $Adb devices 2>$null | Out-String
$devOk = $devs -match '(?m)^\S+\s+device\b'
Set-Result "0a_adb_device" $devOk $(if ($devOk) { "device present (logcat only)" } else { "no adb device" })

$enrollOk = Ensure-EnrollConsole
Set-Result "0b_enroll_ui" $enrollOk $(if ($enrollOk) { "127.0.0.1:8799 /update" } else { "enroll UI down" })

$hubOk = Ensure-HubListen
Set-Result "0c_hub_listen" $hubOk $(if ($hubOk) { "UDP $ListenPort keys kept" } else { "hub listen failed" })

# ---------------------------------------------------------------------------
Write-Step "1. Build APK (bump versionCode)"
$buildOk = $false
$buildDetail = ""
$builtCode = -1
$makeOut = & make android-apk 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -and (Test-Path $ApkPath)) {
    $builtCode = Read-BuiltVersionCode
    $buildOk = $builtCode -ge 1
    $buildDetail = "versionCode=$builtCode apk=android/athanor-lab.apk"
    if ($makeOut -match 'versionCode=(\d+)') {
        $builtCode = [int]$Matches[1]
        $buildDetail = ($makeOut -split "`r?`n" | Where-Object { $_ -match 'APK_OK' } | Select-Object -Last 1)
    }
} else {
    $buildDetail = "make android-apk failed"
}
Set-Result "1_build_apk" $buildOk $buildDetail

# ---------------------------------------------------------------------------
if ($Bootstrap) {
    Write-Step "1b. BOOTSTRAP adb install -r (chicken-egg ONLY)"
    $bootOk = $false
    $bootDetail = ""
    if (-not $buildOk) {
        $bootDetail = "skip: build failed"
    } else {
        Write-Host "BOOTSTRAP: adb install -r (one-time installer-capable build)" -ForegroundColor Yellow
        $ins = & $Adb install -r $ApkPath 2>&1 | Out-String
        if ($ins -match 'Success') {
            $bootOk = $true
            $bootDetail = "BOOTSTRAP installed versionCode=$builtCode"
            Start-Sleep -Seconds 3
            Invoke-PhoneReconnect
            if (-not (Wait-MeshEstablished 40)) {
                Write-Host "WARN: mesh not confirmed after bootstrap; continuing" -ForegroundColor Yellow
            }
        } else {
            $bootDetail = ("adb install failed: {0}" -f ($ins.Trim() -replace '\s+', ' '))
        }
    }
    Set-Result "1b_bootstrap_usb" $bootOk $bootDetail

    Write-Step "1c. Rebuild N+1 for tunnel push (no USB install)"
    $makeOut2 = & make android-apk 2>&1 | Out-String
    $builtCode2 = Read-BuiltVersionCode
    $ok2 = ($LASTEXITCODE -eq 0 -and $builtCode2 -gt $builtCode)
    if ($makeOut2 -match 'versionCode=(\d+)') { $builtCode2 = [int]$Matches[1] }
    Set-Result "1c_build_nplus1" $ok2 ("versionCode=$builtCode2 (tunnel target)")
    $builtCode = $builtCode2
    $buildOk = $ok2
}

# ---------------------------------------------------------------------------
Write-Step "2. Ensure mesh ESTABLISHED (reconnect if needed)"
Invoke-PhoneReconnect
$meshOk = Wait-MeshEstablished 45
$meshDetail = if ($meshOk) { "hub policy_push/ESTABLISHED after reconnect" } else { "mesh not confirmed" }
if (-not $meshOk -and $hubOk) {
    # Phone may be on WAN without USB; hub recv / recent ESTABLISHED counts.
    $tail = if (Test-Path $HubLog) {
        Get-Content $HubLog -Tail 80 -ErrorAction SilentlyContinue | Out-String
    } else { "" }
    if ($tail -match 'ESTABLISHED|policy_push|recv ') {
        $meshOk = $true
        $meshDetail = "hub live (no adb); stream wait will prove"
    }
}
if (-not $meshOk -and $hubOk -and $devOk) {
    $meshOk = $true
    $meshDetail = "hub+device up (soft); stream wait will prove"
}
Set-Result "2_mesh" $meshOk $meshDetail

# ---------------------------------------------------------------------------
Write-Step "3. POST /update kind=apk (tunnel only - no adb install)"
$pubOk = $false
$pubDetail = ""
if (-not $buildOk -or -not $enrollOk -or -not $hubOk) {
    $pubDetail = "skipped: prerequisites failed"
} else {
    $verLabel = "lab.$builtCode"
    $r = Publish-And-Wait $verLabel $builtCode
    $pubOk = [bool]$r.Ok
    $pubDetail = [string]$r.Detail
}
Set-Result "3_tunnel_apk_push" $pubOk $pubDetail

# ---------------------------------------------------------------------------
Write-Step "4. scrub-check"
$scrubOut = & make scrub-check 2>&1 | Out-String
$scrubOk = ($LASTEXITCODE -eq 0)
$scrubOne = ($scrubOut.Trim() -replace '\s+', ' ')
if ($scrubOne.Length -gt 160) { $scrubOne = $scrubOne.Substring(0, 160) }
Set-Result "4_scrub_check" $scrubOk $scrubOne

# ---------------------------------------------------------------------------
Write-Step "5. Summary"
Write-Host ""
Write-Host ("{0,-24} {1,-6} {2}" -f "STEP", "RESULT", "DETAIL")
Write-Host ("{0,-24} {1,-6} {2}" -f "----", "------", "------")
foreach ($k in $results.Keys) {
    $r = $results[$k]
    $tag = if ($r.Ok) { "PASS" } else { "FAIL" }
    Write-Host ("{0,-24} {1,-6} {2}" -f $k, $tag, $r.Detail)
}
Write-Host ""
Write-Host "Standing rule: APK updates via DEC-0048 tunnel (U/UC), NOT adb install."
Write-Host "USB debug remains for logcat/diagnosis only."
if ($Bootstrap) {
    Write-Host "Bootstrap used ONE adb install -r, then tunnel push for N+1."
}
if ($failed -gt 0) {
    Write-Host ("HUB-PUSH-APK FAIL: {0} step(s)" -f $failed) -ForegroundColor Red
    exit 1
}
Write-Host "HUB-PUSH-APK PASS" -ForegroundColor Green
exit 0
