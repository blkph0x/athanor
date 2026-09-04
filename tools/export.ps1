# Copy tools/src.list into export/athanor-src (REQ-6.3 scaffolding / DEC-0024).
# Refuses vendor jars. Does not fetch.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not (Test-Path (Join-Path $root "tools\src.list"))) {
    $root = Get-Location
}
$dest = Join-Path $root "export\athanor-src"
if (Test-Path $dest) {
    Remove-Item -Recurse -Force $dest
}
New-Item -ItemType Directory -Path $dest | Out-Null
Get-Content (Join-Path $root "tools\src.list") | ForEach-Object {
    $rel = $_.Trim()
    if ($rel.Length -eq 0 -or $rel.StartsWith("#")) {
        return
    }
    $src = Join-Path $root $rel
    if (-not (Test-Path $src)) {
        throw "missing $rel"
    }
    $out = Join-Path $dest $rel
    $dir = Split-Path -Parent $out
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    Copy-Item $src $out
}
$jars = Get-ChildItem -Path $dest -Filter *.jar -Recurse -ErrorAction SilentlyContinue
if ($jars) {
    throw "export contains jar"
}
Write-Host "export ok $dest"
