#Requires -Version 5.1
<#
.SYNOPSIS
  Keep lab atnnode listen alive: restart on crash/exit while preserving hub-mlkem.keys.

.DESCRIPTION
  Polls for atnnode.exe + UDP listen port. If either is gone, soft-restarts
  listen with ATN_HUB_KEYS=lab/hub-mlkem.keys (never deletes keys, never keygens
  when keys are missing). Phone HANDSHAKE recovers once peer_ek matches again.

.PARAMETER Root
  Repo root (default: parent of tools/).

.PARAMETER ListenPort
  UDP port (default 47000).

.PARAMETER IntervalSec
  Health poll interval (default 3).

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\hub-watchdog.ps1
#>
param(
    [string]$Root = "",
    [int]$ListenPort = 47000,
    [int]$IntervalSec = 3
)

$ErrorActionPreference = "Continue"
if (-not $Root) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$Root = $Root.TrimEnd('\', '/')
$HubExe = Join-Path $Root "atnnode.exe"
$KeysPath = Join-Path $Root "lab\hub-mlkem.keys"
$HubLog = Join-Path $Root "lab\hub-listen.log"
$HubErr = Join-Path $Root "lab\hub-listen.err"
$WatchLog = Join-Path $Root "lab\hub-watchdog.log"

function Write-Watch([string]$msg) {
    $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
    Write-Host $line
    try {
        Add-Content -Path $WatchLog -Value $line -ErrorAction SilentlyContinue
    } catch { }
}

function Test-HubHealthy {
    $proc = Get-Process atnnode -ErrorAction SilentlyContinue
    if (-not $proc) { return $false }
    $udp = Get-NetUDPEndpoint -LocalPort $ListenPort -ErrorAction SilentlyContinue
    if (-not $udp) { return $false }
    return $true
}

function Start-HubListen {
    if (-not (Test-Path $HubExe)) {
        Write-Watch "REFUSE: atnnode.exe missing at $HubExe"
        return $false
    }
    if (-not (Test-Path $KeysPath)) {
        Write-Watch "REFUSE: lab/hub-mlkem.keys missing (would keygen)"
        return $false
    }
    Get-Process atnnode -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lab") | Out-Null
    # Rotate listen logs on each start; never touch hub-mlkem.keys
    if (Test-Path $HubLog) {
        try { Move-Item $HubLog ($HubLog + ".prev") -Force -ErrorAction SilentlyContinue } catch { }
    }
    if (Test-Path $HubErr) {
        try { Move-Item $HubErr ($HubErr + ".prev") -Force -ErrorAction SilentlyContinue } catch { }
    }
    $env:ATN_HUB_KEYS = "lab/hub-mlkem.keys"
    Write-Watch "starting atnnode listen $ListenPort (keys kept)"
    Start-Process -FilePath $HubExe -ArgumentList @("listen", "$ListenPort") `
        -WorkingDirectory $Root `
        -RedirectStandardOutput $HubLog -RedirectStandardError $HubErr -WindowStyle Hidden
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        if (Test-Path $HubLog) {
            $txt = Get-Content $HubLog -Raw -ErrorAction SilentlyContinue
            if ($txt -match "keys loaded|peer_ek=") {
                if (Test-HubHealthy) {
                    Write-Watch "hub up (keys loaded, UDP $ListenPort)"
                    return $true
                }
            }
        }
    }
    $err = ""
    if (Test-Path $HubErr) {
        $err = ((Get-Content $HubErr -Raw -ErrorAction SilentlyContinue) | Out-String).Trim()
        if ($err.Length -gt 160) { $err = $err.Substring(0, 160) }
    }
    Write-Watch "hub start incomplete err=$err"
    return [bool](Test-HubHealthy)
}

Write-Watch "hub-watchdog start root=$Root port=$ListenPort"
$backoff = 1
while ($true) {
    if (Test-HubHealthy) {
        $backoff = 1
    } else {
        Write-Watch "hub unhealthy - restart (backoff ${backoff}s)"
        $ok = Start-HubListen
        if (-not $ok) {
            Start-Sleep -Seconds $backoff
            if ($backoff -lt 30) {
                $backoff = [Math]::Min(30, $backoff * 2)
            }
        } else {
            $backoff = 1
        }
    }
    Start-Sleep -Seconds $IntervalSec
}
