# Lab enroll console (DEC-0042)

Local-only operator UI to set hub policy, attach a **phone number roster
label**, and **Connect & Enroll** a USB-debuggable phone (install stub APK +
push `atn-node.conf` + prompt Device Admin).

This is **not** mesh `atnhttp` (browsers cannot speak DEC-0009 tunnel HTTP —
ISS-0009). Air-gapped signing is **release beta**. Real Knox Device Owner /
USB charge-only waits on **T-0400** (`knoxsdk.jar`).

## Start

```bat
set PATH=YOUR_GCC_BIN;%PATH%
make atnenroll.exe atnsign.exe
make android-apk
.\atnenroll.exe serve 8799
```

Open **127.0.0.1:8799** in a browser (loopback only). Keep the page open — USB
device status refreshes; **Connect & Enroll** stays available for each phone.

Or: `powershell -NoProfile -File tools\enroll-console.ps1 -Port 8799`

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
