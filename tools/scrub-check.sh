#!/usr/bin/env bash
# Block git push if org-local IPs/domains appear in tracked files.
# Loads lab/org.local.json (+ optional deploy-state.json). Scans git ls-files only.
# Exit 0 if clean; exit 1 with file:line hits. Prefer block over silent rewrite.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "scrub-check: not inside a git work tree" >&2
  exit 1
}
cd "$ROOT"

if ! command -v python3 >/dev/null 2>&1; then
  echo "scrub-check: python3 required" >&2
  exit 1
fi

export ATN_SCRUB_ROOT="$ROOT"
python3 - <<'PY'
import json, os, re, sys
from pathlib import Path

root = Path(os.environ["ATN_SCRUB_ROOT"])

PLACEHOLDER_PREFIXES = ("REPLACE_", "YOUR_", "TODO_", "CHANGEME")
SKIP_IPS = {"127.0.0.1", "0.0.0.0"}
TOKEN_RE = re.compile(
    r"\b(?:\d{1,3}\.){3}\d{1,3}\b|[A-Za-z0-9][A-Za-z0-9.\-]{1,250}\.[A-Za-z]{2,24}\b"
)

def is_placeholder(v: str) -> bool:
    t = (v or "").strip()
    if not t:
        return True
    if t.startswith(PLACEHOLDER_PREFIXES):
        return True
    if t.startswith("<") and t.endswith(">"):
        return True
    if t.startswith("example."):
        return True
    if t == "localhost":
        return True
    return False

def is_ipv4(s: str) -> bool:
    parts = s.split(".")
    if len(parts) != 4:
        return False
    try:
        nums = [int(p) for p in parts]
    except ValueError:
        return False
    return all(0 <= n <= 255 for n in nums)

def looks_like_domain(s: str) -> bool:
    t = s.strip()
    if len(t) < 4 or len(t) > 253 or is_ipv4(t):
        return False
    return bool(
        re.match(
            r"^[A-Za-z0-9]([A-Za-z0-9\-]*[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9\-]*[A-Za-z0-9])?)+$",
            t,
        )
    )

def add_any(secrets: set, v: str) -> None:
    if is_placeholder(v):
        return
    t = v.strip()
    if is_ipv4(t):
        if t not in SKIP_IPS:
            secrets.add(t)
        return
    if looks_like_domain(t):
        secrets.add(t)

def load_json(path: Path, secrets: set) -> None:
    if not path.is_file():
        return
    try:
        j = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        print(f"scrub-check: warn: could not parse {path}", file=sys.stderr)
        return
    if not isinstance(j, dict):
        return
    for k in ("hub_lan_ipv4", "public_ipv4", "phone_peer_ipv4", "edge_lan_ipv4", "domain"):
        add_any(secrets, str(j.get(k) or ""))
    for v in j.values():
        if isinstance(v, str):
            add_any(secrets, v)
            for m in TOKEN_RE.finditer(v):
                add_any(secrets, m.group(0))

def allowlisted(rel: str) -> bool:
    n = rel.replace("\\", "/")
    if n == "lab/org.local.json.example":
        return True
    if n.endswith(".example"):
        return True
    if n in (
        "tools/scrub-check.ps1",
        "tools/scrub-check.sh",
        "tools/install-git-hooks.ps1",
        "tools/install-git-hooks.sh",
    ):
        return True
    return False

secrets: set[str] = set()
load_json(root / "lab" / "org.local.json", secrets)
load_json(root / "lab" / "deploy-state.json", secrets)

if not secrets:
    print("scrub-check: no org secrets loaded (lab/org.local.json empty or missing) - OK")
    sys.exit(0)

import subprocess
out = subprocess.check_output(["git", "ls-files"], cwd=root, text=True)
files = [f for f in out.splitlines() if f]

hits = []
for rel in files:
    if allowlisted(rel):
        continue
    full = root / rel
    if not full.is_file():
        continue
    try:
        data = full.read_bytes()
    except OSError:
        continue
    if not data or b"\0" in data[:8000]:
        continue
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        continue
    for i, line in enumerate(text.splitlines(), 1):
        for s in secrets:
            if s in line:
                hits.append(f"{rel.replace(chr(92), '/')}:{i}: contains org value '{s}'")

if hits:
    print("scrub-check: FAIL - org-local values found in tracked files:", file=sys.stderr)
    for h in hits:
        print(f"  {h}", file=sys.stderr)
    print("", file=sys.stderr)
    print(
        "Remove these values from tracked content (keep them in gitignored lab/org.local.json).",
        file=sys.stderr,
    )
    print("Push blocked.", file=sys.stderr)
    sys.exit(1)

print(f"scrub-check: OK - {len(secrets)} org secret(s) not present in tracked files")
sys.exit(0)
PY
