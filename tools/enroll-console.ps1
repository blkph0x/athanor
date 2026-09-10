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

function Load-DeployDefaults {
    $d = @{
        peer_ipv4   = ""
        peer_port   = "47000"
        peer_ek     = ""
        peer_domain = ""
    }
    function Apply-Json($j, [bool]$onlyGaps) {
        if ($null -eq $j) { return }
        $peer = ""
        if ($j.phone_peer_ipv4) { $peer = [string]$j.phone_peer_ipv4 }
        elseif ($j.hub_lan_ipv4) { $peer = [string]$j.hub_lan_ipv4 }
        if ($peer) {
            if (-not $onlyGaps -or -not $d.peer_ipv4) { $d.peer_ipv4 = $peer }
        }
        if ($j.peer_port) {
            if (-not $onlyGaps -or $d.peer_port -eq "47000") { $d.peer_port = [string]$j.peer_port }
        }
        if ($j.peer_ek) {
            if (-not $onlyGaps -or -not $d.peer_ek) { $d.peer_ek = [string]$j.peer_ek }
        }
        if ($j.domain) {
            if (-not $onlyGaps -or -not $d.peer_domain) { $d.peer_domain = [string]$j.domain }
        }
    }
    $depPath = Join-Path $Root "lab\deploy-state.json"
    $orgPath = Join-Path $Root "lab\org.local.json"
    if (Test-Path $depPath) {
        try { Apply-Json (Get-Content $depPath -Raw | ConvertFrom-Json) $false } catch { }
    }
    if (Test-Path $orgPath) {
        try { Apply-Json (Get-Content $orgPath -Raw | ConvertFrom-Json) $true } catch { }
    }
    return $d
}

function Load-OrgPolicy {
    $path = Join-Path $Root "lab\org-policy.conf"
    $d = @{
        policy_ver         = "1"
        diag               = "1"
        flush_mode         = "log_only"
        wipe_armed         = "0"
        outage_class       = "normal"
        boom_silence_s     = "30"
        password_fail_max  = "5"
        biometric_allowed  = "0"
        password_min_len   = "12"
        usb_data_block     = "1"
        pwd_deny_check     = "1"
    }
    if (-not (Test-Path $path)) { return $d }
    try {
        Get-Content $path | ForEach-Object {
            if ($_ -match '^\s*#' -or $_ -notmatch '=') { return }
            $parts = $_ -split '=', 2
            if ($parts.Count -lt 2) { return }
            $k = $parts[0].Trim(); $v = $parts[1].Trim()
            if ($d.ContainsKey($k)) { $d[$k] = $v }
        }
    } catch { }
    return $d
}

function Opt([string]$cur, [string]$val, [string]$label) {
    $sel = if ($cur -eq $val) { " selected" } else { "" }
    return "<option value=`"$val`"$sel>$label</option>"
}

function Load-Compromise {
    $path = Join-Path $Root "lab\compromise-vote.conf"
    $d = @{
        vote_id       = "0"
        target_label  = ""
        opened_unix   = "0"
        timeout_s     = "300"
        yes           = "0"
        no            = "0"
        quorum        = "1"
        state         = "none"
    }
    if (-not (Test-Path $path)) { return $d }
    try {
        Get-Content $path | ForEach-Object {
            if ($_ -match '^\s*#' -or $_ -notmatch '=') { return }
            $parts = $_ -split '=', 2
            if ($parts.Count -lt 2) { return }
            $k = $parts[0].Trim(); $v = $parts[1].Trim()
            if ($d.ContainsKey($k)) { $d[$k] = $v }
        }
    } catch { }
    return $d
}

function List-EnrolledLabels {
    $dir = Join-Path $Root "lab\enrollments"
    $labels = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path $dir)) { return @() }
    Get-ChildItem -Path $dir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        $rec = Join-Path $_.FullName "enrollment.txt"
        if (-not (Test-Path $rec)) { return }
        Get-Content $rec | ForEach-Object {
            if ($_ -match '^phone_number_label=(.+)$') {
                $lab = $Matches[1].Trim()
                if ($lab -and -not $labels.Contains($lab)) { [void]$labels.Add($lab) }
            }
        }
    }
    return @($labels)
}

function Save-CompromiseFile($c) {
    $dir = Join-Path $Root "lab"
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    $text = @"
vote_id=$($c.vote_id)
target_label=$($c.target_label)
opened_unix=$($c.opened_unix)
timeout_s=$($c.timeout_s)
yes=$($c.yes)
no=$($c.no)
quorum=$($c.quorum)
state=$($c.state)
"@
    [System.IO.File]::WriteAllText((Join-Path $dir "compromise-vote.conf"), ($text.Trim() + "`n"))
}

function Do-Compromise([hashtable]$form) {
    $action = [string]$form["comp_action"]
    $prev = Load-Compromise
    if ($action -eq "start") {
        $target = ([string]$form["target_label"]).Trim()
        $timeout = [string]$form["timeout_s"]; if (-not $timeout) { $timeout = "300" }
        $quorum = [string]$form["quorum"]; if (-not $quorum) { $quorum = "1" }
        if (-not (Test-PhoneLabel $target)) {
            return @{ Ok=$false; Msg="ERR: bad target_label (roster phone)"; Detail="" }
        }
        $toN = 0; $qN = 0
        if (-not [int]::TryParse($timeout, [ref]$toN) -or $toN -lt 30 -or $toN -gt 86400) {
            return @{ Ok=$false; Msg="ERR: timeout_s 30..86400"; Detail="" }
        }
        if (-not [int]::TryParse($quorum, [ref]$qN) -or $qN -lt 1 -or $qN -gt 64) {
            return @{ Ok=$false; Msg="ERR: quorum 1..64"; Detail="" }
        }
        if ($prev.state -eq "open" -or $prev.state -eq "boom_pending") {
            return @{ Ok=$false; Msg="ERR: vote already open - clear or wait for boom"; Detail="" }
        }
        $vid = 1
        try { $vid = [int]$prev.vote_id + 1 } catch { $vid = 1 }
        if ($vid -lt 1) { $vid = 1 }
        $opened = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $c = @{
            vote_id = "$vid"
            target_label = $target
            opened_unix = "$opened"
            timeout_s = "$toN"
            yes = "0"
            no = "0"
            quorum = "$qN"
            state = "open"
        }
        Save-CompromiseFile $c
        return @{ Ok=$true; Msg=("OK: compromise vote {0} OPEN for {1} (timeout {2}s quorum {3}). Hub pushes boom on YES quorum or timeout." -f $vid, $target, $toN, $qN); Detail=(Get-Content (Join-Path $Root "lab\compromise-vote.conf") -Raw) }
    }
    if ($action -eq "yes" -or $action -eq "no") {
        if ($prev.state -ne "open") {
            return @{ Ok=$false; Msg="ERR: no open vote"; Detail="" }
        }
        $yes = 0; $no = 0
        try { $yes = [int]$prev.yes } catch { $yes = 0 }
        try { $no = [int]$prev.no } catch { $no = 0 }
        if ($action -eq "yes") { $yes++ } else { $no++ }
        $prev.yes = "$yes"
        $prev.no = "$no"
        Save-CompromiseFile $prev
        return @{ Ok=$true; Msg=("OK: voted {0} (yes={1} no={2} quorum={3})" -f $action.ToUpper(), $yes, $no, $prev.quorum); Detail=(Get-Content (Join-Path $Root "lab\compromise-vote.conf") -Raw) }
    }
    if ($action -eq "clear") {
        $prev.state = "cleared"
        Save-CompromiseFile $prev
        return @{ Ok=$true; Msg="OK: vote cleared (hub notifies phones)"; Detail=(Get-Content (Join-Path $Root "lab\compromise-vote.conf") -Raw) }
    }
    return @{ Ok=$false; Msg="ERR: unknown comp_action"; Detail="" }
}

function Save-OrgPolicy([hashtable]$form) {
    $diag = [string]$form["diag"]
    $flush = [string]$form["flush_mode"]
    $wipe = [string]$form["wipe_armed"]
    if (-not $wipe) { $wipe = "0" }
    $outage = [string]$form["outage_class"]
    $boom = [string]$form["boom_silence_s"]; if (-not $boom) { $boom = "30" }
    $failK = [string]$form["password_fail_max"]; if (-not $failK) { $failK = "5" }
    $bio = [string]$form["biometric_allowed"]; if (-not $bio) { $bio = "0" }
    $minLen = [string]$form["password_min_len"]; if (-not $minLen) { $minLen = "12" }
    $usb = [string]$form["usb_data_block"]; if (-not $usb) { $usb = "1" }
    $deny = [string]$form["pwd_deny_check"]; if (-not $deny) { $deny = "1" }
    if ($diag -notin @("0", "1")) { return @{ Ok=$false; Msg="ERR: diag"; Detail="" } }
    if ($flush -notin @("log_only", "zeroize")) { return @{ Ok=$false; Msg="ERR: flush_mode"; Detail="" } }
    if ($wipe -notin @("0", "1")) { return @{ Ok=$false; Msg="ERR: wipe_armed"; Detail="" } }
    if ($outage -notin @("normal", "maintenance", "blackout", "faraday", "capture")) {
        return @{ Ok=$false; Msg="ERR: outage_class"; Detail="" }
    }
    if ($flush -eq "log_only" -and $diag -ne "1") {
        return @{ Ok=$false; Msg="ERR: log_only requires diag=1"; Detail="" }
    }
    if ($bio -notin @("0", "1") -or $usb -notin @("0", "1") -or $deny -notin @("0", "1")) {
        return @{ Ok=$false; Msg="ERR: biometric/usb/deny bit"; Detail="" }
    }
    $boomN = 0; $failN = 0; $minN = 0
    if (-not [int]::TryParse($boom, [ref]$boomN) -or $boomN -lt 5 -or $boomN -gt 86400) {
        return @{ Ok=$false; Msg="ERR: boom_silence_s 5..86400"; Detail="" }
    }
    if (-not [int]::TryParse($failK, [ref]$failN) -or $failN -lt 1 -or $failN -gt 20) {
        return @{ Ok=$false; Msg="ERR: password_fail_max 1..20"; Detail="" }
    }
    if (-not [int]::TryParse($minLen, [ref]$minN) -or $minN -lt 8 -or $minN -gt 64) {
        return @{ Ok=$false; Msg="ERR: password_min_len 8..64"; Detail="" }
    }
    $prev = Load-OrgPolicy
    $ver = 1
    try { $ver = [int]$prev.policy_ver + 1 } catch { $ver = 1 }
    if ($ver -lt 1) { $ver = 1 }
    $dir = Join-Path $Root "lab"
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    $text = @"
policy_ver=$ver
diag=$diag
flush_mode=$flush
wipe_armed=$wipe
outage_class=$outage
boom_silence_s=$boomN
password_fail_max=$failN
biometric_allowed=$bio
password_min_len=$minN
usb_data_block=$usb
pwd_deny_check=$deny
"@
    [System.IO.File]::WriteAllText((Join-Path $dir "org-policy.conf"), ($text.Trim() + "`n"))
    return @{ Ok=$true; Msg=("OK: network policy ver={0} saved (DEC-0045/0046) - phones pick up from hub." -f $ver); Detail=$text }
}

function Load-UpdateAnnounce {
    $path = Join-Path $Root "lab\updates\announce.conf"
    $d = @{
        update_id    = "0"
        kind         = "apk"
        version      = ""
        sha256       = ""
        size         = "0"
        chunk_size   = "900"
        payload_path = "lab/updates/payload.bin"
    }
    if (-not (Test-Path $path)) { return $d }
    try {
        Get-Content $path | ForEach-Object {
            if ($_ -match '^\s*#' -or $_ -notmatch '=') { return }
            $parts = $_ -split '=', 2
            if ($parts.Count -lt 2) { return }
            $k = $parts[0].Trim(); $v = $parts[1].Trim()
            if ($d.ContainsKey($k)) { $d[$k] = $v }
        }
    } catch { }
    return $d
}

function Publish-Update([hashtable]$form) {
    $kind = [string]$form["kind"]; if (-not $kind) { $kind = "apk" }
    $version = ([string]$form["version"]).Trim()
    $src = ([string]$form["source_path"]).Trim()
    if (-not $src) { $src = "android/athanor-lab.apk" }
    if ($kind -notin @("apk", "site", "hub")) {
        return @{ Ok=$false; Msg="ERR: kind apk|site|hub"; Detail="" }
    }
    if (-not $version -or $version.Length -ge 64) {
        return @{ Ok=$false; Msg="ERR: version required (<64 chars)"; Detail="" }
    }
    $srcFull = if ([IO.Path]::IsPathRooted($src)) { $src } else { Join-Path $Root $src }
    if (-not (Test-Path -LiteralPath $srcFull)) {
        return @{ Ok=$false; Msg=("ERR: source missing: {0}" -f $src); Detail="" }
    }
    $updDir = Join-Path $Root "lab\updates"
    if (-not (Test-Path $updDir)) { New-Item -ItemType Directory -Path $updDir | Out-Null }
    $dest = Join-Path $updDir "payload.bin"
    Copy-Item -LiteralPath $srcFull -Destination $dest -Force
    $hash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $dest).Length
    if ($size -le 0 -or $size -gt [uint32]::MaxValue) {
        return @{ Ok=$false; Msg="ERR: bad payload size"; Detail="" }
    }
    $prev = Load-UpdateAnnounce
    $uid = 1
    try { $uid = [int]$prev.update_id + 1 } catch { $uid = 1 }
    if ($uid -lt 1) { $uid = 1 }
    $text = @"
update_id=$uid
kind=$kind
version=$version
sha256=$hash
size=$size
chunk_size=900
payload_path=lab/updates/payload.bin
"@
    [System.IO.File]::WriteAllText((Join-Path $updDir "announce.conf"), ($text.Trim() + "`n"))
    return @{ Ok=$true; Msg=("OK: published update_id={0} kind={1} size={2} (DEC-0048). Hub streams over encrypted tunnel DATA only - no HTTP download. Keep atnnode listen running." -f $uid, $kind, $size); Detail=$text }
}

function Hub-Peers-Path {
    return (Join-Path $Root "lab\hub-peers.conf")
}

function Truncate-Ek([string]$ek) {
    if ([string]::IsNullOrEmpty($ek)) { return "" }
    if ($ek.Length -le 28) { return $ek }
    return ($ek.Substring(0, 16) + "..." + $ek.Substring($ek.Length - 8))
}

function Load-HubJoinCard {
    $d = @{ peer_port = ""; peer_ek = ""; ek_preview = ""; ready = $false }
    $log = Join-Path $Root "lab\hub-listen.log"
    if (-not (Test-Path $log)) { return $d }
    try {
        Get-Content $log -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_ -match '^peer_port=(\d{1,5})\s*$') { $d.peer_port = $Matches[1] }
            elseif ($_ -match '^peer_ek=([0-9a-fA-F]+)\s*$') {
                $d.peer_ek = $Matches[1]
                $d.ek_preview = Truncate-Ek $Matches[1]
            }
        }
    } catch { }
    if ($d.peer_port -and $d.peer_ek.Length -eq 3136) { $d.ready = $true }
    return $d
}

function Load-HubPeers {
    $list = New-Object System.Collections.Generic.List[object]
    $path = Hub-Peers-Path
    if (-not (Test-Path $path)) { return @($list) }
    try {
        Get-Content $path -ErrorAction SilentlyContinue | ForEach-Object {
            $line = $_.Trim()
            if ($line -eq "" -or $line.StartsWith("#")) { return }
            $parts = $line -split '\s+', 3
            if ($parts.Count -lt 3) { return }
            $ip = $parts[0].Trim()
            $port = $parts[1].Trim()
            $ek = ($parts[2].Trim() -replace '\s', '')
            if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { return }
            if ($port -notmatch '^\d{1,5}$') { return }
            $list.Add([pscustomobject]@{
                ipv4 = $ip
                port = $port
                ek   = $ek
                preview = (Truncate-Ek $ek)
                key  = ("{0}:{1}" -f $ip, $port)
            }) | Out-Null
        }
    } catch { }
    return @($list)
}

function Save-HubPeersFile([object[]]$peers) {
    $path = Hub-Peers-Path
    $dir = Split-Path $path -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# DEC-0052 / DEC-0048 hub peers (gitignored). Line: ipv4 port ek_hex") | Out-Null
    $lines.Add("# ML-KEM-1024 peer_ek = 3136 hex. Tunnel-only fan-out; no HTTP.") | Out-Null
    foreach ($p in @($peers)) {
        if ($null -eq $p) { continue }
        $lines.Add(("{0} {1} {2}" -f $p.ipv4, $p.port, $p.ek)) | Out-Null
    }
    [System.IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"))
}

function Do-Peers([hashtable]$form) {
    $action = ([string]$form["peers_action"]).Trim().ToLowerInvariant()
    if ($action -eq "") { $action = "add" }
    $peers = @(Load-HubPeers)
    if ($action -eq "remove") {
        $ip = ([string]$form["peer_ipv4"]).Trim()
        $port = ([string]$form["peer_port"]).Trim()
        if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') {
            return @{ Ok=$false; Msg="ERR: bad peer_ipv4"; Detail="" }
        }
        if ($port -notmatch '^\d{1,5}$' -or [int]$port -lt 1 -or [int]$port -gt 65535) {
            return @{ Ok=$false; Msg="ERR: bad peer_port"; Detail="" }
        }
        $key = "{0}:{1}" -f $ip, $port
        $kept = @($peers | Where-Object { $_.key -ne $key })
        if ($kept.Count -eq $peers.Count) {
            return @{ Ok=$false; Msg=("ERR: peer not found {0}" -f $key); Detail="" }
        }
        Save-HubPeersFile $kept
        return @{ Ok=$true; Msg=("OK: removed peer hub {0} (DEC-0052)" -f $key); Detail=("peers={0}" -f $kept.Count) }
    }
    if ($action -ne "add") {
        return @{ Ok=$false; Msg="ERR: peers_action add|remove"; Detail="" }
    }
    $ip = ([string]$form["peer_ipv4"]).Trim()
    $port = ([string]$form["peer_port"]).Trim()
    $ek = ([string]$form["peer_ek"]).Trim() -replace '\s', ''
    if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') {
        return @{ Ok=$false; Msg="ERR: bad peer_ipv4"; Detail="" }
    }
    if ($port -notmatch '^\d{1,5}$' -or [int]$port -lt 1 -or [int]$port -gt 65535) {
        return @{ Ok=$false; Msg="ERR: bad peer_port"; Detail="" }
    }
    if ($ek.Length -ne 3136 -or $ek -notmatch '^[0-9a-fA-F]+$') {
        return @{ Ok=$false; Msg="ERR: peer_ek must be 3136 hex chars (ML-KEM-1024)"; Detail="" }
    }
    $key = "{0}:{1}" -f $ip, $port
    $others = @($peers | Where-Object { $_.key -ne $key })
    $entry = [pscustomobject]@{
        ipv4 = $ip
        port = $port
        ek   = $ek.ToLowerInvariant()
        preview = (Truncate-Ek $ek)
        key  = $key
    }
    $all = @($others + $entry)
    Save-HubPeersFile $all
    $prev = Truncate-Ek $ek
    return @{
        Ok = $true
        Msg = ("OK: peer hub {0} saved (ek {1}). Publish an update to fan-out over the tunnel (DEC-0052/0048)." -f $key, $prev)
        Detail = ("peers={0}" -f $all.Count)
    }
}

function Html-Peers-Section {
    $card = Load-HubJoinCard
    $peers = @(Load-HubPeers)
    $joinHtml = ""
    if ($card.ready) {
        $joinHtml = @"
<p class="meta"><strong>This hub join card</strong> (share out-of-band; never commit):
port=<code>$(Html-Encode $card.peer_port)</code>
ek=<code>$(Html-Encode $card.ek_preview)</code></p>
<details class="meta"><summary>Full peer_ek (copy for peer admin)</summary>
<textarea readonly rows="4">$(Html-Encode $card.peer_ek)</textarea>
<label>peer_port</label>
<input readonly value="$(Html-Encode $card.peer_port)"/>
</details>
"@
    } else {
        $joinHtml = '<p class="meta"><strong>This hub join card:</strong> start <code>atnnode listen</code> (or hub-watchdog) so <code>lab/hub-listen.log</code> has peer_port + peer_ek.</p>'
    }
    $listHtml = ""
    if ($peers.Count -eq 0) {
        $listHtml = '<p class="meta">No peer hubs yet.</p>'
    } else {
        $listHtml = '<ul class="meta">'
        foreach ($p in $peers) {
            $listHtml += ('<li><code>{0}</code> ek=<code>{1}</code>' -f (Html-Encode $p.key), (Html-Encode $p.preview))
            $listHtml += ('<form method="POST" action="/peers" style="display:inline;margin-left:0.5rem">' +
                '<input type="hidden" name="peers_action" value="remove"/>' +
                '<input type="hidden" name="peer_ipv4" value="{0}"/>' +
                '<input type="hidden" name="peer_port" value="{1}"/>' +
                '<button type="submit" style="width:auto;padding:0.25rem 0.75rem;margin:0;font-size:0.85rem">Remove</button></form></li>') -f `
                (Html-Encode $p.ipv4), (Html-Encode $p.port)
        }
        $listHtml += '</ul>'
    }
    return @"
<h2>Peer hubs (join network)</h2>
<p class="meta">DEC-0052. Paste another hub's public IPv4, listen port, and ML-KEM-1024
<code>peer_ek</code> (from its join card). Stored in gitignored
<code>lab/hub-peers.conf</code>. Security: OOB identity + DEC-0048 tunnel fan-out only.
Single-session listen still applies (honest limit).</p>
$joinHtml
$listHtml
<form method="POST" action="/peers" id="peersForm">
<input type="hidden" name="peers_action" value="add"/>
<label>Peer hub IPv4</label>
<input name="peer_ipv4" required placeholder="dotted IPv4"/>
<label>Peer hub port</label>
<input name="peer_port" value="47000" required/>
<label>Peer hub peer_ek (3136 hex)</label>
<textarea name="peer_ek" rows="4" required placeholder="paste peer_ek hex"></textarea>
<button type="submit">Add peer hub</button>
</form>
"@
}

function Page-Html([string]$flash, [string]$detail) {
    $dev = Find-AdbDevice
    $devLine = if ($dev) { "USB device: $dev (ready)" } else { "USB device: none (plug in with USB debugging)" }
    $apk = Test-Path "android\athanor-lab.apk"
    $apkLine = if ($apk) { "APK: android/athanor-lab.apk OK" } else { "APK: missing - run make android-apk" }
    $defs = Load-DeployDefaults
    $pol = Load-OrgPolicy
    $comp = Load-Compromise
    $upd = Load-UpdateAnnounce
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
    $peerDomain = Html-Encode $defs.peer_domain
    $peerIpv4 = Html-Encode $defs.peer_ipv4
    $peerPort = Html-Encode $defs.peer_port
    $peerEk = Html-Encode $defs.peer_ek
    $html = @"
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Athanor admin</title>
<style>
body{font-family:Georgia,serif;max-width:42rem;margin:2rem auto;padding:0 1rem;background:#f7f4ef;color:#1a1a1a}
h1{font-size:1.75rem;margin-bottom:0.25rem}
h2{font-size:1.2rem;margin-top:2rem;border-top:1px solid #ccc;padding-top:1rem}
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
<h1>Athanor admin</h1>
<p class="sub">Loopback only (DEC-0042 / DEC-0045). Network policy pushes to connected
Android nodes via the hub (no USB re-enroll). Phone number is a roster label - never SMS.
Hub fields pre-fill from <code>lab/deploy-state.json</code> (gaps from <code>lab/org.local.json</code>).</p>
<div class="meta" id="status">
<span class="live" id="devLine">__DEVLINE__</span><br/>
<span id="apkLine">__APKLINE__</span><br/>
Bind: __BIND__
</div>
__FLASH__
__DETAIL__
<h2>Publish mesh update</h2>
<p class="meta">DEC-0048. Copies a local hub file to <code>lab/updates/payload.bin</code>,
writes announce.conf (id=__UPD_ID__ kind=__UPD_KIND__ size=__UPD_SIZE__).
Hub pushes announce+chunks over the <strong>encrypted tunnel only</strong> (DATA
<code>U</code> / <code>UC</code>) to the ESTABLISHED phone and peer hubs in
<code>lab/hub-peers.conf</code>. No HTTP/URL download path.</p>
<form method="POST" action="/update" id="updateForm">
<label>kind</label>
<select name="kind">__KIND_OPTS__</select>
<label>version</label>
<input name="version" value="__UPD_VER__" required placeholder="1.0.0"/>
<label>source_path (local on this hub)</label>
<input name="source_path" value="android/athanor-lab.apk" required/>
<button type="submit">Publish update</button>
</form>
__PEERS_SECTION__
<h2>Network-wide policy</h2>
<p class="meta">Writes <code>lab/org-policy.conf</code> (ver __POLVER__). Keep
<code>atnnode listen</code> running so ESTABLISHED phones receive pushes.</p>
<form method="POST" action="/policy" id="policyForm">
<label>diag</label>
<select name="diag">__DIAG_OPTS__</select>
<label>flush_mode</label>
<select name="flush_mode">__FLUSH_OPTS__</select>
<label>wipe_armed</label>
<select name="wipe_armed">__WIPE_OPTS__</select>
<label>outage_class</label>
<select name="outage_class">__OUTAGE_OPTS__</select>
<label>boom_silence_s (hub silence -> BOOM)</label>
<input name="boom_silence_s" value="__BOOM__" required/>
<label>password_fail_max (unlock fails -> BOOM/flush)</label>
<input name="password_fail_max" value="__FAILK__" required/>
<label>biometric_allowed (0=fingerprint+face/iris OFF)</label>
<select name="biometric_allowed">__BIO_OPTS__</select>
<label>password_min_len (alphanumeric only)</label>
<input name="password_min_len" value="__MINLEN__" required/>
<label>usb_data_block (1=block MTP/adb data when Knox jar present)</label>
<select name="usb_data_block">__USB_OPTS__</select>
<label>pwd_deny_check (1=reject rockyou/common leak hashes)</label>
<select name="pwd_deny_check">__DENY_OPTS__</select>
<button type="submit">Save network policy</button>
</form>
<h2>Compromise vote (stolen phone)</h2>
<p class="meta">DEC-0047. Marks an enrolled roster label suspected compromised.
Quorum YES or timeout -> hub sends boom (flush keys; Knox wipe when jar present).
Keep <code>atnnode listen</code> running. State: __COMP_STATE__ vote_id=__COMP_VID__
yes=__COMP_YES__ no=__COMP_NO__ quorum=__COMP_QUORUM__ target=__COMP_TARGET__</p>
<form method="POST" action="/compromise" id="compStartForm">
<input type="hidden" name="comp_action" value="start"/>
<label>target_label (enrolled phone)</label>
<select name="target_label">__COMP_LABEL_OPTS__</select>
<label>timeout_s (fail-closed boom if votes incomplete)</label>
<input name="timeout_s" value="__COMP_TIMEOUT__" required/>
<label>quorum (YES votes needed)</label>
<input name="quorum" value="__COMP_QUORUM_IN__" required/>
<button type="submit">Start compromise vote</button>
</form>
<form method="POST" action="/compromise" style="margin-top:0.75rem">
<input type="hidden" name="comp_action" value="yes"/>
<button type="submit">Vote YES (compromised -> boom)</button>
</form>
<form method="POST" action="/compromise">
<input type="hidden" name="comp_action" value="no"/>
<button type="submit">Vote NO (not compromised)</button>
</form>
<form method="POST" action="/compromise">
<input type="hidden" name="comp_action" value="clear"/>
<button type="submit">Clear vote</button>
</form>
<h2>USB enroll (bootstrap)</h2>
<form method="POST" action="/enroll" id="enrollForm">
<label>Phone number (roster label)</label>
<input name="phone_number" required placeholder="+61..." pattern="\+?[0-9][0-9 \-]{5,30}[0-9]"/>
<label>Hub domain (optional; resolves to peer_ipv4)</label>
<input name="peer_domain" value="$peerDomain" placeholder="mesh.example.org"/>
<label>Hub peer_ipv4 (dotted; leave blank to use domain)</label>
<input name="peer_ipv4" value="$peerIpv4" placeholder="hub or public IPv4"/>
<label>Hub peer_port</label>
<input name="peer_port" value="$peerPort" required/>
<label>Hub peer_ek (hex from atnnode listen)</label>
<textarea name="peer_ek" rows="4" required placeholder="paste peer_ek hex">$peerEk</textarea>
<label>diag (seed conf; live hub policy overrides)</label>
<select name="diag">__DIAG_OPTS__</select>
<label>flush_mode</label>
<select name="flush_mode">__FLUSH_OPTS__</select>
<label>outage_class</label>
<select name="outage_class">__OUTAGE_OPTS__</select>
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
"@
    $bind = ("http" + "://" + "127.0.0.1:$Port/")
    $html = $html.Replace('__DEVLINE__', (Html-Encode $devLine))
    $html = $html.Replace('__APKLINE__', (Html-Encode $apkLine))
    $html = $html.Replace('__BIND__', (Html-Encode $bind))
    $html = $html.Replace('__FLASH__', $flashHtml)
    $html = $html.Replace('__DETAIL__', $detailHtml)
    $html = $html.Replace('__POLVER__', (Html-Encode $pol.policy_ver))
    $html = $html.Replace('__UPD_ID__', (Html-Encode $upd.update_id))
    $html = $html.Replace('__UPD_KIND__', (Html-Encode $upd.kind))
    $html = $html.Replace('__UPD_SIZE__', (Html-Encode $upd.size))
    $html = $html.Replace('__UPD_VER__', (Html-Encode $upd.version))
    $html = $html.Replace('__KIND_OPTS__', ((Opt $upd.kind "apk" "apk") + (Opt $upd.kind "site" "site") + (Opt $upd.kind "hub" "hub")))
    $html = $html.Replace('__PEERS_SECTION__', (Html-Peers-Section))
    $diagOpts = (Opt $pol.diag "1" "1 (lab)") + (Opt $pol.diag "0" "0 (prod)")
    $flushOpts = (Opt $pol.flush_mode "log_only" "log_only") + (Opt $pol.flush_mode "zeroize" "zeroize")
    $wipeOpts = (Opt $pol.wipe_armed "0" "0") + (Opt $pol.wipe_armed "1" "1")
    $outageOpts = (Opt $pol.outage_class "normal" "normal") +
        (Opt $pol.outage_class "maintenance" "maintenance") +
        (Opt $pol.outage_class "blackout" "blackout") +
        (Opt $pol.outage_class "faraday" "faraday") +
        (Opt $pol.outage_class "capture" "capture")
    $html = $html.Replace('__DIAG_OPTS__', $diagOpts)
    $html = $html.Replace('__FLUSH_OPTS__', $flushOpts)
    $html = $html.Replace('__WIPE_OPTS__', $wipeOpts)
    $html = $html.Replace('__OUTAGE_OPTS__', $outageOpts)
    $html = $html.Replace('__BOOM__', (Html-Encode $pol.boom_silence_s))
    $html = $html.Replace('__FAILK__', (Html-Encode $pol.password_fail_max))
    $html = $html.Replace('__MINLEN__', (Html-Encode $pol.password_min_len))
    $html = $html.Replace('__BIO_OPTS__', ((Opt $pol.biometric_allowed "0" "0 (OFF)") + (Opt $pol.biometric_allowed "1" "1 (allow)")))
    $html = $html.Replace('__USB_OPTS__', ((Opt $pol.usb_data_block "1" "1 (block data)") + (Opt $pol.usb_data_block "0" "0")))
    $html = $html.Replace('__DENY_OPTS__', ((Opt $pol.pwd_deny_check "1" "1 (check deny list)") + (Opt $pol.pwd_deny_check "0" "0")))
    $html = $html.Replace('__COMP_STATE__', (Html-Encode $comp.state))
    $html = $html.Replace('__COMP_VID__', (Html-Encode $comp.vote_id))
    $html = $html.Replace('__COMP_YES__', (Html-Encode $comp.yes))
    $html = $html.Replace('__COMP_NO__', (Html-Encode $comp.no))
    $html = $html.Replace('__COMP_QUORUM__', (Html-Encode $comp.quorum))
    $html = $html.Replace('__COMP_TARGET__', (Html-Encode $comp.target_label))
    $html = $html.Replace('__COMP_TIMEOUT__', (Html-Encode $comp.timeout_s))
    $html = $html.Replace('__COMP_QUORUM_IN__', (Html-Encode $comp.quorum))
    $labels = @(List-EnrolledLabels)
    if ($labels.Count -eq 0) {
        $html = $html.Replace('<select name="target_label">__COMP_LABEL_OPTS__</select>',
            '<input name="target_label" required placeholder="+61..." pattern="\+?[0-9][0-9 \-]{5,30}[0-9]"/>')
    } else {
        $labelOpts = ""
        foreach ($lab in $labels) {
            $sel = if ($lab -eq $comp.target_label) { " selected" } else { "" }
            $labelOpts += ("<option value=`"{0}`"{1}>{0}</option>" -f (Html-Encode $lab), $sel)
        }
        $html = $html.Replace('__COMP_LABEL_OPTS__', $labelOpts)
    }
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
Write-Host "ATN admin (DEC-0042/0045/0047/0048/0052) at $prefix"
Write-Host "Policy + update + peer hubs + compromise + USB enroll. Ctrl+C to stop."

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
        if ($req.HttpMethod -eq "POST" -and $path -eq "/policy") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Save-OrgPolicy $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($req.HttpMethod -eq "POST" -and $path -eq "/update") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Publish-Update $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($req.HttpMethod -eq "POST" -and $path -eq "/compromise") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Do-Compromise $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($req.HttpMethod -eq "POST" -and $path -eq "/peers") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Do-Peers $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($req.HttpMethod -eq "POST" -and $path -eq "/enroll") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Do-Enroll $form
            $flash = $result.Msg
            if ($result.Detail) { $detail = $result.Detail }
        } elseif ($path -ne "/" -and $path -ne "/enroll" -and $path -ne "/policy" -and $path -ne "/compromise" -and $path -ne "/update" -and $path -ne "/peers") {
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
