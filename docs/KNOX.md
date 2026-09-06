# Knox attach (DEC-0015 / DEC-0019 / DEC-0030)

REQ-4.x. We do not patch TIMA. We call Samsung’s published SDK.

## Test-without-jar → drop jar later (DEC-0030)

We already compile a **testing** Android Java tree **without**
`knoxsdk.jar`. Do not invent a second build system.

| Stage | What you have | What you run | Flash phone? |
|---|---|---|---|
| Lab / CI compile | No jar | `make android-java` → **STUB BUILD** | Optional |
| Lab connectivity (DEC-0038) | No jar + APK | `make android-apk` → USB `adb install` | **Yes (stub)** |
| Diag soak (PC hub) | `diag=1` conf (DEC-0027) | `atnnode listen` + phone | Yes (stub) |
| Partner jar lands | `vendor/knox/knoxsdk.jar` | `make android-apk` → **REAL** | Still only after enroll for SoT |
| Enrolled S24–S26 | jar + Device/Profile Owner | device gates REQ-4.x | Yes (release) |

### Exact paths (this repo — not “app/libs”)

| Role | Path |
|---|---|
| Real SDK (gitignored) | `vendor/knox/knoxsdk.jar` |
| How to get it | `vendor/knox/README.md` + Partner portal |
| Compile-only stubs | `android/stubs/com/samsung/android/knox/**` |
| Product daemon Java | `android/java/com/athanor/daemon/**` |
| Stub detector | `AtnKnoxBuild.isStub()` → true iff stub `ATN_STUB` field exists |
| Policy call sites | `AtnKnoxPolicy.java` (same imports forever) |
| Native mesh | `android/jni/` + `make android-so` → `android/libatn.so` |
| Lab peer conf | `filesDir/atn-node.conf` (DEC-0021/0027/0028/0029) |

### Why we keep `import com.samsung.android.knox…`

Stubs use the **same package and class names** as the Partner jar.
Product code always imports them. When the jar appears, the Makefile
drops stubs from the compile line and puts the jar on `-classpath`.
No uncomment-reflection dance. No Gradle `USE_REAL_KNOX`.

### What we reject from foreign “familymanager” guides

- Gradle + AndroidX / Material / npm / Node Express (SoT: zero outside libs)
- `app/libs/knox_sdk.jar` naming (ours is `vendor/knox/knoxsdk.jar`)
- Forbidding Samsung imports until the jar lands (breaks our stub model)
- HttpURLConnection enroll/poll “command queue” servers
- `DEVICE_BRICK_LOCK` / wiping or locking devices we do not own

Mesh control plane is **our** UDP tunnel + heartbeat + admin console
(`atnnode` / `atnhttp`), not a Node registry on port 8080.

## Toolchain on this builder

| Piece | Where | Notes |
|---|---|---|
| JDK | `C:\Program Files\Java\jdk-19` (sdkmanager) and Microsoft JDK 11 | Need 17+ for current sdkmanager |
| Android SDK | `%LOCALAPPDATA%\Android\Sdk` | platform 31 + adb |
| cmdline-tools | `Sdk\cmdline-tools\latest` | zip 14742923 |
| NDK | `Sdk\ndk\27.3.13750724` (r27d) | aarch64-linux-android |
| knoxsdk.jar | `vendor/knox/knoxsdk.jar` | **not in git** — Partner download |
| android.jar | `Sdk\platforms\android-31\android.jar` | javac bootclasspath |

LAN DNS on this host may not resolve
`dl.google.com` / `github.com`. Installers used `--resolve` against
1.1.1.1. Builder workaround only — not a product resolver.

## Getting knoxsdk.jar (human step)

1. https://partner.samsungknox.com/ — Become a Partner / sign in
2. Dashboard → SDK Tools → SDK Downloads → Knox SDK
3. Accept the SDK Agreement
4. Copy the jar to **`vendor/knox/knoxsdk.jar`** (exact path)
5. License key → gitignored local file, never in source
6. `make android-java` — must print `REAL knoxsdk.jar: …`
7. Confirm `AtnKnoxBuild.isStub()` is false on that classpath

Until step 4, `make android-java` prints `STUB BUILD` and compiles
`android/stubs` (`ATN_STUB=true`). Policy APIs throw
`UnsupportedOperationException`. **Lab stub APK (DEC-0038)** may be
USB-installed for mesh connectivity; USB/password Knox policy is
**skipped** on stub so adb survives. Do **not** claim enrolled Knox or
flip SoT 4.x until the real jar + Device/Profile Owner gates.

## Makefile contract

```
make android-java
  if vendor/knox/knoxsdk.jar missing:
    javac stubs + daemon  →  STUB BUILD
  else:
    javac -classpath knoxsdk.jar daemon only  →  REAL

make android-so    # NDK libatn.so (mesh native)
make android       # so + java
```

Measured on the builder when the jar is absent: stub path only
(ISS-0016). Real-jar compile is unmeasured here until the file exists.

## APIs we are allowed to call (cited)

USB charge-only (Samsung KBA how-to-restrict USB / data sharing):

- `RestrictionPolicy.setUsbMediaPlayerAvailability(false)` — MTP off
- `RestrictionPolicy.setUsbDebuggingEnabled(false)` — USB data/debug off
- `RestrictionPolicy.allowUsbHostStorage(false)` — OTG storage off
- `RestrictionPolicy.setUsbTethering(false)`

Password / biometric (Knox PasswordPolicy + AOSP DPM):

- `DevicePolicyManager.setPasswordQuality(..., PASSWORD_QUALITY_ALPHANUMERIC)`
- `DevicePolicyManager.setPasswordMinimumLength(..., 12)`
- `PasswordPolicy.setBiometricAuthenticationEnabled(BIOMETRIC_AUTHENTICATION_FINGERPRINT \| IRIS, true)` after quality is set

Entry:

- `EnterpriseDeviceManager.getInstance(context)`
- `edm.getRestrictionPolicy()` / `edm.getPasswordPolicy()`

Android 15+: app must be Device Owner or Profile Owner.

## TIMA vs Android Keystore (DEC-0016)

`TimaKeystore.enableTimaKeystore` is **deprecated API 33** and does not
work on Android 12+ / Knox 3.8. S24–S26 are Android 14+. We do not call
it. Device keys go in `AndroidKeyStore` alias `atn-device` (AES-256,
StrongBox if present, else TEE). RAM copies live in `atn_dmon` and are
wiped by `atn_dmon_flush` (or LOG_ONLY under DEC-0027 diag).

## Native mesh (not HTTP poll)

`android/jni/` links crypto, tun, 2fa, hb, dmon into `libatn.so`.
Java talks via JNI (`AtnNative.tun*`, DEC-0020). No OkHttp, no Play
services. Peer list: `atn-node.conf` (`peer_*` + optional `hub2_*`…`hub16_*`,
DEC-0028/0032). Heartbeat IPv4 (DEC-0022).

Lab PC: `atnnode listen [port]` prints `peer_port` / `peer_ek`. Put
those plus this machine’s IPv4 into the phone conf. Incomplete file →
do not connect.

## Daemon session (DEC-0017 / 0025 / 0027 / 0029)

Native `atn_dmon` holds RAM copies. Production ZEROIZE deletes wrap so
reboot cannot restore cluster keys. Diag `flush_mode=log_only` counts
only (DEC-0027).

| Trigger | What happens |
|---|---|
| Heartbeat **DEAD** (after N+G; HOLD can cancel) | flush per DEC-0027 mode |
| `outage_class=blackout\|maintenance` | local HOLD — no DEAD wipe (DEC-0029) |
| 2FA `ATN_ERR_LOCKOUT` (5 fails) | flush per mode |
| DPM password fail count ≥ 5 | flush + delete wrap + `lockNow` (prod) |
| `ACTION_POWER_CONNECTED` | re-assert USB charge-only |
| Service `onDestroy` | RAM flush only (wrap file kept) |

Factory-reset-on-fail (`setMaximumFailedPasswordsForWipe`) is **not**
enabled. Wrap format: NIST SP 800-38D 12-byte IV + AES-GCM ciphertext.
