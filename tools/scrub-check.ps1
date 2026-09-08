#Requires -Version 5.1
<#
.SYNOPSIS
  Block git push if org-local IPs/domains appear in tracked files.

.DESCRIPTION
  Loads secrets from lab/org.local.json (and optionally lab/deploy-state.json).
  Scans only `git ls-files` content. Prefer block over silent rewrite.
  Exit 0 if clean; exit 1 with file:line hits.
#>
$ErrorActionPreference = "Stop"

$Root = (git rev-parse --show-toplevel 2>$null)
if (-not $Root) {
    Write-Error "scrub-check: not inside a git work tree"
    exit 1
}
Set-Location $Root

function Test-IsPlaceholder([string]$v) {
    if ([string]::IsNullOrWhiteSpace($v)) { return $true }
    $t = $v.Trim()
    if ($t -match '^REPLACE_') { return $true }
    if ($t -match '^<.*>$') { return $true }
    if ($t -match '^(YOUR_|TODO_|CHANGEME|example\.|localhost)') { return $true }
    return $false
}

function Test-IsIPv4([string]$ip) {
    if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { return $false }
    foreach ($o in ($ip -split '\.')) {
        $n = [int]$o
        if ($n -lt 0 -or $n -gt 255) { return $false }
    }
    return $true
}

function Add-Secret([System.Collections.Generic.HashSet[string]]$set, [string]$v, [string]$kind) {
    if (Test-IsPlaceholder $v) { return }
    $t = $v.Trim()
    if ($kind -eq "ipv4") {
        if (-not (Test-IsIPv4 $t)) { return }
        # Skip obviously non-org locals that are common in docs
        if ($t -eq "127.0.0.1" -or $t -eq "0.0.0.0") { return }
    }
    [void]$set.Add($t)
}

function Test-LooksLikeDomain([string]$v) {
    $t = $v.Trim()
    if ($t.Length -lt 4 -or $t.Length -gt 253) { return $false }
    if (Test-IsIPv4 $t) { return $false }
    # hostname-ish: labels.tld (block org domains from org.local.json)
    return ($t -match '^[A-Za-z0-9]([A-Za-z0-9\-]*[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9\-]*[A-Za-z0-9])?)+$')
}

function Add-AnyOrgString([System.Collections.Generic.HashSet[string]]$set, [string]$v) {
    if (Test-IsPlaceholder $v) { return }
    $t = $v.Trim()
    if (Test-IsIPv4 $t) {
        Add-Secret $set $t "ipv4"
        return
    }
    if (Test-LooksLikeDomain $t) {
        Add-Secret $set $t "domain"
    }
}

function Load-JsonSecrets([string]$path, [System.Collections.Generic.HashSet[string]]$set) {
    if (-not (Test-Path $path)) { return }
    try {
        $j = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Host ("scrub-check: warn: could not parse {0}" -f $path) -ForegroundColor Yellow
        return
    }
    # Named fields first, then any other string props (notes may embed extras).
    foreach ($k in @("hub_lan_ipv4", "public_ipv4", "phone_peer_ipv4", "edge_lan_ipv4", "domain")) {
        $p = $j.PSObject.Properties[$k]
        if ($null -ne $p -and $null -ne $p.Value) {
            Add-AnyOrgString $set ([string]$p.Value)
        }
    }
    foreach ($p in $j.PSObject.Properties) {
        if ($null -eq $p.Value) { continue }
        if ($p.Value -is [string]) {
            Add-AnyOrgString $set ([string]$p.Value)
            # Pull dotted tokens from free-text notes
            foreach ($m in [regex]::Matches([string]$p.Value, '\b(?:\d{1,3}\.){3}\d{1,3}\b|[A-Za-z0-9][A-Za-z0-9\.\-]{1,250}\.[A-Za-z]{2,24}\b')) {
                Add-AnyOrgString $set $m.Value
            }
        }
    }
}

function Test-IsAllowlisted([string]$rel) {
    $n = $rel -replace '\\', '/'
    if ($n -eq "lab/org.local.json.example") { return $true }
    if ($n -like "*.example") { return $true }
    if ($n -eq "tools/scrub-check.ps1" -or $n -eq "tools/scrub-check.sh") { return $true }
    if ($n -eq "tools/install-git-hooks.ps1" -or $n -eq "tools/install-git-hooks.sh") { return $true }
    return $false
}

$secrets = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
Load-JsonSecrets (Join-Path $Root "lab\org.local.json") $secrets
Load-JsonSecrets (Join-Path $Root "lab\deploy-state.json") $secrets

if ($secrets.Count -eq 0) {
    Write-Host "scrub-check: no org secrets loaded (lab/org.local.json empty or missing) - OK"
    exit 0
}

$files = @(git ls-files)

$hits = New-Object System.Collections.Generic.List[string]
foreach ($rel in $files) {
    if ([string]::IsNullOrWhiteSpace($rel)) { continue }
    if (Test-IsAllowlisted $rel) { continue }
    $full = Join-Path $Root $rel
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
    # Skip binary-ish
    try {
        $bytes = [System.IO.File]::ReadAllBytes($full)
        if ($bytes.Length -eq 0) { continue }
        $sample = [Math]::Min($bytes.Length, 8000)
        $nul = $false
        for ($i = 0; $i -lt $sample; $i++) {
            if ($bytes[$i] -eq 0) { $nul = $true; break }
        }
        if ($nul) { continue }
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    } catch { continue }

    $lines = $text -split "`r?`n", -1
    for ($ln = 0; $ln -lt $lines.Length; $ln++) {
        $line = $lines[$ln]
        foreach ($s in $secrets) {
            if ($line.Contains($s)) {
                $hits.Add(("{0}:{1}: contains org value '{2}'" -f ($rel -replace '\\', '/'), ($ln + 1), $s))
            }
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "scrub-check: FAIL - org-local values found in tracked files:" -ForegroundColor Red
    foreach ($h in $hits) { Write-Host ("  {0}" -f $h) }
    Write-Host ""
    Write-Host "Remove these values from tracked content (keep them in gitignored lab/org.local.json)."
    Write-Host "Push blocked."
    exit 1
}

Write-Host ("scrub-check: OK - {0} org secret(s) not present in tracked files" -f $secrets.Count)
exit 0
