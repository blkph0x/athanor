# Knox attach (DEC-0015)

REQ-4.x. We do not patch TIMA. We call Samsung’s published SDK.

## Toolchain on this builder

| Piece | Where | Notes |
|---|---|---|
| JDK | `C:\Program Files\Java\jdk-19` (sdkmanager) and Microsoft JDK 11 | Need 17+ for current sdkmanager |
| Android SDK | `%LOCALAPPDATA%\Android\Sdk` | already had platform 31 + adb |
| cmdline-tools | `Sdk\cmdline-tools\latest` | installed this session from Google zip 14742923 |
| NDK | `Sdk\ndk\27.3.13750724` (r27d) | LTS; aarch64-linux-android |
| knoxsdk.jar | `vendor/knox/knoxsdk.jar` | **not in git** — Partner Program download |

LAN DNS on this host (`mydevice.lan` / `10.1.1.1`) does not resolve
`dl.google.com` or `github.com`. Installers used `--resolve` against
1.1.1.1 answers. That is a builder workaround, not a product resolver.

## Getting knoxsdk.jar (human step)

Samsung will not give us the zip without a Knox Developer account:

1. https://partner.samsungknox.com/ — Become a Partner / sign in
2. Dashboard → SDK Tools → SDK Downloads → Knox SDK
3. Accept the SDK Agreement
4. Copy `knoxsdk.jar` to `vendor/knox/knoxsdk.jar`
5. License key goes in a local file that is gitignored, never in source

Until that file exists, `make android-java` compiles **in-tree stubs**
(`ATN_STUB=true`, every policy call throws). Dropping the real jar at
the path above is a classpath switch (DEC-0019). Do not flash a stub
build. The daemon logs `knoxStub=true` so we cannot pretend policy stuck.

## APIs we are allowed to call (cited)

USB charge-only (https://docs.samsungknox.com/dev/knox-sdk/kbas/how-to-restrict-users-from-accessing-and-sharing-device-data):

- `RestrictionPolicy.setUsbMediaPlayerAvailability(false)` — MTP off
- `RestrictionPolicy.setUsbDebuggingEnabled(false)` — USB data/debug off
- `RestrictionPolicy.allowUsbHostStorage(false)` — OTG storage off
- `RestrictionPolicy.setUsbTethering(false)`

Password / biometric (Knox PasswordPolicy + AOSP DPM, same names in
Knox docs):

- `DevicePolicyManager.setPasswordQuality(..., PASSWORD_QUALITY_ALPHANUMERIC)`
- `DevicePolicyManager.setPasswordMinimumLength(..., 12)`
- `PasswordPolicy.setBiometricAuthenticationEnabled(BIOMETRIC_AUTHENTICATION_FINGERPRINT \| IRIS, true)` as convenience **after** quality is set

Entry:

- `EnterpriseDeviceManager.getInstance(context)`
- `edm.getRestrictionPolicy()` / `edm.getPasswordPolicy()`

Android 15+: app must be Device Owner or Profile Owner.

## TIMA vs Android Keystore (DEC-0016)

`TimaKeystore.enableTimaKeystore` is **deprecated API 33** and does not
work on Android 12+ / Knox 3.8. S24–S26 are Android 14+. We do not call
it. Device keys go in `AndroidKeyStore` alias `atn-device` (AES-256,
StrongBox if present, else TEE). RAM copies live in `atn_dmon` and are
wiped by `atn_dmon_flush`.

## Native

`android/jni/` links crypto, tun, 2fa, hb, dmon into `libatn.so`.
Java talks to it with JNI (`AtnNative.tun*`, DEC-0020). No OkHttp, no
Play services. Peer IPv4 is supplied by `filesDir/atn-node.conf`
(`peer_ipv4` / `peer_port` / `peer_ek`), not baked in (DEC-0021).
JNI `tunBind` uses `INADDR_ANY`. INTERNET permission is for that UDP
socket, not a CDN.

Lab PC: `atnnode listen [port]` prints `peer_port` and `peer_ek`. Put
those plus this machine's IPv4 into the phone's `atn-node.conf`. Missing
or incomplete file means the daemon does not connect.

## Daemon session (DEC-0017)

Native `atn_dmon` holds RAM copies. Flush zeros them and Java deletes
`atn-wrap.bin` so a reboot cannot restore cluster keys.

| Trigger | What happens |
|---|---|
| Heartbeat UNTRUSTED/DEAD (N=3 silent 60s buckets) | `atn_dmon_flush` |
| 2FA `ATN_ERR_LOCKOUT` (5 fails) | `atn_dmon_flush` |
| DPM password fail count ≥ 5 | flush + delete wrap + `lockNow` |
| `ACTION_POWER_CONNECTED` | re-assert USB charge-only |
| Service `onDestroy` | RAM flush only (wrap file kept) |

Password DPM (AOSP, Knox DA-deprecation mirrors):

- `setPasswordQuality(..., PASSWORD_QUALITY_ALPHANUMERIC)`
- `setPasswordMinimumLength(..., 12)`
- `setPasswordMinimumLetters(..., 1)`
- `setPasswordMinimumNumeric(..., 1)`

Factory-reset-on-fail (`setMaximumFailedPasswordsForWipe`) is **not**
enabled. Wrap format: NIST SP 800-38D 12-byte IV + AES-GCM ciphertext.
