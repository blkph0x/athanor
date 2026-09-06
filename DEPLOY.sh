#!/bin/sh
# Athanor full lab deploy (POSIX: Linux / macOS / WSL)
# Companion: DEPLOY.ps1 (Windows). Entry: ./DEPLOY or ./DEPLOY.sh
#
#   chmod +x DEPLOY.sh
#   ./DEPLOY.sh
#
# Start to finish:
#   1. Toolchain check (make/cc, optional adb)
#   2. Ask org hosts/IPs (never committed)
#   3. make all (+ android-apk when NDK/SDK available)
#   4. Start hub listen; capture peer_ek
#   5. Write lab/deploy-state.json + phone conf + edge-state.json
#   6. Print edge DNAT steps if public path
#   7. Launch enroll UI on http://127.0.0.1:8799/
#   8. USB phone -> Connect & Enroll in the browser
#
# Options (env):
#   SKIP_BUILD=1   NO_BROWSER=1   ENROLL_PORT=8799   HUB_PORT=47000
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$ROOT"

SKIP_BUILD=${SKIP_BUILD:-0}
NO_BROWSER=${NO_BROWSER:-0}
ENROLL_PORT=${ENROLL_PORT:-8799}
HUB_PORT_ENV=${HUB_PORT:-0}

step() { printf '\n=== %s ===\n' "$1"; }
ok() { printf '  OK  %s\n' "$1"; }
warn() { printf '  !!  %s\n' "$1"; }

ask() {
  # ask "prompt" "default" -> sets REPLY
  _p=$1
  _d=${2:-}
  if [ -n "$_d" ]; then
    printf '%s [%s]: ' "$_p" "$_d" >&2
    read -r _r || _r=
    if [ -z "$_r" ]; then REPLY=$_d; else REPLY=$_r; fi
  else
    while :; do
      printf '%s: ' "$_p" >&2
      read -r _r || _r=
      if [ -n "$_r" ]; then REPLY=$_r; break; fi
      warn "required"
    done
  fi
}

ask_yn() {
  _p=$1
  _def=${2:-Y}
  if [ "$_def" = Y ] || [ "$_def" = y ]; then _h=Y/n; else _h=y/N; fi
  printf '%s (%s): ' "$_p" "$_h" >&2
  read -r _r || _r=
  if [ -z "$_r" ]; then
    [ "$_def" = Y ] || [ "$_def" = y ]
    return $?
  fi
  case $_r in y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

ipv4_ok() {
  echo "$1" | grep -Eq '^[0-9]{1,3}(\.[0-9]{1,3}){3}$' || return 1
  OLDIFS=$IFS
  IFS=.
  # shellcheck disable=SC2086
  set -- $1
  IFS=$OLDIFS
  for o in "$1" "$2" "$3" "$4"; do
    [ "$o" -ge 0 ] 2>/dev/null && [ "$o" -le 255 ] 2>/dev/null || return 1
  done
  return 0
}

prev_get() {
  # prev_get key -> stdout value from lab/deploy-state.json if present
  _k=$1
  _f="$ROOT/lab/deploy-state.json"
  if [ ! -f "$_f" ] || ! command -v python3 >/dev/null 2>&1; then
    echo ""
    return 0
  fi
  python3 - "$_f" "$_k" <<'PY'
import json,sys
p,k=sys.argv[1],sys.argv[2]
try:
    j=json.load(open(p,encoding="utf-8"))
    v=j.get(k,"")
    print("" if v is None else str(v))
except Exception:
    print("")
PY
}

printf '\n  Athanor lab deploy (full path) - POSIX\n'
printf '  ----------------------------------------\n'
printf '  Builds hub + enroll console + stub APK, asks for hosts/IPs,\n'
printf '  starts hub, opens admin enroll site, then first USB phone.\n'
printf '  Windows: powershell -File .\\DEPLOY.ps1\n'
printf '  Docs: docs/LAB.md docs/EDGE.md docs/ENROLL.md\n\n'

step "Toolchain"
if ! command -v make >/dev/null 2>&1; then
  echo "make not on PATH" >&2
  exit 1
fi
if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
  echo "cc/gcc not on PATH" >&2
  exit 1
fi
ok "make=$(command -v make)"
ADB=${ADB:-adb}
if command -v "$ADB" >/dev/null 2>&1; then ok "adb present"; else warn "adb not found - install platform-tools before phone enroll"; fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 required for enroll UI + deploy-state JSON on POSIX" >&2
  exit 1
fi
ok "python3=$(command -v python3)"
if [ -f tools/android_env.sh ]; then
  # shellcheck disable=SC1091
  . tools/android_env.sh
fi

step "Environment (org hosts - not stored in git)"
printf '  hub_lan_ipv4   - this machine LAN address. Phones on Wi-Fi use this.\n'
printf '  public_ipv4    - WAN IP for cellular (optional if LAN-only).\n'
printf '  domain         - DNS name for edge panel (optional).\n'
printf '  edge_lan_ipv4  - DMZ/edge host on LAN (optional).\n'

if [ -f lab/deploy-state.json ]; then
  printf '  (found lab/deploy-state.json - defaults in [brackets])\n'
fi

ask "Hub LAN IPv4 (atnnode host)" "$(prev_get hub_lan_ipv4)"
HUB_LAN=$REPLY
ipv4_ok "$HUB_LAN" || { echo "bad hub_lan_ipv4: $HUB_LAN" >&2; exit 1; }

if [ "$HUB_PORT_ENV" -gt 0 ] 2>/dev/null; then
  _pd=$HUB_PORT_ENV
else
  _pd=$(prev_get peer_port)
  [ -n "$_pd" ] || _pd=47000
fi
ask "UDP mesh port" "$_pd"
PEER_PORT=$REPLY
case $PEER_PORT in
  ''|*[!0-9]*) echo "bad port" >&2; exit 1 ;;
esac
if [ "$PEER_PORT" -lt 1 ] || [ "$PEER_PORT" -gt 65535 ]; then
  echo "bad port" >&2
  exit 1
fi

_pm=$(prev_get path_mode)
[ -n "$_pm" ] || _pm=lan
ask "First phone path: lan (Wi-Fi to hub) or public (cellular via WAN)" "$_pm"
PATH_MODE=$(printf '%s' "$REPLY" | tr 'A-Z' 'a-z')
case $PATH_MODE in lan|public) ;; *) echo "path must be lan or public" >&2; exit 1 ;; esac

PUBLIC_IP=""
DOMAIN=""
EDGE_LAN=""
WANT_EDGE=0
if [ "$PATH_MODE" = public ]; then WANT_EDGE=1; fi
if [ "$WANT_EDGE" -eq 0 ]; then
  if ask_yn "Also configure public edge / cellular path now?" N; then WANT_EDGE=1; fi
fi
if [ "$WANT_EDGE" -eq 1 ]; then
  ask "Public WAN IPv4 (phones dial this off-LAN)" "$(prev_get public_ipv4)"
  PUBLIC_IP=$REPLY
  if [ -n "$PUBLIC_IP" ]; then ipv4_ok "$PUBLIC_IP" || { echo "bad public_ipv4" >&2; exit 1; }; fi
  ask "Org domain for edge panel (or blank)" "$(prev_get domain)"
  DOMAIN=$REPLY
  ask "Edge/DMZ host LAN IPv4 (or blank)" "$(prev_get edge_lan_ipv4)"
  EDGE_LAN=$REPLY
  if [ -n "$EDGE_LAN" ]; then ipv4_ok "$EDGE_LAN" || { echo "bad edge_lan_ipv4" >&2; exit 1; }; fi
fi

if [ "$PATH_MODE" = public ]; then
  [ -n "$PUBLIC_IP" ] || { echo "public path requires public_ipv4" >&2; exit 1; }
  PHONE_PEER=$PUBLIC_IP
else
  PHONE_PEER=$HUB_LAN
fi
ok "phone peer_ipv4 will be $PHONE_PEER (path=$PATH_MODE)"

# --- build ---
if [ "$SKIP_BUILD" != 1 ]; then
  step "Build (make all + lab APK on this host)"
  make all
  ok "native binaries (hub, enroll, sign, tests)"
  if [ -n "${ANDROID_NDK:-}" ] || [ -d "${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}/ndk" ]; then
    if make android-apk; then
      ok "android/athanor-lab.apk"
    else
      warn "android-apk failed - check ANDROID_SDK_ROOT / NDK / JDK (tools/android_env.sh)"
    fi
  else
    warn "Android SDK/NDK not found - skipping APK; set ANDROID_HOME then re-run"
  fi
else
  warn "SKIP_BUILD=1: using existing binaries"
fi

NODE=./atnnode
ENROLL=./atnenroll
[ -x "$NODE" ] || NODE=./atnnode.exe
[ -x "$ENROLL" ] || ENROLL=./atnenroll.exe
[ -x "$NODE" ] || { echo "atnnode binary missing" >&2; exit 1; }
[ -x "$ENROLL" ] || { echo "atnenroll binary missing" >&2; exit 1; }

# --- hub ---
step "Start hub (atnnode listen $PEER_PORT)"
mkdir -p lab
# stop previous hub on this port if we started it
if [ -f lab/hub-listen.pid ]; then
  kill "$(cat lab/hub-listen.pid)" 2>/dev/null || true
  rm -f lab/hub-listen.pid
fi
pkill -f "atnnode listen $PEER_PORT" 2>/dev/null || true
sleep 1
: > lab/hub-listen.log
: > lab/hub-listen.err
nohup "$NODE" listen "$PEER_PORT" >lab/hub-listen.log 2>lab/hub-listen.err &
echo $! > lab/hub-listen.pid
EK=""
i=0
while [ $i -lt 40 ]; do
  if grep -q '^peer_ek=' lab/hub-listen.log 2>/dev/null; then
    EK=$(grep '^peer_ek=' lab/hub-listen.log | head -n1 | sed 's/^peer_ek=//')
    break
  fi
  i=$((i + 1))
  sleep 0.25 2>/dev/null || sleep 1
done
if [ -z "$EK" ] || [ "${#EK}" -lt 64 ]; then
  tail -n 20 lab/hub-listen.err 2>/dev/null || true
  echo "hub did not print peer_ek - see lab/hub-listen.log" >&2
  exit 1
fi
ok "hub listening; peer_ek captured (${#EK} hex chars)"

step "Write local config (gitignored)"
export ATN_HUB_LAN=$HUB_LAN ATN_PUBLIC_IP=$PUBLIC_IP ATN_EDGE_LAN=$EDGE_LAN
export ATN_DOMAIN=$DOMAIN ATN_PEER_PORT=$PEER_PORT ATN_PEER_EK=$EK
export ATN_PATH_MODE=$PATH_MODE ATN_PHONE_PEER=$PHONE_PEER ATN_ENROLL_PORT=$ENROLL_PORT
python3 - <<'PY'
import json, os
from datetime import datetime, timezone
root = os.getcwd()
state = {
    "hub_lan_ipv4": os.environ["ATN_HUB_LAN"],
    "public_ipv4": os.environ.get("ATN_PUBLIC_IP", ""),
    "edge_lan_ipv4": os.environ.get("ATN_EDGE_LAN", ""),
    "domain": os.environ.get("ATN_DOMAIN", ""),
    "peer_port": os.environ["ATN_PEER_PORT"],
    "peer_ek": os.environ["ATN_PEER_EK"],
    "path_mode": os.environ["ATN_PATH_MODE"],
    "phone_peer_ipv4": os.environ["ATN_PHONE_PEER"],
    "updated": datetime.now(timezone.utc).isoformat(),
    "enroll_url": f"http://127.0.0.1:{os.environ['ATN_ENROLL_PORT']}/",
}
os.makedirs("lab", exist_ok=True)
with open("lab/deploy-state.json", "w", encoding="utf-8") as f:
    json.dump(state, f, indent=2)
    f.write("\n")
conf = (
    f"peer_ipv4={state['phone_peer_ipv4']}\n"
    f"peer_port={state['peer_port']}\n"
    f"peer_ek={state['peer_ek']}\n"
    "diag=1\nflush_mode=log_only\noutage_class=normal\n"
)
with open("lab/phone-atn-node.conf", "w", encoding="utf-8") as f:
    f.write(conf)
edge = os.path.join("lab", "edge", "www")
if os.path.isdir(edge):
    es = {
        "domain": state["domain"] or "mesh.example.org",
        "public_ipv4": state["public_ipv4"],
        "hub_lan_ipv4": state["hub_lan_ipv4"],
        "edge_lan_ipv4": state["edge_lan_ipv4"],
        "peer_port": state["peer_port"],
        "peer_ek": state["peer_ek"],
        "diag": "1",
        "flush_mode": "log_only",
        "outage_class": "normal",
        "updated": state["updated"],
    }
    with open(os.path.join(edge, "edge-state.json"), "w", encoding="utf-8") as f:
        json.dump(es, f, indent=2)
        f.write("\n")
print("wrote deploy-state + phone conf")
PY
ok "lab/deploy-state.json"
ok "lab/phone-atn-node.conf"
[ -f lab/edge/www/edge-state.json ] && ok "lab/edge/www/edge-state.json"

if [ -n "$PUBLIC_IP" ] || [ -n "$EDGE_LAN" ]; then
  step "Public edge (run on the DMZ/edge Linux host)"
  printf '  1. Copy lab/edge/atn-udp-forward.sh and lab/edge/www/ to the edge host.\n'
  printf '  2. Install Apache/nginx alias for /atn/ (see lab/edge/apache-atn.conf).\n'
  printf '  3. Router: forward UDP %s to edge host.\n' "$PEER_PORT"
  printf '  4. On edge host:\n\n'
  printf '       sudo HUB_IP=%s HUB_PORT=%s bash atn-udp-forward.sh install\n\n' "$HUB_LAN" "$PEER_PORT"
  printf '  5. Open https://<domain>/atn/ to review/save org settings.\n'
fi

step "Launch enroll admin website"
if [ -f lab/enroll-serve.pid ]; then
  kill "$(cat lab/enroll-serve.pid)" 2>/dev/null || true
  rm -f lab/enroll-serve.pid
fi
# enroll serve blocks in child; run via nohup of the shell script directly so we keep control
nohup sh tools/enroll-console.sh "$ENROLL_PORT" >lab/enroll-serve.log 2>&1 &
echo $! > lab/enroll-serve.pid
sleep 2
URL="http://127.0.0.1:${ENROLL_PORT}/"
if [ "$NO_BROWSER" != 1 ]; then
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$URL" >/dev/null 2>&1 || true
  elif command -v open >/dev/null 2>&1; then
    open "$URL" >/dev/null 2>&1 || true
  else
    warn "open browser manually: $URL"
  fi
fi
ok "admin UI: $URL (loopback only)"

step "First mobile deploy"
printf '  1. Phone: Developer options -> USB debugging; plug into this machine.\n'
printf '  2. Accept the USB debugging prompt.\n'
printf '  3. Admin page %s — hub fields pre-fill from deploy-state.\n' "$URL"
printf '       peer_ipv4=%s  peer_port=%s\n' "$PHONE_PEER" "$PEER_PORT"
printf '       Click Connect and Enroll\n'
printf '  4. Activate Device Admin if asked; wait for MESH UP.\n\n'
printf '  Hub log: lab/hub-listen.log   State: lab/deploy-state.json\n'
printf '  Stop:    kill $(cat lab/hub-listen.pid lab/enroll-serve.pid)\n'
printf '  Re-run:  ./DEPLOY.sh\n'
printf '  Keep hub + enroll UI running while testing.\n'

if command -v "$ADB" >/dev/null 2>&1; then
  if "$ADB" devices 2>/dev/null | grep -Eq '^[A-Za-z0-9]+\s+device$'; then
    ok "USB device visible - use Connect and Enroll in the browser"
  else
    warn "no adb device yet - plug the phone, then enroll in the browser"
  fi
fi

printf '\nDeploy ready.\n'
