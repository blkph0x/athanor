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

Until that file exists, `make android` builds a **stub** APK/classes
that refuse to apply policy. Do not flash a stub build.

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

## Native

`android/jni/` links `src/crypto/*.c`, `src/tun/atn_tun.c`,
`src/auth/atn_2fa.c`, `src/hb/atn_hb.c` into `libatn.so`.
Java talks to it with JNI. No OkHttp, no Play services.
