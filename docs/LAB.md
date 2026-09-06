# Lab: phone + single hub (DEC-0038)

Connectivity soak **without** Knox Partner jar. Stub APK is allowed.
USB charge-only / password Knox policy are **release** only.

This is **not** the rejected Gradle/Node/`USE_REAL_KNOX` guide (DEC-0030).
Build stays Makefile + `android/stubs` + optional `vendor/knox/knoxsdk.jar`.

## Architecture (one hub)

```
PC hub:  atnnode listen <port>     (INADDR_ANY, prints peer_ek)
Phone:   stub APK → AtnLabActivity → AtnDaemonService
         filesDir/atn-node.conf → ML-KEM HS → ESTABLISHED
USB:     adb install + logcat only (mesh runs over Wi-Fi / LAN)
```

## 1. Hub (PC on LAN)

```bat
set PATH=YOUR_GCC_BIN;%PATH%
make atnnode.exe
.\atnnode.exe listen 47000
```

Copy the printed `peer_port` / `peer_ek`. Set `peer_ipv4` to this PC's
LAN address (e.g. `YOUR_HUB_LAN_IPV4`), **not** 127.0.0.1 (phone is another host).

Leave the process running. After **ESTABLISHED**, the hub only echoes;
restart `listen` (new `peer_ek`) before a fresh phone soak.

## 2. Phone conf

Create `lab/phone-atn-node.conf` (gitignored local copy OK):

```
peer_ipv4=YOUR_HUB_LAN_IPV4
peer_port=47000
peer_ek=<hex from listen>
diag=1
flush_mode=log_only
```

`diag=1` keeps keys on DEAD silence during soak (DEC-0027).

## 3. Build + USB install

```bat
make android-apk
adb install -r android\athanor-lab.apk
adb push lab\phone-atn-node.conf /data/local/tmp/atn-node.conf
adb shell run-as com.athanor.daemon mkdir files
adb shell "run-as com.athanor.daemon sh -c \"cat > files/atn-node.conf\"" < lab\phone-atn-node.conf
adb shell am start -n com.athanor.daemon/.AtnLabActivity --ez autostart true
```

On screen you should see live `state=ESTABLISHED` and **MESH UP**.
Buttons: **Start / reconnect mesh**, **Send lab ping**.

```bat
adb logcat -s atn-daemon:I atn-lab:I atn-knox:W
```

Hub prints `ESTABLISHED`, then `recv 4` when you tap ping.

## 3b. Lab enroll console (DEC-0042)

Preferred operator path: local loopback UI — policy + phone roster label +
**Connect & Enroll** (USB install + conf + Device Admin prompt).

See [`ENROLL.md`](ENROLL.md). Short form:

```bat
make atnenroll.exe atnsign.exe android-apk
.\atnenroll.exe serve 8787
```

Open 127.0.0.1:8787 in a browser - keep the page open; plug phone (USB debugging);
fill hub `peer_*` + phone number label; click **Connect & Enroll**.

Receipts: `lab/enrollments/` (gitignored). Local ML-DSA sign via `atnsign`
(not air-gapped — release beta later). Knox DO still T-0400.

## 4. Lab situations (stub phone + one hub)

| Case | Expect | Skip / note |
|---|---|---|
| Happy path HS | UI ESTABLISHED + hub ESTABLISHED | |
| Lab ping | hub `recv 4` | |
| Start while up | log `already ESTABLISHED` | |
| Hub kill / airplane / no Wi‑Fi >30s | **BOOM only after mesh joined** (DEC-0041 narrowed) | join first; pre-join HANDSHAKE does not BOOM |
| **Locked phone: wrong PIN/password ×5** | **BOOM** via Device Admin `watch-login` (DEC-0040) | Activate admin in app first; use PIN not fingerprint-only |
| Optional in-app 2FA code ×5 | **BOOM** (app soak) | not a substitute for lock screen |
| Start/reconnect | clears lab BOOM for another cycle | |
| USB charge-only / real Faraday bag / Knox | | release / T-0400 / ISS-0019 |

### Lock-screen K=5 recipe (DEC-0040)

1. Open Athanor Lab → **Enable lock-screen watch (Device Admin)** → Activate.
2. Confirm status shows `deviceAdmin=ON`.
3. Press power to lock the phone.
4. Wake lock screen; enter the **wrong PIN/password** five times
   (biometric-only fails may not count on all OEMs).
5. Notification / next unlock of the app: **BOOM phone is dead now**
   (`device unlock fail x5`). Keys kept with `log_only`.
6. **Start / reconnect** resets lab BOOM for another cycle.

## 5. What stub does / does not

| Does | Does not |
|---|---|
| Real ML-KEM-1024 + AEAD UDP mesh | Knox RestrictionPolicy / USB charge-only |
| Keystore wrap of device keys | Device Owner enroll |
| Diag `log_only` flush | SoT REQ-4.x `[X]` |
| `knoxStub=true` in logcat | Pretend enrolled Knox |

## 6. Later: drop-in Knox

1. `vendor/knox/knoxsdk.jar`
2. `make android-apk` → REAL classpath
3. Then release gates: Device/Profile Owner, USB charge-only, password K=5

## Firewall

Allow UDP on the hub listen port (inbound) on the PC firewall.
