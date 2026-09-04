# Local replay of GitHub Actions (DEC-0006).
# Run this before push. The pre-push hook runs `make test` as well.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

Write-Host "=== make info ==="
make info
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== make test ==="
make test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== make lib ==="
make lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== git vs origin (trees must match after push) ==="
git fetch origin 2>$null
git status -sb
git rev-parse HEAD
git rev-parse origin/main 2>$null

Write-Host "LOCAL CI OK"
exit 0
