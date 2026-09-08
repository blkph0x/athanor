#Requires -Version 5.1
<#
.SYNOPSIS
  Install pre-push hook that runs tools/scrub-check (blocks push on org IP leak).

.DESCRIPTION
  Writes .git/hooks/pre-push. If core.hooksPath is .githooks (DEC-0006), also
  updates .githooks/pre-push so scrub-check runs before make test.
#>
$ErrorActionPreference = "Stop"

$Root = (git rev-parse --show-toplevel 2>$null)
if (-not $Root) { throw "install-git-hooks: not inside a git work tree" }
Set-Location $Root

$hookBody = @'
#!/bin/sh
# Athanor pre-push: scrub org-local values from tracked files, then make test.
cd "$(git rev-parse --show-toplevel)" || exit 1

echo "pre-push: scrub-check (org-local IPs/domains must not be in tracked files)"
if [ -f tools/scrub-check.sh ] && command -v python3 >/dev/null 2>&1; then
  sh tools/scrub-check.sh || exit 1
elif command -v powershell >/dev/null 2>&1; then
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/scrub-check.ps1 || exit 1
elif command -v pwsh >/dev/null 2>&1; then
  pwsh -NoProfile -File tools/scrub-check.ps1 || exit 1
else
  echo "pre-push: scrub-check unavailable (need python3 or powershell)" >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "pre-push: make is not on PATH. Add the GCC bin dir, then retry."
  echo "  (operator-local MinGW/gcc bin directory)"
  exit 1
fi

echo "pre-push: make test (must match GitHub Actions)"
make test
exit $?
'@

function Write-Hook([string]$path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    # LF for sh hooks on Windows
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, ($hookBody -replace "`r`n", "`n"), $utf8NoBom)
    Write-Host ("  wrote {0}" -f ($path.Substring($Root.Length).TrimStart('\', '/')))
}

$gitHooks = Join-Path $Root ".git\hooks\pre-push"
Write-Hook $gitHooks

$hooksPath = (git config --get core.hooksPath 2>$null)
if ($hooksPath) {
    if (-not [System.IO.Path]::IsPathRooted($hooksPath)) {
        $hooksPath = Join-Path $Root $hooksPath
    }
    $active = Join-Path $hooksPath "pre-push"
    Write-Hook $active
    Write-Host ("  (core.hooksPath={0} - active hook updated)" -f (git config --get core.hooksPath))
} else {
    Write-Host "  tip: this repo normally uses: git config core.hooksPath .githooks"
}

Write-Host "OK - pre-push will block if lab/org.local.json values appear in tracked files."
Write-Host "Manual check: powershell -NoProfile -File tools\scrub-check.ps1"
