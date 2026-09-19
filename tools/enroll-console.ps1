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
        policy_ver              = "1"
        diag                    = "1"
        flush_mode              = "log_only"
        wipe_armed              = "0"
        outage_class            = "normal"
        boom_silence_s          = "30"
        password_fail_max       = "5"
        biometric_allowed       = "0"
        password_min_len        = "12"
        usb_data_block          = "1"
        pwd_deny_check          = "1"
        require_adb_off         = "0"
        require_usb_charge_only = "0"
        enroll_block_on_usb     = "0"
        boom_on_usb_breach      = "0"
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

function Load-AdminRole {
    $path = Join-Path $Root "lab\admin-role.conf"
    $d = @{ role = "primary"; hub_id = "lab-hub-1" }
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

function Admin-IsPrimary {
    $r = Load-AdminRole
    return ($r.role -eq "primary")
}

function Save-MeshOutbox([hashtable]$form) {
    $to = ([string]$form["msg_to"]).Trim()
    $body = ([string]$form["msg_body"]).Trim()
    $from = ([string]$form["msg_from"]).Trim()
    if (-not $from) { $from = "hub" }
    if (-not $to) { return @{ Ok=$false; Msg="ERR: msg_to required"; Detail="" } }
    if (-not $body) { return @{ Ok=$false; Msg="ERR: msg_body required"; Detail="" } }
    if ($to.Length -ge 64 -or $from.Length -ge 64 -or $body.Length -ge 800) {
        return @{ Ok=$false; Msg="ERR: field too long"; Detail="" }
    }
    if ($body -match '\|' -or $to -match '\|' -or $from -match '\|') {
        return @{ Ok=$false; Msg="ERR: pipe char not allowed"; Detail="" }
    }
    $dir = Join-Path $Root "lab"
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    $line = "TEXT|$from|$to|$body"
    $path = Join-Path $dir "mesh-outbox.txt"
    [System.IO.File]::AppendAllText($path, ($line + "`n"))
    return @{ Ok=$true; Msg=("OK: queued mesh TEXT to {0} (hub drains when ESTABLISHED)" -f $to); Detail=$line }
}

function Html-Messages-Section {
    $labels = @(List-EnrolledLabels)
    $peers = @()
    try { $peers = @(Load-HubPeers) } catch { $peers = @() }
    $opts = '<option value="*">* (any / broadcast)</option>'
    $opts += '<option value="phone">phone</option>'
    foreach ($lab in $labels) {
        $opts += ('<option value="{0}">node: {0}</option>' -f (Html-Encode $lab))
    }
    foreach ($p in $peers) {
        $key = if ($p.key) { $p.key } else { "$($p.ipv4):$($p.port)" }
        $opts += ('<option value="{0}">hub: {0}</option>' -f (Html-Encode $key))
    }
    $inboxPath = Join-Path $Root "lab\mesh-inbox.txt"
    $inbox = "(empty — messages appear when tunnel ESTABLISHED peers chat)"
    if (Test-Path $inboxPath) {
        try {
            $raw = Get-Content $inboxPath -Raw -ErrorAction SilentlyContinue
            if ($raw -and $raw.Trim().Length -gt 0) {
                $tail = ($raw -split "`n" | Select-Object -Last 40) -join "`n"
                $inbox = $tail
            }
        } catch { }
    }
    $roster = '<ul class="meta">'
    foreach ($lab in $labels) {
        $roster += ('<li><code>[node]</code> {0}</li>' -f (Html-Encode $lab))
    }
    foreach ($p in $peers) {
        $key = if ($p.key) { $p.key } else { "$($p.ipv4):$($p.port)" }
        $roster += ('<li><code>[hub]</code> {0}</li>' -f (Html-Encode $key))
    }
    if ($labels.Count -eq 0 -and $peers.Count -eq 0) {
        $roster += '<li class="meta">No contacts yet — enroll a phone or add a peer hub.</li>'
    }
    $roster += '</ul>'
    return @"
<h2>Messages</h2>
<p class="meta">DEC-0057. Same PQ/AEAD mesh floor as phone Messages. Hub and nodes
exchange text (and file share on phone). Queues to <code>lab/mesh-outbox.txt</code>;
<code>atnnode listen</code> drains on ESTABLISHED. Nodes never author org policy.</p>
<h3>Contacts</h3>
$roster
<form method="POST" action="/mesh" id="meshForm">
<label>from</label>
<input name="msg_from" value="hub" required/>
<label>to</label>
<select name="msg_to">$opts</select>
<label>message</label>
<input name="msg_body" required placeholder="hello mesh" maxlength="700"/>
<button class="act" type="submit">Send over mesh</button>
</form>
<h3>Inbox (recent)</h3>
<pre class="meta"><code>$(Html-Encode $inbox)</code></pre>
"@
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
    if (-not (Admin-IsPrimary)) {
        $role = Load-AdminRole
        return @{ Ok=$false; Msg=("ERR: this hub is role={0} (hub_id={1}). Only the primary admin hub may author org policy. Secondaries adopt higher policy_ver over tunnel." -f $role.role, $role.hub_id); Detail="" }
    }
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
    $adbOff = [string]$form["require_adb_off"]; if (-not $adbOff) { $adbOff = "0" }
    $chgOnly = [string]$form["require_usb_charge_only"]; if (-not $chgOnly) { $chgOnly = "0" }
    $enrollUsb = [string]$form["enroll_block_on_usb"]; if (-not $enrollUsb) { $enrollUsb = "0" }
    $boomUsb = [string]$form["boom_on_usb_breach"]; if (-not $boomUsb) { $boomUsb = "0" }
    if ($diag -notin @("0", "1")) { return @{ Ok=$false; Msg="ERR: diag"; Detail="" } }
    if ($flush -notin @("log_only", "zeroize")) { return @{ Ok=$false; Msg="ERR: flush_mode"; Detail="" } }
    if ($wipe -notin @("0", "1")) { return @{ Ok=$false; Msg="ERR: wipe_armed"; Detail="" } }
    if ($outage -notin @("normal", "maintenance", "blackout", "faraday", "capture")) {
        return @{ Ok=$false; Msg="ERR: outage_class"; Detail="" }
    }
    if ($flush -eq "log_only" -and $diag -ne "1") {
        return @{ Ok=$false; Msg="ERR: log_only requires diag=1"; Detail="" }
    }
    foreach ($bit in @($bio, $usb, $deny, $adbOff, $chgOnly, $enrollUsb, $boomUsb)) {
        if ($bit -notin @("0", "1")) {
            return @{ Ok=$false; Msg="ERR: posture bit must be 0|1"; Detail="" }
        }
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
require_adb_off=$adbOff
require_usb_charge_only=$chgOnly
enroll_block_on_usb=$enrollUsb
boom_on_usb_breach=$boomUsb
"@
    [System.IO.File]::WriteAllText((Join-Path $dir "org-policy.conf"), ($text.Trim() + "`n"))
    return @{ Ok=$true; Msg=("OK: network policy ver={0} saved (DEC-0056) - live nodes + peer hubs update immediately; offline catch up on rejoin." -f $ver); Detail=$text }
}

function List-DeviceRoster {
    $dir = Join-Path $Root "lab\enrollments"
    $rows = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path $dir)) { return "" }
    Get-ChildItem -Path $dir -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 40 |
        ForEach-Object {
            $rec = Join-Path $_.FullName "enrollment.txt"
            if (-not (Test-Path $rec)) { return }
            $label = ""; $serial = ""; $when = ""; $ipv4 = ""
            Get-Content $rec | ForEach-Object {
                if ($_ -match '^phone_number_label=(.+)$') { $label = $Matches[1].Trim() }
                elseif ($_ -match '^serial=(.+)$') { $serial = $Matches[1].Trim() }
                elseif ($_ -match '^time_utc=(.+)$') { $when = $Matches[1].Trim() }
                elseif ($_ -match '^peer_ipv4=(.+)$') { $ipv4 = $Matches[1].Trim() }
            }
            if (-not $label) { $label = $_.Name }
            $rows.Add(("<tr><td>{0}</td><td><code>{1}</code></td><td>{2}</td><td>{3}</td><td><code>{4}</code></td></tr>" -f
                (Html-Encode $label), (Html-Encode $_.Name), (Html-Encode $when),
                (Html-Encode $ipv4), (Html-Encode $serial)))
        }
    if ($rows.Count -eq 0) { return "<p class='meta'>No enrollments yet.</p>" }
    return "<table class='roster'><thead><tr><th>Label</th><th>Id</th><th>UTC</th><th>Hub</th><th>Serial</th></tr></thead><tbody>" +
        ($rows -join "") + "</tbody></table>"
}

function Test-EnrollUsbGate([string]$serial) {
    $pol = Load-OrgPolicy
    if ($pol.wipe_armed -ne "1" -or $pol.enroll_block_on_usb -ne "1") {
        return @{ Ok=$true; Msg="" }
    }
    # Kill-mode enroll gate (DEC-0056). Lab bootstrap: leave wipe_armed=0.
    if ($pol.require_adb_off -eq "1") {
        return @{ Ok=$false; Msg="ERR: kill policy require_adb_off=1 blocks USB enroll (adb is required for Connect & Enroll). Arm USB gates after first join, or set require_adb_off=0 / enroll_block_on_usb=0." }
    }
    if ($pol.require_usb_charge_only -eq "1") {
        # Best-effort: if ADB is up, USB data path is active - fail closed when kill armed.
        return @{ Ok=$false; Msg="ERR: kill policy require_usb_charge_only=1 blocks USB enroll while debugging is active. Bootstrap with flags off, then arm after mesh join." }
    }
    return @{ Ok=$true; Msg="" }
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
    if (-not (Test-Path $path)) { return @() }
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
    if ($list.Count -eq 0) { return @() }
    return @($list.ToArray())
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
:root{--bg:#0e1116;--panel:#161b22;--ink:#e6edf3;--muted:#8b949e;--line:#30363d;--accent:#3fb950;--warn:#d29922;--danger:#f85149;--focus:#58a6ff}
*{box-sizing:border-box}
body{font-family:"Segoe UI",system-ui,sans-serif;margin:0;background:var(--bg);color:var(--ink);line-height:1.45}
.wrap{max-width:52rem;margin:0 auto;padding:1.25rem 1rem 3rem}
h1{font-size:1.5rem;margin:0 0 0.25rem;letter-spacing:0.02em}
.sub{color:var(--muted);margin:0 0 1rem;font-size:0.95rem}
.nav{display:flex;flex-wrap:wrap;gap:0.35rem;margin:1rem 0 1.25rem;border-bottom:1px solid var(--line);padding-bottom:0.5rem}
.nav button{background:transparent;color:var(--muted);border:1px solid transparent;border-radius:6px;padding:0.45rem 0.75rem;font:inherit;cursor:pointer;width:auto;margin:0}
.nav button:hover{color:var(--ink);border-color:var(--line)}
.nav button.on{color:var(--ink);background:var(--panel);border-color:var(--line)}
.panel{display:none;background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:1rem 1.1rem}
.panel.on{display:block}
h2{font-size:1.05rem;margin:0 0 0.5rem}
h3{font-size:0.95rem;margin:1.25rem 0 0.4rem;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:0.04em}
label{display:block;margin-top:0.7rem;font-weight:600;font-size:0.9rem}
input,textarea,select{width:100%;padding:0.5rem 0.55rem;margin-top:0.25rem;font:inherit;background:#0d1117;color:var(--ink);border:1px solid var(--line);border-radius:6px}
button.act{margin-top:1rem;width:100%;padding:0.75rem;font-size:1rem;font-weight:700;cursor:pointer;background:var(--focus);color:#0d1117;border:0;border-radius:6px}
button.act:disabled{opacity:0.5;cursor:wait}
button.danger{background:var(--danger);color:#fff}
.flash{padding:0.75rem;background:#12261a;border:1px solid var(--accent);margin-bottom:1rem;border-radius:6px}
.err{background:#2a1215;border-color:var(--danger)}
.meta{font-size:0.88rem;color:var(--muted);margin:0.75rem 0}
code{font-family:Consolas,"Cascadia Mono",monospace;font-size:0.84rem;color:#c9d1d9}
.live{font-weight:700;color:var(--accent)}
.banner{padding:0.75rem 0.9rem;border-radius:6px;margin:0.75rem 0;border:1px solid var(--line)}
.banner.kill{background:#2a1215;border-color:var(--danger);color:#ffb4b0}
.banner.ok{background:#12261a;border-color:var(--accent)}
table.roster{width:100%;border-collapse:collapse;font-size:0.85rem;margin-top:0.5rem}
table.roster th,table.roster td{border-bottom:1px solid var(--line);padding:0.45rem 0.35rem;text-align:left;vertical-align:top}
table.roster th{color:var(--muted);font-weight:600}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0.75rem}
@media(max-width:640px){.grid2{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
<h1>Athanor admin</h1>
<p class="sub">Loopback only (DEC-0042 / DEC-0056 / DEC-0057). Policy pushes over PQ/AEAD tunnel to
nodes and peer hubs. Only the primary admin hub authors security policy. Messages tab
shares the same mesh floor as phones (hubs + nodes).</p>
<div class="meta" id="status">
<span class="live" id="devLine">__DEVLINE__</span><br/>
<span id="apkLine">__APKLINE__</span><br/>
Bind: __BIND__ · policy_ver=<code>__POLVER__</code> · wipe_armed=<code>__WIPE_VAL__</code>
 · admin=<code>__ADMIN_ROLE__</code>
</div>
__KILL_BANNER__
__ADMIN_BANNER__
__FLASH__
__DETAIL__
<nav class="nav" id="tabs">
<button type="button" data-tab="overview" class="on">Overview</button>
<button type="button" data-tab="devices">Devices</button>
<button type="button" data-tab="messages">Messages</button>
<button type="button" data-tab="security">Security</button>
<button type="button" data-tab="peers">Peers</button>
<button type="button" data-tab="enroll">Enroll</button>
<button type="button" data-tab="compromise">Compromise</button>
<button type="button" data-tab="updates">Updates</button>
</nav>

<section class="panel on" id="tab-overview">
<h2>Overview</h2>
<p class="meta">Hub listen must be running. Live nodes get policy immediately; offline
nodes and hubs sync on next ESTABLISHED / higher <code>policy_ver</code>.</p>
<p class="meta">Listen path is single-session today (one phone at a time). Peer hubs
listed under Peers receive policy fan-out over tunnel AEAD.</p>
</section>

<section class="panel" id="tab-devices">
<h2>Devices</h2>
<p class="meta">From <code>lab/enrollments/</code> (USB bootstrap receipts). Not a live
presence feed - reconnect after policy change to confirm apply.</p>
__DEVICE_ROSTER__
</section>

<section class="panel" id="tab-messages">
__MESSAGES_SECTION__
</section>

<section class="panel" id="tab-security">
<h2>Security postures</h2>
<p class="meta">Writes <code>lab/org-policy.conf</code> (ver __POLVER__). Primary admin
hub only. Takes effect immediately on live nodes; peer hubs adopt higher ver;
offline catch up on rejoin. USB/ADB gates enforce only when <code>wipe_armed=1</code>.</p>
__POLICY_FORM__
</section>

<section class="panel" id="tab-peers">
__PEERS_SECTION__
</section>

<section class="panel" id="tab-enroll">
<h2>USB enroll (bootstrap)</h2>
<p class="meta">Lab path uses USB debugging. Kill-mode USB gates refuse enroll when
armed - join first, then enable postures under Security.</p>
<form method="POST" action="/enroll" id="enrollForm">
<label>Phone number (roster label)</label>
<input name="phone_number" required placeholder="+61..." pattern="\+?[0-9][0-9 \-]{5,30}[0-9]"/>
<label>Hub domain (optional; resolves to peer_ipv4)</label>
<input name="peer_domain" value="$peerDomain" placeholder="mesh.example.org"/>
<label>Hub peer_ipv4</label>
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
<button class="act" type="submit" name="action" value="enroll" id="enrollBtn">Connect and Enroll</button>
</form>
</section>

<section class="panel" id="tab-compromise">
<h2>Compromise vote</h2>
<p class="meta">DEC-0047. State: __COMP_STATE__ vote_id=__COMP_VID__
yes=__COMP_YES__ no=__COMP_NO__ quorum=__COMP_QUORUM__ target=__COMP_TARGET__</p>
<form method="POST" action="/compromise" id="compStartForm">
<input type="hidden" name="comp_action" value="start"/>
<label>target_label</label>
<select name="target_label">__COMP_LABEL_OPTS__</select>
<label>timeout_s</label>
<input name="timeout_s" value="__COMP_TIMEOUT__" required/>
<label>quorum</label>
<input name="quorum" value="__COMP_QUORUM_IN__" required/>
<button class="act" type="submit">Start compromise vote</button>
</form>
<form method="POST" action="/compromise" style="margin-top:0.75rem">
<input type="hidden" name="comp_action" value="yes"/>
<button class="act danger" type="submit">Vote YES</button>
</form>
<form method="POST" action="/compromise">
<input type="hidden" name="comp_action" value="no"/>
<button class="act" type="submit">Vote NO</button>
</form>
<form method="POST" action="/compromise">
<input type="hidden" name="comp_action" value="clear"/>
<button class="act" type="submit">Clear vote</button>
</form>
</section>

<section class="panel" id="tab-updates">
<h2>Publish mesh update</h2>
<p class="meta">DEC-0048. Tunnel only (<code>U</code>/<code>UC</code>). id=__UPD_ID__
kind=__UPD_KIND__ size=__UPD_SIZE__.</p>
<form method="POST" action="/update" id="updateForm">
<label>kind</label>
<select name="kind">__KIND_OPTS__</select>
<label>version</label>
<input name="version" value="__UPD_VER__" required placeholder="1.0.0"/>
<label>source_path</label>
<input name="source_path" value="android/athanor-lab.apk" required/>
<button class="act" type="submit">Publish update</button>
</form>
</section>
</div>
<script>
(function(){
  var tabs=document.querySelectorAll('#tabs button');
  function show(id){
    tabs.forEach(function(b){ b.classList.toggle('on', b.getAttribute('data-tab')===id); });
    document.querySelectorAll('.panel').forEach(function(p){
      p.classList.toggle('on', p.id==='tab-'+id);
    });
    try{ localStorage.setItem('atnAdminTab', id); }catch(e){}
  }
  tabs.forEach(function(b){ b.addEventListener('click', function(){ show(b.getAttribute('data-tab')); }); });
  var saved=null; try{ saved=localStorage.getItem('atnAdminTab'); }catch(e){}
  if(saved) show(saved);
  var form=document.getElementById('enrollForm');
  var btn=document.getElementById('enrollBtn');
  if(form&&btn){ form.addEventListener('submit', function(){ btn.disabled=true; btn.textContent='Enrolling...'; }); }
  function tick(){
    fetch('/status').then(function(r){ return r.json(); }).then(function(j){
      var d=document.getElementById('devLine');
      var a=document.getElementById('apkLine');
      if(d) d.textContent=j.device_ok?('USB device: '+j.device+' (ready)'):'USB device: none (plug in with USB debugging)';
      if(a) a.textContent=j.apk_ok?'APK: android/athanor-lab.apk OK':'APK: missing - run make android-apk';
    }).catch(function(){});
  }
  setInterval(tick, 3000);
})();
</script>
</body>
</html>
"@
    $bind = ("http" + "://" + "127.0.0.1:$Port/")
    $killBanner = if ($pol.wipe_armed -eq "1") {
        '<div class="banner kill"><strong>KILL MODE ARMED</strong> - wipe_armed=1. USB/ADB gates and crypto-shred BOOM are live when their flags are on.</div>'
    } else {
        '<div class="banner ok">Test boom mode (wipe_armed=0). Arm kill only when ready for production posture.</div>'
    }
    $adminRole = Load-AdminRole
    $isPrimary = Admin-IsPrimary
    $adminBanner = if ($isPrimary) {
        ('<div class="banner ok">Primary admin hub (<code>{0}</code>). This hub authors network security policy.</div>' -f (Html-Encode $adminRole.hub_id))
    } else {
        ('<div class="banner kill">Secondary hub (<code>{0}</code> role={1}). Policy Save disabled — adopt higher policy_ver from primary over tunnel. Messages still work.</div>' -f (Html-Encode $adminRole.hub_id), (Html-Encode $adminRole.role))
    }
    $policyForm = if ($isPrimary) {
        @"
<form method="POST" action="/policy" id="policyForm">
<h3>Boom / kill</h3>
<div class="grid2">
<div><label>wipe_armed (1 = kill shred)</label><select name="wipe_armed">__WIPE_OPTS__</select></div>
<div><label>boom_silence_s</label><input name="boom_silence_s" value="__BOOM__" required/></div>
</div>
<div class="grid2">
<div><label>diag</label><select name="diag">__DIAG_OPTS__</select></div>
<div><label>flush_mode</label><select name="flush_mode">__FLUSH_OPTS__</select></div>
</div>
<label>outage_class</label>
<select name="outage_class">__OUTAGE_OPTS__</select>
<h3>Lock screen</h3>
<div class="grid2">
<div><label>password_fail_max</label><input name="password_fail_max" value="__FAILK__" required/></div>
<div><label>password_min_len</label><input name="password_min_len" value="__MINLEN__" required/></div>
</div>
<div class="grid2">
<div><label>biometric_allowed</label><select name="biometric_allowed">__BIO_OPTS__</select></div>
<div><label>pwd_deny_check</label><select name="pwd_deny_check">__DENY_OPTS__</select></div>
</div>
<h3>USB / ADB (kill-mode gates)</h3>
<p class="meta">Detect always on phone. Enroll-block and runtime BOOM only when
wipe_armed=1 and flags below. Bootstrap enroll with wipe_armed=0, then arm.</p>
<div class="grid2">
<div><label>usb_data_block (Knox charge-only assert)</label><select name="usb_data_block">__USB_OPTS__</select></div>
<div><label>require_adb_off</label><select name="require_adb_off">__ADB_OPTS__</select></div>
</div>
<div class="grid2">
<div><label>require_usb_charge_only</label><select name="require_usb_charge_only">__CHG_OPTS__</select></div>
<div><label>enroll_block_on_usb</label><select name="enroll_block_on_usb">__ENROLLUSB_OPTS__</select></div>
</div>
<label>boom_on_usb_breach (runtime BOOM if posture flips)</label>
<select name="boom_on_usb_breach">__BOOMUSB_OPTS__</select>
<button class="act" type="submit">Save network policy</button>
</form>
"@
    } else {
        '<p class="meta">Read-only on secondary hubs. Change <code>lab/admin-role.conf</code> only if this machine is intentionally the sole primary.</p>'
    }
    $html = $html.Replace('__DEVLINE__', (Html-Encode $devLine))
    $html = $html.Replace('__APKLINE__', (Html-Encode $apkLine))
    $html = $html.Replace('__BIND__', (Html-Encode $bind))
    $html = $html.Replace('__FLASH__', $flashHtml)
    $html = $html.Replace('__DETAIL__', $detailHtml)
    $html = $html.Replace('__KILL_BANNER__', $killBanner)
    $html = $html.Replace('__ADMIN_BANNER__', $adminBanner)
    $html = $html.Replace('__ADMIN_ROLE__', (Html-Encode $adminRole.role))
    $html = $html.Replace('__POLICY_FORM__', $policyForm)
    $html = $html.Replace('__MESSAGES_SECTION__', (Html-Messages-Section))
    $html = $html.Replace('__DEVICE_ROSTER__', (List-DeviceRoster))
    $html = $html.Replace('__POLVER__', (Html-Encode $pol.policy_ver))
    $html = $html.Replace('__WIPE_VAL__', (Html-Encode $pol.wipe_armed))
    $html = $html.Replace('__UPD_ID__', (Html-Encode $upd.update_id))
    $html = $html.Replace('__UPD_KIND__', (Html-Encode $upd.kind))
    $html = $html.Replace('__UPD_SIZE__', (Html-Encode $upd.size))
    $html = $html.Replace('__UPD_VER__', (Html-Encode $upd.version))
    $html = $html.Replace('__KIND_OPTS__', ((Opt $upd.kind "apk" "apk") + (Opt $upd.kind "site" "site") + (Opt $upd.kind "hub" "hub")))
    $html = $html.Replace('__PEERS_SECTION__', (Html-Peers-Section))
    $diagOpts = (Opt $pol.diag "1" "1 (lab)") + (Opt $pol.diag "0" "0 (prod)")
    $flushOpts = (Opt $pol.flush_mode "log_only" "log_only") + (Opt $pol.flush_mode "zeroize" "zeroize")
    $wipeOpts = (Opt $pol.wipe_armed "0" "0 (test)") + (Opt $pol.wipe_armed "1" "1 (KILL)")
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
    $html = $html.Replace('__ADB_OPTS__', ((Opt $pol.require_adb_off "0" "0") + (Opt $pol.require_adb_off "1" "1 (require ADB off)")))
    $html = $html.Replace('__CHG_OPTS__', ((Opt $pol.require_usb_charge_only "0" "0") + (Opt $pol.require_usb_charge_only "1" "1 (charge-only)")))
    $html = $html.Replace('__ENROLLUSB_OPTS__', ((Opt $pol.enroll_block_on_usb "0" "0") + (Opt $pol.enroll_block_on_usb "1" "1 (block kill enroll)")))
    $html = $html.Replace('__BOOMUSB_OPTS__', ((Opt $pol.boom_on_usb_breach "0" "0") + (Opt $pol.boom_on_usb_breach "1" "1 (BOOM on breach)")))
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
    $gate = Test-EnrollUsbGate $serial
    if (-not $gate.Ok) {
        return @{ Ok=$false; Msg=$gate.Msg; Detail="" }
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
        } elseif ($req.HttpMethod -eq "POST" -and $path -eq "/mesh") {
            $reader = New-Object System.IO.StreamReader($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd()
            $reader.Close()
            $form = Get-Form $body
            $result = Save-MeshOutbox $form
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
        } elseif ($path -ne "/" -and $path -ne "/enroll" -and $path -ne "/policy" -and $path -ne "/compromise" -and $path -ne "/update" -and $path -ne "/peers" -and $path -ne "/mesh") {
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
