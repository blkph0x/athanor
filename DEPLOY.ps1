#Requires -Version 5.1
<#
.SYNOPSIS
  Athanor full lab deploy - build hub + APK, configure hosts, launch admin UI,
  first USB phone enroll.

.DESCRIPTION
  ONE entry point for a new org / machine:

    Windows:  powershell -NoProfile -ExecutionPolicy Bypass -File .\DEPLOY.ps1
    Linux/macOS/WSL:  chmod +x DEPLOY.sh && ./DEPLOY.sh
    Auto:     ./DEPLOY   (picks .ps1 or .sh)

  What this does (start to finish):
    1. Checks toolchain (make/gcc, optional Android SDK/NDK)
    2. Asks org questions: hub LAN IP, public WAN IP, domain, UDP port, path mode
    3. Builds everything on this host (make all + lab APK)
    4. Starts hub listener; captures peer_ek
    5. Writes gitignored lab/deploy-state.json + lab/phone-atn-node.conf
       (+ lab/edge/www/edge-state.json for the public edge panel)
    6. Prints edge DNAT install line (run on the DMZ/edge host)
    7. Launches the enroll admin website on http://127.0.0.1:8799/
    8. You plug the first phone (USB debugging) -> Connect & Enroll in the browser

  Never commit real IPs - answers stay under lab/* (gitignored).

.NOTES
  LAN-only first phone: peer_ipv4 = hub LAN.
  Cellular / off-LAN: peer_ipv4 = public WAN (edge must DNAT UDP to hub).
#>
param(
    [switch]$SkipBuild,
    [switch]$NoBrowser,
    [int]$EnrollPort = 8799,
    [int]$HubPort = 0
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
if (-not $Root) { $Root = (Get-Location).Path }
Set-Location $Root

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host ("=== {0} ===" -f $msg) -ForegroundColor Cyan
}
function Write-Ok([string]$msg) { Write-Host ("  OK  {0}" -f $msg) -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host ("  !!  {0}" -f $msg) -ForegroundColor Yellow }
function Ask-Text([string]$prompt, [string]$default = "") {
    if ($default -ne "") {
        $r = Read-Host ("{0} [{1}]" -f $prompt, $default)
        if ([string]::IsNullOrWhiteSpace($r)) { return $default }
        return $r.Trim()
    }
    while ($true) {
        $r = Read-Host $prompt
        if (-not [string]::IsNullOrWhiteSpace($r)) { return $r.Trim() }
        Write-Warn "required"
    }
}
function Ask-YesNo([string]$prompt, [bool]$defaultYes = $true) {
    $hint = if ($defaultYes) { "Y/n" } else { "y/N" }
    $r = Read-Host ("{0} ({1})" -f $prompt, $hint)
    if ([string]::IsNullOrWhiteSpace($r)) { return $defaultYes }
    return ($r -match '^(y|yes)$')
}
function Test-IPv4([string]$ip) {
    if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { return $false }
    foreach ($o in ($ip -split '\.')) {
        $n = [int]$o
        if ($n -lt 0 -or $n -gt 255) { return $false }
    }
    return $true
}
function Prev-Str($obj, [string]$name) {
    if ($null -eq $obj) { return "" }
    $p = $obj.PSObject.Properties[$name]
    if ($null -eq $p -or $null -eq $p.Value) { return "" }
    return [string]$p.Value
}

Write-Host ""
Write-Host "  Athanor lab deploy (full path)"
Write-Host "  ------------------------------"
Write-Host "  Builds the hub + enroll console + stub APK, asks for your hosts/IPs,"
Write-Host "  starts the hub, opens the admin enroll site, then first USB phone."
Write-Host ""
Write-Host "  Docs if you need detail later: docs/LAB.md, docs/EDGE.md, docs/ENROLL.md"
Write-Host ""

# --- toolchain ---
Write-Step "Toolchain"
$gccCandidates = @(
    $env:GCC_BIN,
    ("C:\Users\{0}\gcc\bin" -f $env:USERNAME),
    "C:\msys64\mingw64\bin",
    "C:\mingw64\bin"
) | Where-Object { $_ -and (Test-Path $_) }
foreach ($g in $gccCandidates) {
    $env:PATH = "{0};{1}" -f $g, $env:PATH
    break
}
if (-not (Get-Command make -ErrorAction SilentlyContinue)) {
    $gcc = Ask-Text "Path to MinGW/gcc bin (contains make.exe / gcc.exe)"
    if (-not (Test-Path (Join-Path $gcc "gcc.exe"))) {
        throw ("gcc.exe not found under {0}" -f $gcc)
    }
    $env:PATH = "{0};{1}" -f $gcc, $env:PATH
    $env:GCC_BIN = $gcc
}
Write-Ok ("make = {0}" -f (Get-Command make).Source)
Write-Ok ("gcc  = {0}" -f (Get-Command gcc).Source)

$androidEnv = Join-Path $Root "tools\android_env.ps1"
if (Test-Path $androidEnv) {
    . $androidEnv | Out-Null
}
$adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { $adb = "adb" }
$haveAdb = ($null -ne (Get-Command $adb -ErrorAction SilentlyContinue)) -or (Test-Path $adb)
if ($haveAdb) { Write-Ok "adb present" } else { Write-Warn "adb not found - install Android platform-tools before phone enroll" }

# --- org questions ---
Write-Step "Environment (org hosts - not stored in git)"
Write-Host "  You need:"
Write-Host "    hub_lan_ipv4   - this PC LAN address (ipconfig). Phones on Wi-Fi use this."
Write-Host "    public_ipv4    - WAN IP for cellular phones (optional if LAN-only lab)."
Write-Host "    domain         - DNS name for the edge panel (optional)."
Write-Host "    edge_lan_ipv4  - DMZ/edge Linux host on LAN (optional; for DNAT)."

$prevPath = Join-Path $Root "lab\deploy-state.json"
$prev = $null
if (Test-Path $prevPath) {
    try { $prev = Get-Content $prevPath -Raw | ConvertFrom-Json } catch { $prev = $null }
    if ($prev) { Write-Host "  (found previous lab/deploy-state.json - defaults shown in [brackets])" }
}

$hubLanDefault = Prev-Str $prev "hub_lan_ipv4"
$hubLan = Ask-Text "Hub LAN IPv4 (Windows atnnode host)" $hubLanDefault
if (-not (Test-IPv4 $hubLan)) { throw ("bad hub_lan_ipv4: {0}" -f $hubLan) }

if ($HubPort -gt 0) {
    $portDefault = "$HubPort"
} else {
    $pp = Prev-Str $prev "peer_port"
    if ($pp -ne "") { $portDefault = $pp } else { $portDefault = "47000" }
}
$peerPort = [int](Ask-Text "UDP mesh port" $portDefault)
if ($peerPort -lt 1 -or $peerPort -gt 65535) { throw "bad port" }

$pathDefault = Prev-Str $prev "path_mode"
if ($pathDefault -eq "") { $pathDefault = "lan" }
$pathMode = (Ask-Text "First phone path: lan (Wi-Fi to hub) or public (cellular via WAN)" $pathDefault).ToLowerInvariant()
if ($pathMode -ne "lan" -and $pathMode -ne "public") { throw "path must be lan or public" }

$publicIp = ""
$domain = ""
$edgeLan = ""
$wantEdge = ($pathMode -eq "public") -or (Ask-YesNo "Also configure public edge / cellular path now?" ($pathMode -eq "public"))
if ($wantEdge) {
    $publicIp = Ask-Text "Public WAN IPv4 (phones dial this off-LAN)" (Prev-Str $prev "public_ipv4")
    if ($publicIp -ne "" -and -not (Test-IPv4 $publicIp)) { throw "bad public_ipv4" }
    $domain = Ask-Text "Org domain for edge panel (or blank)" (Prev-Str $prev "domain")
    $edgeLan = Ask-Text "Edge/DMZ host LAN IPv4 (or blank)" (Prev-Str $prev "edge_lan_ipv4")
    if ($edgeLan -ne "" -and -not (Test-IPv4 $edgeLan)) { throw "bad edge_lan_ipv4" }
}

if ($pathMode -eq "public") {
    if ($publicIp -eq "") { throw "public path requires public_ipv4" }
    $phonePeer = $publicIp
} else {
    $phonePeer = $hubLan
}
Write-Ok ("phone peer_ipv4 will be {0} (path={1})" -f $phonePeer, $pathMode)

# --- build ---
if (-not $SkipBuild) {
    Write-Step "Build (make all + lab APK on this host)"
    & make all
    if ($LASTEXITCODE -ne 0) { throw "make all failed" }
    Write-Ok "native binaries (hub, enroll, sign, tests)"
    & make android-apk
    if ($LASTEXITCODE -ne 0) { throw "make android-apk failed - check NDK/JDK (tools/android_env.ps1)" }
    if (-not (Test-Path "android\athanor-lab.apk")) { throw "android/athanor-lab.apk missing" }
    Write-Ok "android/athanor-lab.apk"
} else {
    Write-Warn "SkipBuild: using existing binaries"
}

# --- hub listen ---
Write-Step ("Start hub (atnnode listen {0})" -f $peerPort)
Get-Process atnnode -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
New-Item -ItemType Directory -Force -Path "lab" | Out-Null
$hubLog = Join-Path $Root "lab\hub-listen.log"
$hubErr = Join-Path $Root "lab\hub-listen.err"
Remove-Item $hubLog, $hubErr -Force -ErrorAction SilentlyContinue
$hubExe = Join-Path $Root "atnnode.exe"
Start-Process -FilePath $hubExe -ArgumentList @("listen", "$peerPort") `
    -RedirectStandardOutput $hubLog -RedirectStandardError $hubErr -WindowStyle Hidden
$ek = $null
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    if (Test-Path $hubLog) {
        $line = Select-String -Path $hubLog -Pattern '^peer_ek=' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($line) {
            $ek = ($line.Line -replace '^peer_ek=', '')
            break
        }
    }
}
if (-not $ek -or $ek.Length -lt 64) {
    if (Test-Path $hubErr) { Get-Content $hubErr -Tail 20 }
    throw "hub did not print peer_ek - see lab/hub-listen.log"
}
Write-Ok ("hub listening; peer_ek captured ({0} hex chars)" -f $ek.Length)

# --- persist state (gitignored) ---
Write-Step "Write local config (gitignored)"
$state = [ordered]@{
    hub_lan_ipv4    = $hubLan
    public_ipv4     = $publicIp
    edge_lan_ipv4   = $edgeLan
    domain          = $domain
    peer_port       = "$peerPort"
    peer_ek         = $ek
    path_mode       = $pathMode
    phone_peer_ipv4 = $phonePeer
    updated         = (Get-Date).ToUniversalTime().ToString("o")
    enroll_url      = ("http://127.0.0.1:{0}/" -f $EnrollPort)
}
($state | ConvertTo-Json) | Set-Content -Encoding utf8 $prevPath
Write-Ok "lab/deploy-state.json"

$conf = @"
peer_ipv4=$phonePeer
peer_port=$peerPort
peer_ek=$ek
diag=1
flush_mode=log_only
outage_class=normal
"@
[System.IO.File]::WriteAllText((Join-Path $Root "lab\phone-atn-node.conf"), $conf)
Write-Ok "lab/phone-atn-node.conf"

$edgeDir = Join-Path $Root "lab\edge\www"
if (Test-Path $edgeDir) {
    $edgeDomain = $domain
    if ($edgeDomain -eq "") { $edgeDomain = "mesh.example.org" }
    $edgeState = [ordered]@{
        domain        = $edgeDomain
        public_ipv4   = $publicIp
        hub_lan_ipv4  = $hubLan
        edge_lan_ipv4 = $edgeLan
        peer_port     = "$peerPort"
        peer_ek       = $ek
        diag          = "1"
        flush_mode    = "log_only"
        outage_class  = "normal"
        updated       = $state.updated
    }
    ($edgeState | ConvertTo-Json) | Set-Content -Encoding utf8 (Join-Path $edgeDir "edge-state.json")
    Write-Ok "lab/edge/www/edge-state.json (copy panel to edge host as needed)"
}

if ($publicIp -ne "" -or $edgeLan -ne "") {
    Write-Step "Public edge (run on the DMZ/edge Linux host)"
    Write-Host "  1. Copy lab/edge/atn-udp-forward.sh and lab/edge/www/ to the edge host."
    Write-Host "  2. Install Apache/nginx alias for /atn/ (see lab/edge/apache-atn.conf)."
    Write-Host ("  3. Router: forward UDP {0} to edge host." -f $peerPort)
    Write-Host "  4. On edge host:"
    Write-Host ""
    Write-Host ("       sudo HUB_IP={0} HUB_PORT={1} bash atn-udp-forward.sh install" -f $hubLan, $peerPort)
    Write-Host ""
    Write-Host "  5. Open https://<domain>/atn/ to review/save org settings."
}

# --- enroll admin site ---
Write-Step "Launch enroll admin website"
Get-Process atnenroll -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$enrollExe = Join-Path $Root "atnenroll.exe"
Start-Process -FilePath $enrollExe -ArgumentList @("serve", "$EnrollPort") -WorkingDirectory $Root
Start-Sleep -Seconds 2
$url = "http://127.0.0.1:{0}/" -f $EnrollPort
if (-not $NoBrowser) {
    try { Start-Process $url } catch { Write-Warn ("open browser manually: {0}" -f $url) }
}
Write-Ok ("admin UI: {0} (loopback only)" -f $url)

Write-Step "First mobile deploy"
Write-Host "  1. On the phone: enable Developer options -> USB debugging; plug into this PC."
Write-Host "  2. Accept the USB debugging prompt on the phone."
Write-Host ("  3. In the admin page ({0}):" -f $url)
Write-Host "       - Phone number = roster label only (e.g. +61...)"
Write-Host "       - Hub fields pre-fill from lab/deploy-state.json:"
Write-Host ("           peer_ipv4 = {0}" -f $phonePeer)
Write-Host ("           peer_port = {0}" -f $peerPort)
Write-Host "           peer_ek   = (filled from hub)"
Write-Host "       - Click Connect and Enroll"
Write-Host "  4. On phone: activate Device Admin if asked; wait for MESH UP / ESTABLISHED."
Write-Host ""
Write-Host "  Hub log:  lab\hub-listen.log"
Write-Host "  State:    lab\deploy-state.json"
Write-Host "  Re-run:   powershell -NoProfile -ExecutionPolicy Bypass -File .\DEPLOY.ps1"
Write-Host ""
Write-Host "  Keep atnnode + atnenroll running while testing."

if ($haveAdb) {
    $devOut = & $adb devices 2>&1 | Out-String
    if ($devOut -match '(?m)^([A-Za-z0-9]+)\s+device\b') {
        Write-Ok ("USB device already visible: {0} - use Connect and Enroll in the browser" -f $Matches[1])
    } else {
        Write-Warn "no adb device yet - plug the phone, then enroll in the browser"
    }
}

Write-Host ""
Write-Host "Deploy ready." -ForegroundColor Green
