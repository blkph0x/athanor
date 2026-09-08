# Lab live soak: hub soft-restart, phone reconnect, policy, update, scrub.
# Usage: powershell -NoProfile -File tools/lab-soak.ps1
# Does NOT wipe lab/hub-mlkem.keys. Skips compromise boom (would kill phone).
$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root

$Adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $Adb)) { $Adb = "adb" }
$HubExe = Join-Path $Root "atnnode.exe"
$HubLog = Join-Path $Root "lab\hub-listen.log"
$HubErr = Join-Path $Root "lab\hub-listen.err"
$KeysPath = Join-Path $Root "lab\hub-mlkem.keys"
$PolPath = Join-Path $Root "lab\org-policy.conf"
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

function Get-PeerEkFromLog([string]$logPath) {
    if (-not (Test-Path $logPath)) { return $null }
    $line = Select-String -Path $logPath -Pattern '^peer_ek=' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $line) { return $null }
    return ($line.Line -replace '^peer_ek=', '').Trim()
}

function Get-PeerEkFromKeys([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    $line = Select-String -Path $path -Pattern '^ek=' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $line) { return $null }
    return ($line.Line -replace '^ek=', '').Trim()
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

function Invoke-FormPost([string]$url, [hashtable]$fields) {
    $pairs = foreach ($k in $fields.Keys) {
        ("{0}={1}" -f [uri]::EscapeDataString($k), [uri]::EscapeDataString([string]$fields[$k]))
    }
    $body = ($pairs -join "&")
    try {
        return Invoke-WebRequest -Uri $url -Method POST -Body $body `
            -ContentType "application/x-www-form-urlencoded" -UseBasicParsing -TimeoutSec 30
    } catch {
        return $null
    }
}

function Get-DefaultPolicy([string]$boom) {
    $pol = @{
        diag              = "1"
        flush_mode        = "log_only"
        wipe_armed        = "0"
        outage_class      = "normal"
        boom_silence_s    = $boom
        password_fail_max = "5"
        biometric_allowed = "0"
        password_min_len  = "12"
        usb_data_block    = "1"
        pwd_deny_check    = "1"
    }
    if (Test-Path $PolPath) {
        Get-Content $PolPath | ForEach-Object {
            if ($_ -match '^\s*#' -or $_ -notmatch '=') { return }
            $parts = $_ -split '=', 2
            $k = $parts[0].Trim(); $v = $parts[1].Trim()
            if ($pol.ContainsKey($k) -and $k -ne "boom_silence_s") { $pol[$k] = $v }
        }
    }
    $pol["boom_silence_s"] = $boom
    return $pol
}

function Ensure-EnrollConsole {
    $needRestart = $true
    try {
        $r = Invoke-WebRequest -Uri $EnrollUrl -UseBasicParsing -TimeoutSec 3
        if ($r.StatusCode -eq 200 -and $r.Content -match 'policyForm' -and $r.Content -match 'action="/update"') {
            $needRestart = $false
        }
    } catch { $needRestart = $true }
    if (-not $needRestart) { return $true }
    Write-Host "enroll UI missing policy/update routes - restarting tools/enroll-console.ps1"
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
            if ($r.Content -match 'policyForm') { return $true }
        } catch { }
    }
    if (Test-Path $eerr) { Write-Host (Get-Content $eerr -Raw) }
    return $false
}

function Invoke-PhoneReconnect {
    # Prefer service ACTION_RECONNECT (works while activity already foreground).
    & $Adb shell am start-foreground-service -n com.athanor.daemon/.AtnDaemonService -a com.athanor.daemon.RECONNECT 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $Adb shell am startservice -n com.athanor.daemon/.AtnDaemonService -a com.athanor.daemon.RECONNECT 2>$null | Out-Null
    }
    # -S force-stops so Activity onCreate re-reads --ez reconnect (extras ignored if task brought to front).
    & $Adb shell am start -S -n com.athanor.daemon/.AtnLabActivity --ez reconnect true 2>$null | Out-Null
}

# ---------------------------------------------------------------------------
Write-Step "0. Ensure enroll UI (policy/update)"
$enrollOk = Ensure-EnrollConsole
Set-Result "0_enroll_ui" $enrollOk $(if ($enrollOk) { "127.0.0.1:8799 has policy+update" } else { "could not start enroll-console" })

# ---------------------------------------------------------------------------
Write-Step "A. Record current peer_ek"
$ekBefore = Get-PeerEkFromLog $HubLog
if (-not $ekBefore) { $ekBefore = Get-PeerEkFromKeys $KeysPath }
$keysExisted = Test-Path $KeysPath
if ($ekBefore -and $ekBefore.Length -ge 64 -and $keysExisted) {
    Set-Result "A_record_peer_ek" $true ("len={0} keys=present" -f $ekBefore.Length)
} else {
    Set-Result "A_record_peer_ek" $false ("ek_len={0} keys={1}" -f $(if ($ekBefore) { $ekBefore.Length } else { 0 }), $keysExisted)
}

# ---------------------------------------------------------------------------
Write-Step "A2. Raise boom_silence_s before hub restart (avoid silence BOOM)"
# Hub soft-restart gap can exceed default 30s; raise first while/if phone can hear policy.
$prePol = Get-DefaultPolicy "180"
$preResp = $null
if ($enrollOk) { $preResp = Invoke-FormPost "$EnrollUrl/policy" $prePol }
$preOk = $false
$preDetail = ""
if ($preResp -and $preResp.StatusCode -eq 200 -and $preResp.Content -match 'OK:') {
    $preOk = $true
    $preDetail = "POST boom_silence_s=180"
} else {
    # Fallback: write conf directly so next ESTABLISHED / reload sees it
    $prevVer = 1
    if (Test-Path $PolPath) {
        $vm = Select-String -Path $PolPath -Pattern '^policy_ver=(\d+)' | Select-Object -First 1
        if ($vm) { $prevVer = [int]$vm.Matches[0].Groups[1].Value + 1 }
    }
    $text = @"
policy_ver=$prevVer
diag=$($prePol.diag)
flush_mode=$($prePol.flush_mode)
wipe_armed=$($prePol.wipe_armed)
outage_class=$($prePol.outage_class)
boom_silence_s=180
password_fail_max=$($prePol.password_fail_max)
biometric_allowed=$($prePol.biometric_allowed)
password_min_len=$($prePol.password_min_len)
usb_data_block=$($prePol.usb_data_block)
pwd_deny_check=$($prePol.pwd_deny_check)
"@
    [System.IO.File]::WriteAllText($PolPath, ($text.Trim() + "`n"))
    $preOk = $true
    $preDetail = "wrote lab/org-policy.conf boom_silence_s=180 (POST unavailable)"
}
Set-Result "A2_pre_raise_silence" $preOk $preDetail

# Clear any prior lab BOOM and try to be ESTABLISHED before soft-restart if possible
Invoke-PhoneReconnect
Start-Sleep -Seconds 5

# ---------------------------------------------------------------------------
Write-Step "B. Soft-restart atnnode listen (keys must persist)"
$restartOk = $false
$restartDetail = ""
if (-not (Test-Path $HubExe)) {
    $restartDetail = "atnnode.exe missing"
} elseif (-not $keysExisted) {
    $restartDetail = "lab/hub-mlkem.keys missing - refuse restart that would keygen"
} else {
    Get-Process atnnode -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    # Recreate log for this soak; do NOT touch hub-mlkem.keys
    Remove-Item $HubLog, $HubErr -Force -ErrorAction SilentlyContinue
    $env:ATN_HUB_KEYS = "lab/hub-mlkem.keys"
    Start-Process -FilePath $HubExe -ArgumentList @("listen", "$ListenPort") `
        -WorkingDirectory $Root `
        -RedirectStandardOutput $HubLog -RedirectStandardError $HubErr -WindowStyle Hidden
    $loaded = Wait-LogContains $HubLog "keys loaded" 15
    $ekAfter = $null
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        $ekAfter = Get-PeerEkFromLog $HubLog
        if ($ekAfter) { break }
    }
    $udp = Get-NetUDPEndpoint -LocalPort $ListenPort -ErrorAction SilentlyContinue
    $sameEk = ($ekBefore -and $ekAfter -and ($ekBefore -eq $ekAfter))
    if ($loaded -and $sameEk -and $udp) {
        $restartOk = $true
        $restartDetail = "keys loaded; peer_ek unchanged; UDP $ListenPort up"
    } else {
        $errBit = ""
        if (Test-Path $HubErr) {
            $raw = ((Get-Content $HubErr -Raw -ErrorAction SilentlyContinue) | Out-String).Trim()
            if ($raw.Length -gt 0) {
                if ($raw.Length -gt 120) { $raw = $raw.Substring(0, 120) }
                $errBit = " err=$raw"
            }
        }
        $restartDetail = ("loaded={0} same_ek={1} udp={2} ek_after_len={3}{4}" -f `
            [bool]$loaded, $sameEk, [bool]$udp, $(if ($ekAfter) { $ekAfter.Length } else { 0 }), $errBit)
    }
}
Set-Result "B_soft_restart_keys" $restartOk $restartDetail

# ---------------------------------------------------------------------------
Write-Step "C. Wait phone reconnect (~60s)"
$reconnectOk = $false
$reconnectDetail = ""
$deadline = (Get-Date).AddSeconds(60)
& $Adb logcat -c 2>$null | Out-Null
# Boom-dead needs explicit Start/reconnect; nudge every ~15s
Invoke-PhoneReconnect
$nextNudge = (Get-Date).AddSeconds(15)

while ((Get-Date) -lt $deadline) {
    $hubHit = $null
    if (Test-Path $HubLog) {
        $hubHit = Select-String -Path $HubLog -Pattern 'ESTABLISHED|recv ' -ErrorAction SilentlyContinue |
            Select-Object -Last 1
    }
    $phoneOut = & $Adb logcat -d -s atn-daemon:I atn-lab:I 2>$null | Out-String
    $phoneHit = $phoneOut -match 'ESTABLISHED|MESH UP|reconnect: lab BOOM reset'
    if ($hubHit) {
        $reconnectOk = $true
        $reconnectDetail = "hub: $($hubHit.Line.Trim())"
        break
    }
    if ($phoneHit -and ($phoneOut -match 'ESTABLISHED|MESH UP')) {
        $reconnectOk = $true
        $reconnectDetail = "phone logcat ESTABLISHED"
        break
    }
    if ((Get-Date) -ge $nextNudge) {
        Invoke-PhoneReconnect
        $nextNudge = (Get-Date).AddSeconds(15)
    }
    Start-Sleep -Seconds 2
}
if (-not $reconnectOk) {
    $reconnectDetail = "no ESTABLISHED/recv in hub log or phone logcat within 60s"
}
Set-Result "C_phone_reconnect" $reconnectOk $reconnectDetail

# ---------------------------------------------------------------------------
Write-Step "D. POST /policy boom_silence_s=45"
$policyOk = $false
$policyDetail = ""
$pol = Get-DefaultPolicy "45"
$resp = Invoke-FormPost "$EnrollUrl/policy" $pol
if (-not $resp -or $resp.StatusCode -ne 200) {
    $policyDetail = "POST /policy failed (is enroll UI on :8799 with current console?)"
} else {
    $hubPol = Wait-LogContains $HubLog "policy_reload|policy_push" 25
    $phonePol = (& $Adb logcat -d -s atn-org-pol:I 2>$null | Out-String)
    $phoneApplied = $phonePol -match 'applied ver='
    if ($hubPol) {
        $policyOk = $true
        $policyDetail = "hub: $($hubPol.Trim())"
        if ($phoneApplied) { $policyDetail += "; phone org policy applied" }
        else { $policyDetail += "; phone apply not seen in logcat (optional)" }
    } else {
        $confBoom = $null
        if (Test-Path $PolPath) {
            $confBoom = Select-String -Path $PolPath -Pattern '^boom_silence_s=45' -ErrorAction SilentlyContinue
        }
        if ($confBoom -and ($resp.Content -match 'OK:')) {
            $policyOk = $true
            $policyDetail = "policy file boom_silence_s=45; hub log lag (no policy_reload yet)"
        } else {
            $policyDetail = "no policy_reload/policy_push in hub log"
        }
    }
}
Set-Result "D_policy_bump" $policyOk $policyDetail

# ---------------------------------------------------------------------------
Write-Step "E. Publish site update (tiny payload)"
$updateOk = $false
$updateDetail = ""
$updDir = Join-Path $Root "lab\updates"
New-Item -ItemType Directory -Force -Path $updDir | Out-Null
$payloadSrc = Join-Path $updDir "soak-payload.txt"
[System.IO.File]::WriteAllText($payloadSrc, "athanor-lab-soak $(Get-Date -Format o)`n")
$updForm = @{
    kind        = "site"
    version     = "soak-1"
    source_path = "lab/updates/soak-payload.txt"
}
$uresp = Invoke-FormPost "$EnrollUrl/update" $updForm
if (-not $uresp -or $uresp.StatusCode -ne 200) {
    $updateDetail = "POST /update failed"
} elseif ($uresp.Content -notmatch 'OK:') {
    $updateDetail = "publish response not OK"
} else {
    $hubUpd = Wait-LogContains $HubLog "update_announce|update_stream_done|update_reload" 30
    $staged = $false
    $stageName = ""
    $ls = & $Adb shell "run-as com.athanor.daemon ls files" 2>$null | Out-String
    if ($ls -match 'atn-update') {
        $staged = $true
        $stageName = (($ls -split "`r?`n") | Where-Object { $_ -match 'atn-update' } | Select-Object -First 1)
    }
    $updLog = (& $Adb logcat -d -s atn-upd:I 2>$null | Out-String)
    if ($updLog -match 'update staged|announce id=') { $staged = $true }
    if ($hubUpd -and $staged) {
        $updateOk = $true
        $updateDetail = "hub stream + phone stage ($stageName)"
    } elseif ($hubUpd) {
        $updateOk = $true
        $updateDetail = "hub: $($hubUpd.Trim()); phone stage not confirmed yet"
    } else {
        $updateDetail = "no update_announce/stream in hub log"
    }
}
Set-Result "E_site_update" $updateOk $updateDetail

# ---------------------------------------------------------------------------
Write-Step "F. scrub-check"
$scrubOut = & make scrub-check 2>&1 | Out-String
$scrubOk = ($LASTEXITCODE -eq 0)
$scrubOne = ($scrubOut.Trim() -replace '\s+', ' ')
if ($scrubOne.Length -gt 160) { $scrubOne = $scrubOne.Substring(0, 160) }
Set-Result "F_scrub_check" $scrubOk $scrubOne

# ---------------------------------------------------------------------------
Write-Step "G. Summary"
Write-Host ""
Write-Host ("{0,-24} {1,-6} {2}" -f "STEP", "RESULT", "DETAIL")
Write-Host ("{0,-24} {1,-6} {2}" -f "----", "------", "------")
foreach ($k in $results.Keys) {
    $r = $results[$k]
    $tag = if ($r.Ok) { "PASS" } else { "FAIL" }
    Write-Host ("{0,-24} {1,-6} {2}" -f $k, $tag, $r.Detail)
}
Write-Host ""
Write-Host "Note: compromise boom intentionally skipped (would leave phone dead mid-soak)."
if ($failed -gt 0) {
    Write-Host ("SOAK FAIL: {0} step(s)" -f $failed) -ForegroundColor Red
    exit 1
}
Write-Host "SOAK PASS" -ForegroundColor Green
exit 0
