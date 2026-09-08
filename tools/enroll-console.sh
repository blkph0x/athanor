#!/bin/sh
# DEC-0042: Lab enroll console - loopback plain HTTP only (127.0.0.1).
# Usage: sh tools/enroll-console.sh [port]
#        .\atnenroll serve [port]
# Companion to tools/enroll-console.ps1 (Windows). Same form + adb enroll.
# Requires python3 on PATH (builder tool; not a product crypto dependency).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
PORT=${1:-8799}

if ! command -v python3 >/dev/null 2>&1; then
  echo "enroll-console.sh: python3 required on POSIX for the loopback UI" >&2
  exit 1
fi

export ATN_ENROLL_ROOT="$ROOT"
export ATN_ENROLL_PORT="$PORT"
exec python3 - <<'PY'
import json
import os
import re
import socket
import subprocess
import sys
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs

ROOT = Path(os.environ["ATN_ENROLL_ROOT"])
PORT = int(os.environ["ATN_ENROLL_PORT"])
ADB = os.environ.get("ADB", "adb")


def html_escape(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def phone_ok(p: str) -> bool:
    if not p or len(p) < 7 or len(p) > 32:
        return False
    return re.match(r"^\+?[0-9][0-9 \-]{5,30}[0-9]$", p) is not None


def find_adb_device():
    try:
        out = subprocess.check_output([ADB, "devices"], text=True, stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError):
        return None
    for line in out.splitlines():
        m = re.match(r"^([A-Za-z0-9]+)\s+device\b", line)
        if m:
            return m.group(1)
    return None


def status_json():
    dev = find_adb_device()
    apk = (ROOT / "android" / "athanor-lab.apk").is_file()
    return {
        "device": dev,
        "device_ok": bool(dev),
        "apk_ok": apk,
        "apk_path": "android/athanor-lab.apk",
        "bind": ("http" + "://" + f"127.0.0.1:{PORT}/"),
        "note": "loopback only; phone_number is a label (no SMS)",
    }


def load_deploy_defaults():
    d = {"peer_ipv4": "", "peer_port": "47000", "peer_ek": "", "peer_domain": ""}

    def apply(j, only_gaps):
        peer = str(j.get("phone_peer_ipv4") or j.get("hub_lan_ipv4") or "")
        if peer and (not only_gaps or not d["peer_ipv4"]):
            d["peer_ipv4"] = peer
        if j.get("peer_port") and (not only_gaps or d["peer_port"] == "47000"):
            d["peer_port"] = str(j["peer_port"])
        if j.get("peer_ek") and (not only_gaps or not d["peer_ek"]):
            d["peer_ek"] = str(j["peer_ek"])
        if j.get("domain") and (not only_gaps or not d["peer_domain"]):
            d["peer_domain"] = str(j["domain"])

    dep = ROOT / "lab" / "deploy-state.json"
    org = ROOT / "lab" / "org.local.json"
    if dep.is_file():
        try:
            apply(json.loads(dep.read_text(encoding="utf-8")), False)
        except (OSError, json.JSONDecodeError, TypeError):
            pass
    if org.is_file():
        try:
            apply(json.loads(org.read_text(encoding="utf-8")), True)
        except (OSError, json.JSONDecodeError, TypeError):
            pass
    return d


def load_org_policy():
    d = {
        "policy_ver": "1",
        "diag": "1",
        "flush_mode": "log_only",
        "wipe_armed": "0",
        "outage_class": "normal",
        "boom_silence_s": "30",
        "password_fail_max": "5",
        "biometric_allowed": "0",
        "password_min_len": "12",
        "usb_data_block": "1",
        "pwd_deny_check": "1",
    }
    path = ROOT / "lab" / "org-policy.conf"
    if not path.is_file():
        return d
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k, v = k.strip(), v.strip()
            if k in d:
                d[k] = v
    except OSError:
        pass
    return d


def opt(cur, val, label):
    sel = " selected" if cur == val else ""
    return f'<option value="{val}"{sel}>{label}</option>'


def save_org_policy(form):
    diag = form.get("diag", ["1"])[0]
    flush = form.get("flush_mode", ["log_only"])[0]
    wipe = form.get("wipe_armed", ["0"])[0]
    outage = form.get("outage_class", ["normal"])[0]
    boom = form.get("boom_silence_s", ["30"])[0]
    fail_k = form.get("password_fail_max", ["5"])[0]
    bio = form.get("biometric_allowed", ["0"])[0]
    min_len = form.get("password_min_len", ["12"])[0]
    usb = form.get("usb_data_block", ["1"])[0]
    deny = form.get("pwd_deny_check", ["1"])[0]
    if diag not in ("0", "1"):
        return False, "ERR: diag", ""
    if flush not in ("log_only", "zeroize"):
        return False, "ERR: flush_mode", ""
    if wipe not in ("0", "1"):
        return False, "ERR: wipe_armed", ""
    if outage not in ("normal", "maintenance", "blackout", "faraday", "capture"):
        return False, "ERR: outage_class", ""
    if flush == "log_only" and diag != "1":
        return False, "ERR: log_only requires diag=1", ""
    if bio not in ("0", "1") or usb not in ("0", "1") or deny not in ("0", "1"):
        return False, "ERR: biometric/usb/deny", ""
    try:
        boom_n = int(boom)
        fail_n = int(fail_k)
        min_n = int(min_len)
    except ValueError:
        return False, "ERR: boom/fail/min int", ""
    if boom_n < 5 or boom_n > 86400:
        return False, "ERR: boom_silence_s 5..86400", ""
    if fail_n < 1 or fail_n > 20:
        return False, "ERR: password_fail_max 1..20", ""
    if min_n < 8 or min_n > 64:
        return False, "ERR: password_min_len 8..64", ""
    prev = load_org_policy()
    try:
        ver = int(prev.get("policy_ver") or "0") + 1
    except ValueError:
        ver = 1
    if ver < 1:
        ver = 1
    body = (
        "policy_ver=%d\n"
        "diag=%s\n"
        "flush_mode=%s\n"
        "wipe_armed=%s\n"
        "outage_class=%s\n"
        "boom_silence_s=%d\n"
        "password_fail_max=%d\n"
        "biometric_allowed=%s\n"
        "password_min_len=%d\n"
        "usb_data_block=%s\n"
        "pwd_deny_check=%s\n"
    ) % (ver, diag, flush, wipe, outage, boom_n, fail_n, bio, min_n, usb, deny)
    lab = ROOT / "lab"
    lab.mkdir(parents=True, exist_ok=True)
    (lab / "org-policy.conf").write_text(body, encoding="utf-8")
    return True, f"OK: network policy ver={ver} saved (DEC-0045/0046) - phones pick up from hub.", body


def load_update_announce():
    d = {
        "update_id": "0",
        "kind": "apk",
        "version": "",
        "sha256": "",
        "size": "0",
        "chunk_size": "900",
        "payload_path": "lab/updates/payload.bin",
    }
    path = ROOT / "lab" / "updates" / "announce.conf"
    if not path.is_file():
        return d
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k, v = k.strip(), v.strip()
            if k in d:
                d[k] = v
    except OSError:
        pass
    return d


def publish_update(form):
    import hashlib
    import shutil

    kind = form.get("kind", ["apk"])[0]
    version = form.get("version", [""])[0].strip()
    src = form.get("source_path", ["android/athanor-lab.apk"])[0].strip()
    if not src:
        src = "android/athanor-lab.apk"
    if kind not in ("apk", "site", "hub"):
        return False, "ERR: kind apk|site|hub", ""
    if not version or len(version) >= 64:
        return False, "ERR: version required (<64 chars)", ""
    src_path = Path(src) if Path(src).is_absolute() else ROOT / src
    if not src_path.is_file():
        return False, f"ERR: source missing: {src}", ""
    upd_dir = ROOT / "lab" / "updates"
    upd_dir.mkdir(parents=True, exist_ok=True)
    dest = upd_dir / "payload.bin"
    shutil.copyfile(str(src_path), str(dest))
    data = dest.read_bytes()
    if len(data) == 0:
        return False, "ERR: bad payload size", ""
    digest = hashlib.sha256(data).hexdigest()
    size = len(data)
    prev = load_update_announce()
    try:
        uid = int(prev.get("update_id") or "0") + 1
    except ValueError:
        uid = 1
    if uid < 1:
        uid = 1
    body = (
        "update_id=%d\n"
        "kind=%s\n"
        "version=%s\n"
        "sha256=%s\n"
        "size=%d\n"
        "chunk_size=900\n"
        "payload_path=lab/updates/payload.bin\n"
    ) % (uid, kind, version, digest, size)
    (upd_dir / "announce.conf").write_text(body, encoding="utf-8")
    return (
        True,
        "OK: published update_id=%d kind=%s size=%d (DEC-0048). "
        "Hub streams over encrypted tunnel DATA only — no HTTP download. "
        "Keep atnnode listen running." % (uid, kind, size),
        body,
    )


def load_compromise():
    d = {
        "vote_id": "0",
        "target_label": "",
        "opened_unix": "0",
        "timeout_s": "300",
        "yes": "0",
        "no": "0",
        "quorum": "1",
        "state": "none",
    }
    path = ROOT / "lab" / "compromise-vote.conf"
    if not path.is_file():
        return d
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k, v = k.strip(), v.strip()
            if k in d:
                d[k] = v
    except OSError:
        pass
    return d


def list_enrolled_labels():
    labels = []
    base = ROOT / "lab" / "enrollments"
    if not base.is_dir():
        return labels
    for d in sorted(base.iterdir()):
        rec = d / "enrollment.txt"
        if not rec.is_file():
            continue
        try:
            for line in rec.read_text(encoding="utf-8").splitlines():
                if line.startswith("phone_number_label="):
                    lab = line.split("=", 1)[1].strip()
                    if lab and lab not in labels:
                        labels.append(lab)
        except OSError:
            pass
    return labels


def save_compromise_file(c):
    lab = ROOT / "lab"
    lab.mkdir(parents=True, exist_ok=True)
    body = (
        "vote_id=%s\n"
        "target_label=%s\n"
        "opened_unix=%s\n"
        "timeout_s=%s\n"
        "yes=%s\n"
        "no=%s\n"
        "quorum=%s\n"
        "state=%s\n"
    ) % (
        c["vote_id"],
        c["target_label"],
        c["opened_unix"],
        c["timeout_s"],
        c["yes"],
        c["no"],
        c["quorum"],
        c["state"],
    )
    (lab / "compromise-vote.conf").write_text(body, encoding="utf-8")
    return body


def do_compromise(form):
    import time as _time
    action = form.get("comp_action", [""])[0]
    prev = load_compromise()
    if action == "start":
        target = form.get("target_label", [""])[0].strip()
        timeout = form.get("timeout_s", ["300"])[0]
        quorum = form.get("quorum", ["1"])[0]
        if not phone_ok(target):
            return False, "ERR: bad target_label (roster phone)", ""
        try:
            to_n = int(timeout)
            q_n = int(quorum)
        except ValueError:
            return False, "ERR: timeout/quorum int", ""
        if to_n < 30 or to_n > 86400:
            return False, "ERR: timeout_s 30..86400", ""
        if q_n < 1 or q_n > 64:
            return False, "ERR: quorum 1..64", ""
        if prev["state"] in ("open", "boom_pending"):
            return False, "ERR: vote already open - clear or wait for boom", ""
        try:
            vid = int(prev.get("vote_id") or "0") + 1
        except ValueError:
            vid = 1
        if vid < 1:
            vid = 1
        c = {
            "vote_id": str(vid),
            "target_label": target,
            "opened_unix": str(int(_time.time())),
            "timeout_s": str(to_n),
            "yes": "0",
            "no": "0",
            "quorum": str(q_n),
            "state": "open",
        }
        body = save_compromise_file(c)
        msg = (
            "OK: compromise vote %d OPEN for %s (timeout %ds quorum %d). "
            "Hub pushes boom on YES quorum or timeout."
        ) % (vid, target, to_n, q_n)
        return True, msg, body
    if action in ("yes", "no"):
        if prev["state"] != "open":
            return False, "ERR: no open vote", ""
        try:
            yes = int(prev.get("yes") or "0")
            no = int(prev.get("no") or "0")
        except ValueError:
            yes, no = 0, 0
        if action == "yes":
            yes += 1
        else:
            no += 1
        prev["yes"] = str(yes)
        prev["no"] = str(no)
        body = save_compromise_file(prev)
        msg = "OK: voted %s (yes=%d no=%d quorum=%s)" % (
            action.upper(), yes, no, prev["quorum"])
        return True, msg, body
    if action == "clear":
        prev["state"] = "cleared"
        body = save_compromise_file(prev)
        return True, "OK: vote cleared (hub notifies phones)", body
    return False, "ERR: unknown comp_action", ""


def page(flash: str, detail: str) -> str:
    st = status_json()
    defs = load_deploy_defaults()
    pol = load_org_policy()
    comp = load_compromise()
    upd = load_update_announce()
    labels = list_enrolled_labels()
    if labels:
        label_opts = "".join(opt(comp["target_label"], lab, lab) for lab in labels)
        target_field = "<select name=\"target_label\">%s</select>" % label_opts
    else:
        target_field = (
            "<input name=\"target_label\" required placeholder=\"+61...\" "
            "pattern=\"\\+?[0-9][0-9 \\-]{5,30}[0-9]\"/>"
        )
    peer_domain = html_escape(defs["peer_domain"])
    peer_ipv4 = html_escape(defs["peer_ipv4"])
    peer_port = html_escape(defs["peer_port"])
    peer_ek = html_escape(defs["peer_ek"])
    pol_ver = html_escape(pol["policy_ver"])
    diag_opts = opt(pol["diag"], "1", "1 (lab)") + opt(pol["diag"], "0", "0 (prod)")
    flush_opts = opt(pol["flush_mode"], "log_only", "log_only") + opt(pol["flush_mode"], "zeroize", "zeroize")
    wipe_opts = opt(pol["wipe_armed"], "0", "0") + opt(pol["wipe_armed"], "1", "1")
    outage_opts = (
        opt(pol["outage_class"], "normal", "normal")
        + opt(pol["outage_class"], "maintenance", "maintenance")
        + opt(pol["outage_class"], "blackout", "blackout")
        + opt(pol["outage_class"], "faraday", "faraday")
        + opt(pol["outage_class"], "capture", "capture")
    )
    dev_line = (
        f"USB device: {st['device']} (ready)"
        if st["device_ok"]
        else "USB device: none (plug in with USB debugging)"
    )
    apk_line = (
        "APK: android/athanor-lab.apk OK"
        if st["apk_ok"]
        else "APK: missing - run make android-apk"
    )
    flash_html = ""
    if flash:
        cls = "flash err" if flash.startswith("ERR") else "flash"
        flash_html = f"<div class='{cls}'>{html_escape(flash)}</div>"
    detail_html = ""
    if detail:
        detail_html = f"<pre class='meta'><code>{html_escape(detail)}</code></pre>"
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Athanor admin</title>
<style>
body{{font-family:Georgia,serif;max-width:42rem;margin:2rem auto;padding:0 1rem;background:#f7f4ef;color:#1a1a1a}}
h1{{font-size:1.75rem;margin-bottom:0.25rem}}
h2{{font-size:1.2rem;margin-top:2rem;border-top:1px solid #ccc;padding-top:1rem}}
.sub{{color:#444;margin-bottom:1.5rem}}
label{{display:block;margin-top:0.75rem;font-weight:bold}}
input,textarea,select{{width:100%;box-sizing:border-box;padding:0.5rem;margin-top:0.25rem;font:inherit}}
button{{margin-top:1.25rem;width:100%;padding:0.85rem;font-size:1.1rem;font-weight:bold;cursor:pointer;background:#1a1a1a;color:#f7f4ef;border:0}}
.flash{{padding:0.75rem;background:#e8f0e4;border:1px solid #6a8f5a;margin-bottom:1rem}}
.err{{background:#f8e8e8;border-color:#a55}}
.meta{{font-size:0.9rem;color:#333;margin:1rem 0}}
code{{font-family:Consolas,monospace;font-size:0.85rem}}
</style>
</head>
<body>
<h1>Athanor admin</h1>
<p class="sub">Loopback only (DEC-0042 / DEC-0045). Network policy pushes to connected
Android nodes via the hub. Phone number is a roster label - never SMS.</p>
<div class="meta" id="status">
<span id="devLine">{html_escape(dev_line)}</span><br/>
<span id="apkLine">{html_escape(apk_line)}</span><br/>
Bind: 127.0.0.1:{PORT}/
</div>
{flash_html}
{detail_html}
<h2>Publish mesh update</h2>
<p class="meta">DEC-0048. Copies a local hub file to <code>lab/updates/payload.bin</code>,
writes announce.conf (id={html_escape(upd["update_id"])} kind={html_escape(upd["kind"])}
size={html_escape(upd["size"])}). Hub pushes announce+chunks over the
<strong>encrypted tunnel only</strong> (DATA <code>U</code> / <code>UC</code>) to the
ESTABLISHED phone and peer hubs in <code>lab/hub-peers.conf</code>. No HTTP/URL path.</p>
<form method="POST" action="/update" id="updateForm">
<label>kind</label>
<select name="kind">{opt(upd["kind"], "apk", "apk") + opt(upd["kind"], "site", "site") + opt(upd["kind"], "hub", "hub")}</select>
<label>version</label>
<input name="version" value="{html_escape(upd["version"])}" required placeholder="1.0.0"/>
<label>source_path (local on this hub)</label>
<input name="source_path" value="android/athanor-lab.apk" required/>
<button type="submit">Publish update</button>
</form>
<h2>Network-wide policy</h2>
<p class="meta">Writes <code>lab/org-policy.conf</code> (ver {pol_ver}). Keep
<code>atnnode listen</code> running so ESTABLISHED phones receive pushes.</p>
<form method="POST" action="/policy" id="policyForm">
<label>diag</label>
<select name="diag">{diag_opts}</select>
<label>flush_mode</label>
<select name="flush_mode">{flush_opts}</select>
<label>wipe_armed</label>
<select name="wipe_armed">{wipe_opts}</select>
<label>outage_class</label>
<select name="outage_class">{outage_opts}</select>
<label>boom_silence_s (hub silence -> BOOM)</label>
<input name="boom_silence_s" value="{html_escape(pol["boom_silence_s"])}" required/>
<label>password_fail_max</label>
<input name="password_fail_max" value="{html_escape(pol["password_fail_max"])}" required/>
<label>biometric_allowed (0=OFF)</label>
<select name="biometric_allowed">{opt(pol["biometric_allowed"], "0", "0 (OFF)") + opt(pol["biometric_allowed"], "1", "1")}</select>
<label>password_min_len</label>
<input name="password_min_len" value="{html_escape(pol["password_min_len"])}" required/>
<label>usb_data_block</label>
<select name="usb_data_block">{opt(pol["usb_data_block"], "1", "1 (block)") + opt(pol["usb_data_block"], "0", "0")}</select>
<label>pwd_deny_check</label>
<select name="pwd_deny_check">{opt(pol["pwd_deny_check"], "1", "1") + opt(pol["pwd_deny_check"], "0", "0")}</select>
<button type="submit">Save network policy</button>
</form>
<h2>Compromise vote (stolen phone)</h2>
<p class="meta">DEC-0047. Quorum YES or timeout -> hub boom. State: {html_escape(comp["state"])}
vote_id={html_escape(comp["vote_id"])} yes={html_escape(comp["yes"])} no={html_escape(comp["no"])}
quorum={html_escape(comp["quorum"])} target={html_escape(comp["target_label"])}</p>
<form method="POST" action="/compromise">
<input type="hidden" name="comp_action" value="start"/>
<label>target_label (enrolled phone)</label>
{target_field}
<label>timeout_s</label>
<input name="timeout_s" value="{html_escape(comp["timeout_s"])}" required/>
<label>quorum</label>
<input name="quorum" value="{html_escape(comp["quorum"])}" required/>
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
<input name="phone_number" required placeholder="+61..." pattern="\\+?[0-9][0-9 \\-]{{5,30}}[0-9]"/>
<label>Hub domain (optional; resolves to peer_ipv4)</label>
<input name="peer_domain" value="{peer_domain}" placeholder="mesh.example.org"/>
<label>Hub peer_ipv4 (dotted; leave blank to use domain)</label>
<input name="peer_ipv4" value="{peer_ipv4}" placeholder="hub or public IPv4"/>
<label>Hub peer_port</label>
<input name="peer_port" value="{peer_port}" required/>
<label>Hub peer_ek (hex from atnnode listen)</label>
<textarea name="peer_ek" rows="4" required placeholder="paste peer_ek hex">{peer_ek}</textarea>
<label>diag (seed conf; live hub policy overrides)</label>
<select name="diag">{diag_opts}</select>
<label>flush_mode</label>
<select name="flush_mode">{flush_opts}</select>
<label>outage_class</label>
<select name="outage_class">{outage_opts}</select>
<button type="submit" name="action" value="enroll" id="enrollBtn">Connect and Enroll</button>
</form>
<script>
(function(){{
  setInterval(function(){{
    fetch('/status').then(function(r){{ return r.json(); }}).then(function(j){{
      var d = document.getElementById('devLine');
      var a = document.getElementById('apkLine');
      if (d) d.textContent = j.device_ok
        ? ('USB device: ' + j.device + ' (ready)')
        : 'USB device: none (plug in with USB debugging)';
      if (a) a.textContent = j.apk_ok
        ? 'APK: android/athanor-lab.apk OK'
        : 'APK: missing - run make android-apk';
    }}).catch(function(){{}});
  }}, 3000);
}})();
</script>
</body>
</html>
"""


def adb(serial, *args):
    cmd = [ADB, "-s", serial, *args]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True)
        out = (p.stdout or "") + (p.stderr or "")
        return p.returncode, out.strip()
    except OSError as e:
        return 1, str(e)


def do_enroll(form):
    phone = form.get("phone_number", [""])[0]
    domain = form.get("peer_domain", [""])[0].strip()
    ipv4 = form.get("peer_ipv4", [""])[0].strip()
    port = form.get("peer_port", [""])[0]
    ek = re.sub(r"\s+", "", form.get("peer_ek", [""])[0].strip())
    diag = form.get("diag", ["1"])[0]
    flush = form.get("flush_mode", ["log_only"])[0]
    outage = form.get("outage_class", ["normal"])[0]

    if not phone_ok(phone):
        return False, "ERR: bad phone_number label", ""
    if ipv4 == "" and domain:
        try:
            ipv4 = socket.gethostbyname(domain)
        except OSError:
            return False, "ERR: domain resolve failed", ""
    if not re.match(r"^\d{1,3}(\.\d{1,3}){3}$", ipv4):
        return False, "ERR: bad peer_ipv4 (or set peer_domain)", ""
    if not re.match(r"^\d{1,5}$", port):
        return False, "ERR: bad peer_port", ""
    if len(ek) != 3136 or not re.match(r"^[0-9a-fA-F]+$", ek):
        return False, "ERR: peer_ek must be 3136 hex chars (ML-KEM-1024)", ""
    if diag not in ("0", "1"):
        return False, "ERR: diag", ""
    if flush not in ("log_only", "zeroize"):
        return False, "ERR: flush_mode", ""
    if outage not in ("normal", "maintenance", "blackout", "faraday", "capture"):
        return False, "ERR: outage_class", ""
    apk = ROOT / "android" / "athanor-lab.apk"
    if not apk.is_file():
        return False, "ERR: android/athanor-lab.apk missing (make android-apk)", ""
    serial = find_adb_device()
    if not serial:
        return False, "ERR: no adb device - enable USB debugging and keep this page open", ""

    safe = re.sub(r"[^0-9+]", "", phone)
    eid = datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + safe
    edir = ROOT / "lab" / "enrollments" / eid
    edir.mkdir(parents=True, exist_ok=True)
    conf = (
        f"peer_ipv4={ipv4}\npeer_port={port}\npeer_ek={ek}\n"
        f"diag={diag}\nflush_mode={flush}\noutage_class={outage}\n"
    )
    conf_path = edir / "atn-node.conf"
    conf_path.write_text(conf, encoding="utf-8")
    (ROOT / "lab" / "phone-atn-node.conf").write_text(conf, encoding="utf-8")

    log = [
        f"enrollment_id={eid}",
        f"phone_number_label={phone}",
        f"serial={serial}",
    ]
    rc, out = adb(serial, "install", "-r", str(apk))
    log.append(f"adb_install: {out}")
    if rc != 0 or "Success" not in out:
        return False, "ERR: adb install failed (unlock phone / allow install)", "\n".join(log)

    rc, out = adb(serial, "push", str(conf_path), "/data/local/tmp/atn-node.conf")
    log.append(f"adb_push: {out}")
    rc, out = adb(serial, "shell", "run-as", "com.athanor.daemon", "mkdir", "-p", "files")
    log.append(f"adb_mkdir: {out}")
    rc, out = adb(
        serial,
        "shell",
        "run-as",
        "com.athanor.daemon",
        "cp",
        "/data/local/tmp/atn-node.conf",
        "/data/user/0/com.athanor.daemon/files/atn-node.conf",
    )
    log.append(f"adb_conf: {out}")
    adb(serial, "shell", "am", "force-stop", "com.athanor.daemon")
    rc, out = adb(
        serial,
        "shell",
        "am",
        "start",
        "-n",
        "com.athanor.daemon/.AtnLabActivity",
        "--ez",
        "autostart",
        "true",
        "--ez",
        "request_admin",
        "true",
    )
    log.append(f"adb_start: {out}")

    receipt = edir / "enrollment.txt"
    body = "\n".join(
        [
            "ATN-ENROLL-1",
            f"id={eid}",
            f"phone_number_label={phone}",
            f"serial={serial}",
            f"peer_ipv4={ipv4}",
            f"peer_port={port}",
            f"diag={diag}",
            f"flush_mode={flush}",
            f"outage_class={outage}",
            f"host={os.uname().nodename if hasattr(os, 'uname') else 'posix'}",
            f"time_utc={datetime.now(timezone.utc).isoformat()}",
            "note=lab_usb_enroll DEC-0042; POSIX enroll-console.sh; air-gap = release beta",
        ]
    )
    receipt.write_text(body + "\n", encoding="utf-8")
    log.append(f"receipt={receipt}")

    sign = ROOT / "atnsign"
    if not sign.is_file():
        sign = ROOT / "atnsign.exe"
    key_dir = ROOT / "lab" / "enroll-keys"
    if sign.is_file():
        key_dir.mkdir(parents=True, exist_ok=True)
        pk, sk = key_dir / "enroll.pk", key_dir / "enroll.sk"
        if not (pk.is_file() and sk.is_file()):
            subprocess.run([str(sign), "keygen", str(pk), str(sk)], check=False)
        sig = edir / "enrollment.sig"
        subprocess.run([str(sign), "sign", str(sk), str(receipt), str(sig)], check=False)
        if sig.is_file():
            log.append(f"signed=yes pk={pk}")
        else:
            log.append("signed=no (atnsign sign failed)")
    else:
        log.append("signed=no (build atnsign for local receipt sign)")

    detail = "\n".join(log)
    (edir / "enroll.log").write_text(detail + "\n", encoding="utf-8")
    return True, f"OK: enrolled {phone} - Activate Device Admin on phone if shown, then mesh.", detail


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("enroll: " + (fmt % args) + "\n")

    def _send(self, code, ctype, body: bytes):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/status"):
            body = json.dumps(status_json()).encode("utf-8")
            self._send(200, "application/json; charset=utf-8", body)
            return
        if self.path in ("/", "/enroll", "/policy", "/compromise", "/update"):
            body = page("", "").encode("utf-8")
            self._send(200, "text/html; charset=utf-8", body)
            return
        self._send(404, "text/plain", b"not found")

    def do_POST(self):
        if self.path not in ("/enroll", "/policy", "/compromise", "/update"):
            self._send(404, "text/plain", b"not found")
            return
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n).decode("utf-8", errors="replace")
        form = parse_qs(raw, keep_blank_values=True)
        if self.path == "/policy":
            ok, msg, detail = save_org_policy(form)
        elif self.path == "/update":
            ok, msg, detail = publish_update(form)
        elif self.path == "/compromise":
            ok, msg, detail = do_compromise(form)
        else:
            ok, msg, detail = do_enroll(form)
        body = page(msg, detail).encode("utf-8")
        self._send(200 if ok else 500, "text/html; charset=utf-8", body)


print(f"ATN admin (DEC-0042/0045/0047/0048) at 127.0.0.1:{PORT}/", flush=True)
print("Network policy + update publish + compromise vote + USB enroll. Ctrl+C to stop.", flush=True)
print("Ctrl+C to stop.", flush=True)
HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
PY
