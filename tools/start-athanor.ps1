#Requires -Version 5.1
<#
.SYNOPSIS
  Start Athanor hub stack and keep it alive across logons.

.DESCRIPTION
  - Ensures a single hub-watchdog (respawns atnnode listen; keeps lab/hub-mlkem.keys)
  - Optionally starts enroll console on 127.0.0.1:8799
  - Registers a per-user Scheduled Task to run watchdog at logon

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\start-athanor.ps1
#>
param(
    [switch]$NoEnroll,
    [switch]$NoStartup,
    [int]$ListenPort = 47000,
    [int]$EnrollPort = 8799
)

$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root
$GccBin = "C:\Users\Blkph0x\gcc\bin"
if (Test-Path $GccBin) {
    $env:PATH = "$GccBin;$env:PATH"
}

$TaskName = "AthanorHubWatchdog"
$WatchScript = Join-Path $Root "tools\hub-watchdog.ps1"
$EnrollScript = Join-Path $Root "tools\enroll-console.ps1"
$HubExe = Join-Path $Root "atnnode.exe"
$KeysPath = Join-Path $Root "lab\hub-mlkem.keys"
$WatchLog = Join-Path $Root "lab\hub-watchdog.log"

function Write-Step([string]$msg) {
    Write-Host $msg
}

function Test-WatchdogRunning {
    $hits = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -match 'hub-watchdog\.ps1' }
    return [bool]$hits
}

function Stop-WatchdogInstances {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -match 'hub-watchdog\.ps1' } |
        ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
}

function Start-Watchdog {
    if (-not (Test-Path $WatchScript)) {
        Write-Step "FAIL: missing $WatchScript"
        return $false
    }
    if (-not (Test-Path $HubExe)) {
        Write-Step "FAIL: missing atnnode.exe - run make first"
        return $false
    }
    if (-not (Test-Path $KeysPath)) {
        Write-Step "FAIL: missing lab/hub-mlkem.keys (refuse keygen)"
        return $false
    }
    if (Test-WatchdogRunning) {
        Write-Step "watchdog already running"
        return $true
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    Start-Process -FilePath "powershell.exe" `
        -ArgumentList @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-WindowStyle", "Hidden",
            "-File", $WatchScript,
            "-Root", $Root,
            "-ListenPort", "$ListenPort"
        ) `
        -WorkingDirectory $Root -WindowStyle Hidden
    Start-Sleep -Seconds 2
    return (Test-WatchdogRunning)
}

function Wait-HubHealthy([int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $proc = Get-Process atnnode -ErrorAction SilentlyContinue
        $udp = Get-NetUDPEndpoint -LocalPort $ListenPort -ErrorAction SilentlyContinue
        if ($proc -and $udp) {
            return $true
        }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Ensure-EnrollConsole {
    try {
        $r = Invoke-WebRequest -Uri "http://127.0.0.1:$EnrollPort/" -UseBasicParsing -TimeoutSec 2
        if ($r.StatusCode -eq 200 -and $r.Content -match 'action="/update"') {
            Write-Step "enroll console already up :$EnrollPort"
            return $true
        }
    } catch { }
    $listening = Get-NetTCPConnection -LocalPort $EnrollPort -State Listen -ErrorAction SilentlyContinue
    if ($listening) {
        Start-Sleep -Seconds 2
        try {
            $r = Invoke-WebRequest -Uri "http://127.0.0.1:$EnrollPort/" -UseBasicParsing -TimeoutSec 3
            if ($r.Content -match 'action="/update"') { return $true }
        } catch { }
        Write-Step "WARN: port $EnrollPort listening but / not ready"
        return $false
    }
    if (-not (Test-Path $EnrollScript)) {
        Write-Step "WARN: enroll-console.ps1 missing"
        return $false
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    $elog = Join-Path $Root "lab\enroll-serve.log"
    $eerr = Join-Path $Root "lab\enroll-serve.err"
    Start-Process -FilePath "powershell.exe" `
        -ArgumentList @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $EnrollScript,
            "-Port", "$EnrollPort"
        ) `
        -WorkingDirectory $Root `
        -RedirectStandardOutput $elog -RedirectStandardError $eerr -WindowStyle Hidden
    for ($i = 0; $i -lt 25; $i++) {
        Start-Sleep -Milliseconds 400
        try {
            $r = Invoke-WebRequest -Uri "http://127.0.0.1:$EnrollPort/" -UseBasicParsing -TimeoutSec 2
            if ($r.Content -match 'action="/update"') { return $true }
        } catch { }
    }
    return $false
}

function Ensure-StartupTask {
    $startScript = Join-Path $Root "tools\start-athanor.ps1"
    # At logon: start watchdog+enroll. -NoStartup skips re-register.
    $arg = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$startScript`" -ListenPort $ListenPort -NoStartup"
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arg -WorkingDirectory $Root
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -RestartCount 3 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit ([TimeSpan]::Zero) `
        -MultipleInstances IgnoreNew
    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
        -Settings $settings -Principal $principal -Force | Out-Null
    $t = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    return [bool]$t
}

Write-Step "=== Athanor start ==="
Write-Step "root=$Root port=$ListenPort"

$watchOk = Start-Watchdog
if ($watchOk) {
    Write-Step "PASS  watchdog"
} else {
    Write-Step "FAIL  watchdog"
}

$hubOk = Wait-HubHealthy 25
if ($hubOk) {
    Write-Step "PASS  hub listen UDP $ListenPort"
} else {
    Write-Step "FAIL  hub not healthy yet (see lab\hub-watchdog.log)"
    if (Test-Path $WatchLog) {
        Get-Content $WatchLog -Tail 8 | ForEach-Object { Write-Step "  $_" }
    }
}

$enrollOk = $true
if (-not $NoEnroll) {
    $enrollOk = Ensure-EnrollConsole
    if ($enrollOk) {
        Write-Step "PASS  enroll http://127.0.0.1:$EnrollPort/"
    } else {
        Write-Step "FAIL  enroll console"
    }
}

$startupOk = $true
if (-not $NoStartup) {
    $startupOk = Ensure-StartupTask
    if ($startupOk) {
        Write-Step "PASS  startup task '$TaskName' (AtLogOn)"
    } else {
        Write-Step "FAIL  could not register startup task"
    }
}

Write-Step ""
if ($watchOk -and $hubOk -and $startupOk) {
    if (-not $enrollOk -and -not $NoEnroll) {
        Write-Step "ATHANOR UP (enroll console soft-fail - hub/watchdog/startup OK)"
    } else {
        Write-Step "ATHANOR UP - watchdog + listen + startup registered"
    }
    exit 0
}
Write-Step "ATHANOR PARTIAL - check lab\hub-watchdog.log / hub-listen.err"
exit 1
