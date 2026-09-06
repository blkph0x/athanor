#!/bin/sh
# DEC-0042: Lab enroll console — loopback plain HTTP only (127.0.0.1).
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


def page(flash: str, detail: str) -> str:
    st = status_json()
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
<title>Athanor lab enroll</title>
<style>
body{{font-family:Georgia,serif;max-width:42rem;margin:2rem auto;padding:0 1rem;background:#f7f4ef;color:#1a1a1a}}
h1{{font-size:1.75rem;margin-bottom:0.25rem}}
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
<h1>Athanor lab enroll</h1>
<p class="sub">Loopback only (DEC-0042). Phone number is a local label - never SMS.
POSIX companion to enroll-console.ps1.</p>
<div class="meta" id="status">
<span id="devLine">{html_escape(dev_line)}</span><br/>
<span id="apkLine">{html_escape(apk_line)}</span><br/>
Bind: 127.0.0.1:{PORT}/
</div>
{flash_html}
{detail_html}
<form method="POST" action="/enroll" id="enrollForm">
<label>Phone number (roster label)</label>
<input name="phone_number" required placeholder="+61..." pattern="\\+?[0-9][0-9 \\-]{{5,30}}[0-9]"/>
<label>Hub peer_ipv4</label>
<input name="peer_ipv4" value="YOUR_HUB_LAN_IPV4" required/>
<label>Hub peer_port</label>
<input name="peer_port" value="47000" required/>
<label>Hub peer_ek (hex from atnnode listen)</label>
<textarea name="peer_ek" rows="4" required placeholder="paste peer_ek hex"></textarea>
<label>diag</label>
<select name="diag"><option value="1" selected>1 (lab soak)</option><option value="0">0</option></select>
<label>flush_mode</label>
<select name="flush_mode"><option value="log_only" selected>log_only</option><option value="zeroize">zeroize</option></select>
<label>outage_class</label>
<select name="outage_class">
<option value="normal" selected>normal</option>
<option value="maintenance">maintenance</option>
<option value="blackout">blackout</option>
<option value="faraday">faraday</option>
<option value="capture">capture</option>
</select>
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
    ipv4 = form.get("peer_ipv4", [""])[0]
    port = form.get("peer_port", [""])[0]
    ek = re.sub(r"\s+", "", form.get("peer_ek", [""])[0].strip())
    diag = form.get("diag", ["1"])[0]
    flush = form.get("flush_mode", ["log_only"])[0]
    outage = form.get("outage_class", ["normal"])[0]

    if not phone_ok(phone):
        return False, "ERR: bad phone_number label", ""
    if not re.match(r"^\d{1,3}(\.\d{1,3}){3}$", ipv4):
        return False, "ERR: bad peer_ipv4", ""
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
        if self.path in ("/", "/enroll"):
            body = page("", "").encode("utf-8")
            self._send(200, "text/html; charset=utf-8", body)
            return
        self._send(404, "text/plain", b"not found")

    def do_POST(self):
        if self.path != "/enroll":
            self._send(404, "text/plain", b"not found")
            return
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n).decode("utf-8", errors="replace")
        form = parse_qs(raw, keep_blank_values=True)
        ok, msg, detail = do_enroll(form)
        body = page(msg, detail).encode("utf-8")
        self._send(200 if ok else 500, "text/html; charset=utf-8", body)


print(f"ATN enroll console (DEC-0042) at 127.0.0.1:{PORT}/", flush=True)
print("USB status polls live; Connect and Enroll stays on this page.", flush=True)
print("Ctrl+C to stop.", flush=True)
HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
PY
