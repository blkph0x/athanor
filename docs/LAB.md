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
USB:     adb install + logcat only (mesh runs over Wi‑Fi / LAN)
```

## 1. Hub (PC on LAN)

```bat
set PATH=YOUR_GCC_BIN;%PATH%
make atnnode.exe
.\atnnode.exe listen 47000
```

Copy the printed `peer_port` / `peer_ek`. Set `peer_ipv4` to this PC’s
LAN address (e.g. `YOUR_HUB_LAN_IPV4`), **not** 127.0.0.1 (phone is another host).

Leave the process running.

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
adb shell run-as com.athanor.daemon sh -c "cat > files/atn-node.conf" < lab\phone-atn-node.conf
adb shell am start -n com.athanor.daemon/.AtnLabActivity
```

Tap **Start mesh daemon**. Watch:

```bat
adb logcat -s atn-daemon:I atn-lab:I atn-knox:W
```

Hub should print `ESTABLISHED`. Phone log: `lab tun rc=0` then pumps.

## 4. What stub does / does not

| Does | Does not |
|---|---|
| Real ML-KEM-1024 + AEAD UDP mesh | Knox RestrictionPolicy / USB charge-only |
| Keystore wrap of device keys | Device Owner enroll |
| Diag `log_only` flush | SoT REQ-4.x `[X]` |
| `knoxStub=true` in logcat | Pretend enrolled Knox |

## 5. Later: drop-in Knox

1. `vendor/knox/knoxsdk.jar`
2. `make android-apk` → REAL classpath
3. Then release gates: Device/Profile Owner, USB charge-only, password K=5

## Firewall

Allow UDP on the hub listen port (inbound) on the PC firewall.
