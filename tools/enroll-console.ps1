# DEC-0042: Lab enroll console - loopback plain HTTP only (127.0.0.1).
# Usage: .\atnenroll.exe serve [port]
#    or: powershell -NoProfile -File tools/enroll-console.ps1 [-Port 8799]
# Browser: loopback port 8799 (plain HTTP, 127.0.0.1 only)
# Phone number = roster label only (never SMS). Air-gap sign = release beta.
param(
    [int]$Port = 8799
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$Adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $Adb)) { $Adb = "adb" }

function Html-Encode([string]$s) {
    if ($null -eq $s) { return "" }
    return [System.Net.WebUtility]::HtmlEncode($s)
}

function Get-Form([string]$body) {
    $map = @{}
    if ([string]::IsNullOrEmpty($body)) { return $map }
    foreach ($pair in $body.Split('&')) {
        $kv = $pair.Split('=', 2)
        $k = [Uri]::UnescapeDataString(($kv[0] -replace '\+', ' '))
        $v = if ($kv.Length -gt 1) { [Uri]::UnescapeDataString(($kv[1] -replace '\+', ' ')) } else { "" }
        $map[$k] = $v
    }
    return $map
}

function Test-PhoneLabel([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return $false }
    if ($p.Length -lt 7 -or $p.Length -gt 32) { return $false }
    return ($p -match '^\+?[0-9][0-9 \-]{5,30}[0-9]$')
}

function Find-AdbDevice {
    try {
        $out = & $Adb devices 2>&1 | Out-String
        if ($out -match '(?m)^([A-Za-z0-9]+)\s+device\b') { return $Matches[1] }
    } catch { }
    return $null
}

function Status-Json {
    $dev = Find-AdbDevice
    $apk = Test-Path "android\athanor-lab.apk"
    $obj = [ordered]@{
        device     = if ($dev) { $dev } else { $null }
        device_ok  = [bool]$dev
        apk_ok     = [bool]$apk
        apk_path   = "android/athanor-lab.apk"
        bind       = (("http" + "://" + "127.0.0.1:{0}/") -f $Port)
        note       = "loopback only; phone_number is a label (no SMS)"
    }
    return ($obj | ConvertTo-Json -Compress)
}

function Page-Html([string]$flash, [string]$detail) {
    $dev = Find-AdbDevice
    $devLine = if ($dev) { "USB device: $dev (ready)" } else { "USB device: none (plug in with USB debugging)" }
    $apk = Test-Path "android\athanor-lab.apk"
    $apkLine = if ($apk) { "APK: android/athanor-lab.apk OK" } else { "APK: missing - run make android-apk" }
    $flashHtml = ""
    if ($flash) {
        $cls = "flash"
        if ($flash -match '^ERR') { $cls = "flash err" }
        $flashHtml = "<div class='$cls'>" + (Html-Encode $flash) + "</div>"
    }
    $detailHtml = ""
    if ($detail) {
        $detailHtml = "<pre class='meta'><code>" + (Html-Encode $detail) + "</code></pre>"
    }
    $html = @'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Athanor lab enroll</title>
<style>
body{font-family:Georgia,serif;max-width:42rem;margin:2rem auto;padding:0 1rem;background:#f7f4ef;color:#1a1a1a}
h1{font-size:1.75rem;margin-bottom:0.25rem}
.sub{color:#444;margin-bottom:1.5rem}
label{display:block;margin-top:0.75rem;font-weight:bold}
input,textarea,select{width:100%;box-sizing:border-box;padding:0.5rem;margin-top:0.25rem;font:inherit}
button{margin-top:1.25rem;width:100%;padding:0.85rem;font-size:1.1rem;font-weight:bold;cursor:pointer;background:#1a1a1a;color:#f7f4ef;border:0}
button:disabled{opacity:0.5;cursor:wait}
.flash{padding:0.75rem;background:#e8f0e4;border:1px solid #6a8f5a;margin-bottom:1rem}
.err{background:#f8e8e8;border-color:#a55}
.meta{font-size:0.9rem;color:#333;margin:1rem 0}
code{font-family:Consolas,monospace;font-size:0.85rem}
.live{font-weight:bold}
</style>
</head>
<body>
<h1>Athanor lab enroll</h1>
<p class="sub">Loopback only (DEC-0042). Phone number is a local label - never SMS.
Air-gap signing is release-beta later. Knox DO waits on knoxsdk.jar.
This page stays up; Connect and Enroll re-runs whenever you plug a phone.</p>
<div class="meta" id="status">
<span class="live" id="devLine">__DEVLINE__</span><br/>
<span id="apkLine">__APKLINE__</span><br/>
Bind: __BIND__
</div>
__FLASH__
__DETAIL__
<form method="POST" action="/enroll" id="enrollForm">
<label>Phone number (roster label)</label>
<input name="phone_number" required placeholder="+61..." pattern="\+?[0-9][0-9 \-]{5,30}[0-9]"/>
<label>Hub domain (optional; resolves to peer_ipv4)</label>
<input name="peer_domain" value="mesh.example.org" placeholder="mesh.example.org"/>
<label>Hub peer_ipv4 (dotted; leave blank to use domain)</label>
<input name="peer_ipv4" value="" placeholder="auto from domain or YOUR_HUB_LAN_IPV4"/>
<label>Hub peer_port</label>
<input name="peer_port" value="47000" required/>
<label>Hub peer_ek (hex from atnnode listen)</label>
<textarea name="peer_ek" rows="4" required placeholder="paste peer_ek hex"></textarea>
<label>diag</label>
<select name="diag"><option value="1" selected>1 (lab soak)</option><option value="0">0</option></select>
<label>flush_mode</label>
<select name="flush_mode"><option value="log_only" selected>log_only</option><option value="zeroize">zeroize</option></select>
<label>outage_class</label>
<select name="outage_class">
<option value="normal" selected>normal</option>
<option value="maintenance">maintenance</option>
<option value="blackout">blackout</option>
<option value="faraday">faraday</option>
<option value="capture">capture</option>
</select>
<button type="submit" name="action" value="enroll" id="enrollBtn">Connect and Enroll</button>
</form>
<p class="meta">After enroll: Activate Device Admin on the phone if prompted, then confirm MESH UP.
Receipts land under <code>lab/enrollments/</code> (gitignored).</p>
<script>
(function(){
  var form = document.getElementById('enrollForm');
  var btn = document.getElementById('enrollBtn');
  if (form && btn) {
    form.addEventListener('submit', function(){
      btn.disabled = true;
      btn.textContent = 'Enrolling... keep this page open';
    });
  }
  function tick(){
    fetch('/status').then(function(r){ return r.json(); }).then(function(j){
      var d = document.getElementById('devLine');
      var a = document.getElementById('apkLine');
      if (d) d.textContent = j.device_ok
        ? ('USB device: ' + j.device + ' (ready)')
        : 'USB device: none (plug in with USB debugging)';
      if (a) a.textContent = j.apk_ok
        ? 'APK: android/athanor-lab.apk OK'
        : 'APK: missing - run make android-apk';
    }).catch(function(){});
  }
  setInterval(tick, 3000);
})();
</script>
</body>
</html>
'@
    $bind = ("http" + "://" + "127.0.0.1:$Port/")
    $html = $html.Replace('__DEVLINE__', (Html-Encode $devLine))
    $html = $html.Replace('__APKLINE__', (Html-Encode $apkLine))
    $html = $html.Replace('__BIND__', (Html-Encode $bind))
    $html = $html.Replace('__FLASH__', $flashHtml)
    $html = $html.Replace('__DETAIL__', $detailHtml)
    return $html
}

function Ensure-EnrollKeys {
    $keyDir = Join-Path $Root "lab\enroll-keys"
    New-Item -ItemType Directory -Force -Path $keyDir | Out-Null
    $pk = Join-Path $keyDir "enroll.pk"
    $sk = Join-Path $keyDir "enroll.sk"
    $signExe = Join-Path $Root "atnsign.exe"
    if (-not (Test-Path $signExe)) { return $null }
    if (-not ((Test-Path $pk) -and (Test-Path $sk))) {
        & $signExe keygen $pk $sk 2>&1 | Out-Null
    }
    if ((Test-Path $pk) -and (Test-Path $sk)) {
        return @{ Pk = $pk; Sk = $sk; Exe = $signExe }
    }
    return $null
}

function Invoke-AdbLogged([string]$label, [string[]]$AdbArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $Adb @AdbArgs 2>&1
        $code = $LASTEXITCODE
        foreach ($line in @($out)) {
            $logLine = ("{0}: {1}" -f $label, ([string]$line))
            # NativeCommandError objects stringify oddly; force text.
            if ($line -is [System.Management.Automation.ErrorRecord]) {
                $logLine = ("{0}: {1}" -f $label, $line.ToString())
            }
            $script:LastAdbLog.Add($logLine)
        }
        return $code
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Do-Enroll($form) {
    $script:LastAdbLog = New-Object System.Collections.Generic.List[string]
    $phone = [string]$form["phone_number"]
    $domain = ([string]$form["peer_domain"]).Trim()
    $ipv4 = ([string]$form["peer_ipv4"]).Trim()
    $port = [string]$form["peer_port"]
    $ek = ([string]$form["peer_ek"]).Trim() -replace '\s',''
    $diag = [string]$form["diag"]
    $flush = [string]$form["flush_mode"]
    $outage = [string]$form["outage_class"]

    if (-not (Test-PhoneLabel $phone)) { return @{ Ok=$false; Msg="ERR: bad phone_number label" } }
    if ($ipv4 -eq "" -and $domain -ne "") {
        try {
            $addrs = [System.Net.Dns]::GetHostAddresses($domain)
            $v4 = $addrs | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1
            if ($null -eq $v4) { return @{ Ok=$false; Msg="ERR: domain has no A record" } }
            $ipv4 = $v4.ToString()
        } catch {
            return @{ Ok=$false; Msg="ERR: domain resolve failed" }
        }
    }
    if ($ipv4 -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { return @{ Ok=$false; Msg="ERR: bad peer_ipv4 (or set peer_domain)" } }
    if ($port -notmatch '^\d{1,5}$') { return @{ Ok=$false; Msg="ERR: bad peer_port" } }
    if ($ek.Length -ne 3136 -or $ek -notmatch '^[0-9a-fA-F]+$') {
        return @{ Ok=$false; Msg="ERR: peer_ek must be 3136 hex chars (ML-KEM-1024)" }
    }
    if ($diag -notin @('0','1')) { return @{ Ok=$false; Msg="ERR: diag" } }
    if ($flush -notin @('log_only','zeroize')) { return @{ Ok=$false; Msg="ERR: flush_mode" } }
    if ($outage -notin @('normal','maintenance','blackout','faraday','capture')) {
        return @{ Ok=$false; Msg="ERR: outage_class" }
    }
    if (-not (Test-Path "android\athanor-lab.apk")) {
        return @{ Ok=$false; Msg="ERR: android/athanor-lab.apk missing (make android-apk)" }
    }
    $serial = Find-AdbDevice
    if (-not $serial) {
        return @{ Ok=$false; Msg="ERR: no adb device - enable USB debugging and keep this page open" }
    }

    $safe = ($phone -replace '[^0-9+]','')
    $id = (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + $safe
    $dir = Join-Path $Root "lab\enrollments\$id"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $confPath = Join-Path $dir "atn-node.conf"
    $confLines = @(
        "peer_ipv4=$ipv4"
        "peer_port=$port"
        "peer_ek=$ek"
        "diag=$diag"
        "flush_mode=$flush"
        "outage_class=$outage"
    )
    [System.IO.File]::WriteAllText($confPath, (($confLines -join "`n") + "`n"))
    Copy-Item $confPath "lab\phone-atn-node.conf" -Force

    $log = $script:LastAdbLog
    $log.Add("enrollment_id=$id")
    $log.Add("phone_number_label=$phone")
    $log.Add("serial=$serial")

    $inst = Invoke-AdbLogged "adb_install" @("-s", $serial, "install", "-r", "android\athanor-lab.apk")
    $instText = ($log | Where-Object { $_ -like "adb_install:*" }) -join "`n"
    if ($inst -ne 0 -or $instText -notmatch 'Success') {
        return @{ Ok=$false; Msg="ERR: adb install failed (unlock phone / allow install)"; Detail=($log -join "`n") }
    }

    [void](Invoke-AdbLogged "adb_push" @("-s", $serial, "push", $confPath, "/data/local/tmp/atn-node.conf"))
    [void](Invoke-AdbLogged "adb_mkdir" @("-s", $serial, "shell", "run-as", "com.athanor.daemon", "mkdir", "-p", "files"))
    # Absolute dest: Windows stdin + relative files/ often fails under run-as sh -c.
    $confRc = Invoke-AdbLogged "adb_conf" @(
        "-s", $serial, "shell", "run-as", "com.athanor.daemon",
        "cp", "/data/local/tmp/atn-node.conf",
        "/data/user/0/com.athanor.daemon/files/atn-node.conf"
    )
    if ($confRc -ne 0) {
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $shellCmd = "run-as com.athanor.daemon sh -c `"mkdir -p files; cat > /data/user/0/com.athanor.daemon/files/atn-node.conf`""
            Get-Content -Raw $confPath | & $Adb -s $serial shell $shellCmd 2>&1 | ForEach-Object {
                $log.Add(("adb_conf_stdin: {0}" -f $_))
            }
        } finally {
            $ErrorActionPreference = $prev
        }
    }

    [void](Invoke-AdbLogged "adb_stop" @("-s", $serial, "shell", "am", "force-stop", "com.athanor.daemon"))
    [void](Invoke-AdbLogged "adb_start" @(
        "-s", $serial, "shell", "am", "start",
        "-n", "com.athanor.daemon/.AtnLabActivity",
        "--ez", "autostart", "true",
        "--ez", "request_admin", "true"
    ))

    $receipt = Join-Path $dir "enrollment.txt"
    $hostName = [System.Net.Dns]::GetHostName()
    $utc = (Get-Date).ToUniversalTime().ToString("o")
    $bodyLines = @(
        "ATN-ENROLL-1"
        "id=$id"
        "phone_number_label=$phone"
        "serial=$serial"
        "peer_ipv4=$ipv4"
        "peer_port=$port"
        "diag=$diag"
        "flush_mode=$flush"
        "outage_class=$outage"
        "host=$hostName"
        "time_utc=$utc"
        "note=lab_usb_enroll DEC-0042; local ML-DSA sign OK; air-gap = release beta; Knox DO blocked on T-0400"
    )
    [System.IO.File]::WriteAllText($receipt, (($bodyLines -join "`n") + "`n"))
    $log.Add("receipt=$receipt")

    $keys = Ensure-EnrollKeys
    if ($keys) {
        $sig = Join-Path $dir "enrollment.sig"
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $keys.Exe sign $keys.Sk $receipt $sig 2>&1 | ForEach-Object { $log.Add(("sign: {0}" -f $_)) }
        } finally {
            $ErrorActionPreference = $prev
        }
        if (Test-Path $sig) {
            $log.Add("signed=yes pk=$($keys.Pk)")
        } else {
            $log.Add("signed=no (atnsign sign failed)")
        }
    } else {
        $log.Add("signed=no (build atnsign.exe for local receipt sign)")
    }

    $detail = ($log -join "`n")
    [System.IO.File]::WriteAllText((Join-Path $dir "enroll.log"), $detail)
    return @{ Ok=$true; Msg="OK: enrolled $phone - Activate Device Admin on phone if shown, then mesh."; Detail=$detail }
}

$prefix = ("http" + "://" + "127.0.0.1:$Port/")
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($prefix)
try {
    $listener.Start()
} catch {
    Write-Error "Bind failed on $prefix - is the port free? $_"
    exit 1
}
Write-Host "ATN enroll console (DEC-0042) at $prefix"
Write-Host "USB status polls live; Connect and Enroll stays on this page."
Write-Host "Ctrl+C to stop."

while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    $res = $ctx.Response
    try {
        $path = $req.Url.AbsolutePath
        if ($path -eq "/status") {
            $json = Status-Json
            $buf = [Text.Encoding]::UTF8.GetBytes($json)
            $res.StatusCode = 200
            $res.ContentType = "application/json; charset=utf-8"
            $res.ContentLength64 = $buf.Length
            $res.OutputStream.Write($buf, 0, $buf.Length)
            $res.Close()
            continue
        }
        $flash = ""
        $detail = ""
        if ($req.HttpMethod -eq "POST" -and $path -eq "/enroll") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Do-Enroll $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($path -ne "/" -and $path -ne "/enroll") {
            $res.StatusCode = 404
            $bytes = [Text.Encoding]::ASCII.GetBytes("not found")
            $res.ContentLength64 = $bytes.Length
            $res.OutputStream.Write($bytes, 0, $bytes.Length)
            $res.Close()
            continue
        }
        $html = Page-Html $flash $detail
        $buf = [Text.Encoding]::UTF8.GetBytes($html)
        $res.StatusCode = 200
        $res.ContentType = "text/html; charset=utf-8"
        $res.ContentLength64 = $buf.Length
        $res.OutputStream.Write($buf, 0, $buf.Length)
    } catch {
        $msg = "ERR: $($_.Exception.Message)"
        $buf = [Text.Encoding]::UTF8.GetBytes((Page-Html $msg ""))
        $res.StatusCode = 500
        $res.ContentType = "text/html; charset=utf-8"
        $res.ContentLength64 = $buf.Length
        $res.OutputStream.Write($buf, 0, $buf.Length)
    } finally {
        try { $res.Close() } catch { }
    }
}
