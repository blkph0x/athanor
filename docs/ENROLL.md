# Lab enroll console (DEC-0042)

Local-only operator UI to set hub policy, attach a **phone number roster
label**, and **Connect & Enroll** a USB-debuggable phone (install stub APK +
push `atn-node.conf` + prompt Device Admin).

**Preferred start:** [`../DEPLOY`](../DEPLOY) / `DEPLOY.ps1` (Windows) /
`DEPLOY.sh` (Linux/macOS) — asks hosts/IPs, builds, starts hub + this UI,
pre-fills from `lab/org.local.json` / `lab/deploy-state.json` (gitignored).

One-time on a new clone: copy `lab/org.local.json.example` → `lab/org.local.json`,
fill real IPs, then `tools/install-git-hooks` so push is blocked if org values
leak into tracked files.

**Network-wide policy (DEC-0045 / DEC-0046):** **Save network policy** writes
`lab/org-policy.conf` (boom silence, password-fail K, biometrics off,
alphanumeric min length, USB data block, leak-password deny check). Hub
pushes to phones; phones store **Keystore-wrapped** `atn-policy.bin` and
wipe plaintext after apply. Rebuild deny hashes from rockyou locally:

`python tools/gen_pwd_deny.py --rockyou /path/to/rockyou.txt`

**Compromise vote (DEC-0047):** **Start compromise vote** on an enrolled
roster label → `lab/compromise-vote.conf`. Cast **Vote YES/NO** (or phone
lab buttons). Quorum YES **or** timeout → hub sends boom (`C`+`action=boom`).
Stub lab proves the signal (flush + UI dead); factory wipe waits **T-0400**.

**Mesh update (DEC-0048):** **Publish update** copies a local hub path to
`lab/updates/payload.bin`, writes `announce.conf`, bumps `update_id`. Hub
streams announce+chunks as tunnel DATA (`U` / `UC`) to the connected phone
and to peer hubs (`lab/hub-peers.conf`) — **encrypted tunnel only**, never
HTTP/URL download. Phone stages verified APK/site under app files; install
may still need USB/PackageInstaller until Knox/DO (T-0400).

This is **not** mesh `atnhttp` (browsers cannot speak DEC-0009 tunnel HTTP —
ISS-0009). Air-gapped signing is **release beta**. Real Knox Device Owner /
USB charge-only waits on **T-0400** (`knoxsdk.jar`).

## Start (manual)

```bat
set PATH=%GCC_BIN%;%PATH%
make atnenroll.exe atnsign.exe
make android-apk
.\atnenroll.exe serve 8799
```

Open **127.0.0.1:8799** in a browser (loopback only). Keep the page open — USB
device status refreshes; **Connect & Enroll** stays available for each phone.

| Host | Launch |
|---|---|
| Windows | `.\atnenroll.exe serve 8799` or `tools\enroll-console.ps1` |
| Linux / macOS | `./atnenroll serve 8799` or `sh tools/enroll-console.sh 8799` (needs `python3`) |

## Form fields

| Field | Meaning |
|---|---|
| Phone number | Local roster label only (E.164-ish). Never dialed / SMS (ISS-0020). |
| peer_ipv4 / peer_port / peer_ek | Hub from `atnnode listen` (PC LAN IP, not 127.0.0.1). |
| diag / flush_mode / outage_class | Lab policy written into `atn-node.conf`. |

## What Connect & Enroll does

1. Validates inputs; requires `android/athanor-lab.apk` and one `adb` device.
2. Writes `lab/enrollments/<id>/` (gitignored) + copies active
   `lab/phone-atn-node.conf`.
3. `adb install -r` stub APK; push conf into app `files/atn-node.conf`.
4. Starts `AtnLabActivity` with `autostart` + `request_admin`.
5. Writes `enrollment.txt`; if `atnsign.exe` is built, ML-DSA-signs it with
   `lab/enroll-keys/` (local host — **not** air-gapped).

On the phone: activate Device Admin if prompted, then confirm **MESH UP**.

## Related

- Manual USB steps: [`LAB.md`](LAB.md)
- Decision: DEC-0042 in [`DECISIONS.md`](DECISIONS.md)
