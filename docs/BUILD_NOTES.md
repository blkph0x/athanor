# Build notes

Newest entry at the top. Record facts, not hopes.

---

## BN-0040 — DEC-0042 lab enroll console (2026-09-06)

- **UI:** `atnenroll serve [port]` → PowerShell `HttpListener` on
  **127.0.0.1 only** (default 8799; 8787 often taken on this builder). Form: phone roster label, hub
  `peer_*`, policy; button **Connect & Enroll** (page stays live; `/status`
  polls USB).
- **Action:** write `lab/enrollments/<id>/`, `adb install -r` stub APK,
  push `atn-node.conf`, start Activity with `autostart` + `request_admin`.
  Optional local ML-DSA receipt via `atnsign` + `lab/enroll-keys/` (not
  air-gapped).
- **Not yet:** air-gap sign host; Knox DO / USB charge-only (T-0400).
- **Docs:** `docs/ENROLL.md`, LAB §3b.

---

## BN-0039 — DEC-0041 narrowed: join-gated unreachable BOOM (2026-09-06)

- **Policy:** airplane/hub-silence BOOM only after `sawEstablished`.
  Pre-join HANDSHAKE does not BOOM. Lock-screen K=5 unchanged.
- **Retest:** hub `listen 47000` + S24 → UI ESTABLISHED / MESH UP;
  post-join airplane BOOM confirmed by operator.
- **Waiting:** T-0400 `knoxsdk.jar`. SoT 4.x / 5.3 still `[ ]`.

---

## BN-0038 — DEC-0041 airplane/handshake BOOM (2026-09-06)

- **Fix:** Lab soak arms on tunnel start; unreachable timer added.
- **Superseded in part by BN-0039:** pre-join handshake BOOM removed
  per operator (join first, then silence/airplane).
- **SoT:** unchanged.

---

## BN-0037 — DEC-0040 lock-screen watch (2026-09-06)

- **Change:** Lab APK prompts Device Admin; stub `onPasswordFailed` →
  BOOM at fail count ≥5 without wrap delete.
- **Operator proof:** Activate admin → lock → wrong PIN ×5 → BOOM notif.
- **SoT:** REQ-4.2 still `[ ]`. Biometric-only may not increment count.

---

## BN-0036 — DEC-0039 lab BOOM on S24 (2026-09-06)

- **Device:** SM-S901E; stub APK; `diag=1` / `flush_mode=log_only`.
- **Silence:** hub kill → UI `BOOM phone is dead now` after ~30s;
  log `LAB BOOM: hub silence >30s (lab)`.
- **Codes:** five wrong Submit → `code fail #5` verifyRc=8 (LOCKOUT) →
  `BOOM phone is dead now` / reason `wrong code x5 (lab)`.
- **SoT:** REQ-5.3 Faraday still `[ ]`. Keys kept (log_only).

---

## BN-0035 — Lab UI + phone situation matrix (2026-09-06)

- **Device:** SM-S901E; stub APK reinstall; UI shows `state=ESTABLISHED`.
- **Hub:** `atnnode listen 47000` @ YOUR_HUB_LAN_IPV4; ping → `recv 4`.
- **Matrix:** happy/ping/reconnect-up/restore PASS; hub-kill dead-peer
  detect open; wrong-ek phone stays non-ESTABLISHED.
- **SoT:** 4.x still `[ ]`. No Knox.

---

## BN-0034 — S24 lab soak ESTABLISHED (2026-09-06)

- **Device:** SM-S901E Android 16; stub APK; `knoxStub=true`.
- **Hub:** `atnnode listen 47000` on YOUR_HUB_LAN_IPV4 → **ESTABLISHED**.
- **Fixes in tip:** `AtnKnoxBuildFlags`; `libatn.so` links `atn_cfg.c`;
  FGS `dataSync`. Firewall UDP 47000 allowed.
- **SoT:** 4.x still `[ ]` (no Knox enroll).

---

## BN-0033 — DEC-0038 stub lab APK path (2026-09-06)

- **Host:** Windows x86_64; Android SDK build-tools 34.0.0; NDK r27d.
- **Policy:** USB-installable stub APK for phone↔`atnnode` hub soak;
  USB charge-only skipped on stub; no Gradle.
- **Commands:** `make android-apk` → `android/athanor-lab.apk`.
- **SoT:** 4.x still `[ ]`. T-0805 done; T-0401b open for live soak.

---

## BN-0032 — DEC-0037 lab online / release NIC-down (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Network:** default route **up** (intentional lab posture).
- **Policy:** testing builds do not air-gap this host; ISS-0021 is
  release-only. No Admin elevation required for ordinary gates.
- **SoT:** unchanged. T-0804 done.

---

## BN-0031 — DEC-0036 form percent-decode (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **`make test`:** ALL PASSED including `test_http` form_get gates.
- **Network:** default route still up (T-0601 / ISS-0021 not measured;
  `Remove-NetRoute` Access denied without elevation).
- **SoT:** unchanged. T-0803 done; ISS-0011 closed.

---

## BN-0030 — DEC-0035 PQ tunnel rekey (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **`make test`:** ALL PASSED including `test_tun` rekey echo.
- **Wire:** types 6/7; `atn_tun_rekey_send`; ISS-0008 closed.
- **SoT:** unchanged. T-0801 done.

---

## BN-0029 — DEC-0033/0034 crypto floor + console outage 2FA (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **`make test`:** ALL PASSED; `test_recipe` URL + crypto floor;
  `test_http` outage blackout via 2FA.
- **Docs:** `docs/CRYPTO.md`, `docs/ESSENTIAL8.md`; README posture board.
- **SoT:** unchanged. T-0800 + T-0802 done.

---

## BN-0028 — DEC-0032 repl/hub cap 16 + atnnode multi-hub connect (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **`make test`:** ALL PASSED (`test_cfg` hub2..10 / hub17 reject;
  `test_repl` init 16 / reject 17; `ATN_REPL_MAX_REC` fits tunnel;
  `atnnode demo` OK with dmon-linked CLI).
- **README:** pipeline + testing-phase board updated.
- **SoT:** 4.x/5.x/6.x stay `[ ]`. T-0702 + T-0703 done.

---

## BN-0027 — DEC-0031 hub failover D-08 (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **`make test`:** ALL PASSED including `test_hub_failover`.
- **SoT:** 4.x/5.x/6.x stay `[ ]`. T-0701 done; T-0702 open.

---

## BN-0026 — DEC-0030 stub Knox path documented (2026-09-06)

- **Host:** Windows x86_64
- **`vendor/knox/knoxsdk.jar`:** absent
- **`make android-java`:** `STUB BUILD: … (DEC-0030; not a device build)`
  javac exit 0 (deprecation notes on DPM APIs only).
- **SoT 4.1:** still `[ ]`.

---

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Network:** default route present (not ISS-0021).
- **`make test`:** ALL PASSED.
  - `test_cfg`: hub2 parse/count/get; `log_only` without `diag` rejected;
    `outage_class=blackout`.
  - `test_dmon`: diag `log_only` DEAD keeps keys + `flush_log_count`;
    blackout class silence keeps LIVE/keys; production ZEROIZE path
    unchanged.
  - `test_sign`: `diag=0` / `diag=1` in ATN-REPORT-1.
- **SoT:** 4.x/5.x/6.x stay `[ ]`. ISS-0022/0023 narrowed closed.

---

## BN-0024 — DEC-0026 operator client + Poly1305 wall + export-tree (2026-09-06)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Network:** default route `0.0.0.0/0` → `10.1.1.1` on `Ethernet 10`.
  `Test-Connection 1.1.1.1` quiet=True. **Not** a disconnected build
  (ISS-0021).
- **`make test`:** ALL PASSED.
  - `note poly1305 wall N=50000 msg=1024 key_a_ms=40 key_b_ms=40
    (ISS-0003; not a CT proof)` then `ok poly1305 timing ran`
  - `atnhttp demo: GET / and GET /admin + conf reload OK (DEC-0009/0026)`
- **`make export-tree`:** `export ok ...\export\athanor-src` with
  `SOURCE_OF_TRUTH.md`, `CAUSE_EFFECT_MAP.md`, `docs/ISOLATION.md`.
- **SoT:** 4.x / 5.x / 6.x stay `[ ]`. ISS-0009 narrowed closed.
  ISS-0003 remains open (no aarch64/S24 timing yet).

---

## BN-0023 — org failsafe / mesh console (DEC-0025) (2026-09-04)

- **Host:** Windows x86_64
- **`make test`:** ALL PASSED (verified 2026-09-06 before DEC-0026).
  Gates: hb retrieve/hold, dmon grace→DEAD flush, HTTP `mesh roster`.
- **Prior note:** entry was written pending the run; this completes it.

---

## BN-0022 — keep-alive / DNS TCP / connect (DEC-0024) (2026-09-04)

- **Host:** Windows x86_64
- **`make test`:** ALL PASSED. New gates: HTTP `parse ka`/`parse close`,
  two-GET keep-alive, DNS `tcp port` + `tcp A`, cfg `load file`.
- **`tools/export.ps1`:** `export ok ...\export\athanor-src`

---

## BN-0021 — fuzz / isolation / wrong-ek (DEC-0023) (2026-09-04)

- **Host:** Windows x86_64
- **`tests/test_fuzz.exe`:** ALL PASSED (4096 HTTP, 4096 DNS, 1024 cfg).
- **`tests/test_recipe.exe`:** ALL PASSED (56 product paths; xmlns skipped).
- **`tests/test_tun.exe`:** `bad-ek A not established`.
- **`atnnode demo`:** conf handshake + echo OK.

---

## BN-0020 — IPv4 heartbeat path (DEC-0022) (2026-09-04)

- **Host:** Windows x86_64
- **`make test`:** ALL PASSED. New gates: `hs retry`, `stray ignored`,
  send/recv after stray, KA, `hb pump IPv4` on two-dmon.
- **`make android-java`:** STUB BUILD. Daemon 1s ticker / 15s KA compiles.

---

## BN-0019 — three UDP heartbeat pairs (2026-09-04)

- **Host:** Windows x86_64
- **Command:** `tests/test_hb.exe`
- **Result:** ALL PASSED including `mesh 3-pair live`, `lossy AC drop`,
  A/C UNTRUSTED on the dropped pair, B still LIVE/trusts A.

---

## BN-0018 — lab cfg + report + bind-any (2026-09-04)

- **Host:** Windows x86_64, gcc 11.3.0
- **`make test`:** ALL PASSED. New gates: `test_cfg` (parse/ready/unknown
  keys/CRLF/upper hex), `test_sign` report encode/sign/verify/tamper,
  `test_dmon` bind_any, `atnnode demo` cfg roundtrip + INADDR_ANY bind.
- **`make android-java`:** STUB BUILD (no knoxsdk.jar). javac includes
  `AtnNodeConfig.java`. Deprecation notes on DPM password APIs only.

---

## BN-0017 — dmon tunnel + recipe-check (2026-09-04)

- **Host:** Windows x86_64
- **`tests/test_dmon.exe`:** existing flush gates plus two-dmon
  loopback handshake/echo/flush-closes-send.
- **`tests/test_recipe.exe`:** no `http://` or `https://` in Makefile.
- **`atnsign manifest`:** exercised via `make manifest` after tests.

---

## BN-0016 — Knox stub classpath + atnsign (2026-09-04)

- **Host:** Windows x86_64. `vendor/knox/knoxsdk.jar` still absent
  (Partner / PTR).
- **`make android-java`:** STUB BUILD path (compiles `android/stubs` +
  `AtnKnoxBuild`).
- **`tests/test_sign.exe` + `atnsign demo`:** recorded with `make test`.

---

## BN-0015 — ML-DSA-87 ACVP KATs (2026-09-04)

- **Host:** Windows x86_64, gcc 11.3.0
- **Command:** `tests/test_mldsa.exe`
- **Result:** ALL PASSED
  - keygen_internal pk/sk match ACVP ML-DSA-87 tests[0]
  - sign_internal (rnd=0) signature matches tgId=12 tests[0]
  - verify_internal accepts KAT, rejects flipped byte
  - hedged Sign/Verify roundtrip + wrong-message reject
- **Vectors:** `usnistgov/ACVP-Server` `gen-val/json-files/ML-DSA-*-FIPS204/internalProjection.json`
- SHAKE256 incremental vs one-shot on 200-byte split absorb also gated
  in `tests/test_crypto.c`.

---

## BN-0014 — DEC-0017 dmon hb/2FA flush + Keystore wrap (2026-09-04)

- **Host:** Windows x86_64. ping 8.8.8.8 18 ms. github.com via 1.1.1.1 →
  4.237.22.38. `vendor/knox/knoxsdk.jar` still absent.
- **Command:** `make test` (includes `tests/test_dmon.exe`)
- **Result:** ALL PASSED — including UNTRUSTED-at-N=3 zeros keys, 2FA
  lockout flush, no-peer silence keeps keys.
- **`make android-so`:** NDK r27d `aarch64-linux-android21-clang` linked
  `android/libatn.so` with dmon JNI (hb + 2FA).
- **`make android-java`:** javac against android-31 + stubs. Deprecation
  notes on DPM min-letters/numeric (expected; APIs still present).
- **Not a device build.** SoT 4.1–4.4 stay `[ ]`.

---

## BN-0013 — Native daemon flush + Android Keystore path (2026-09-04)

- **Host:** Windows x86_64, ping 8.8.8.8 OK (19 ms). Prior DNS hiccup gone
  for ICMP; `knoxsdk.jar` still absent.
- **Command:** `tests/test_dmon.exe`
- **Result:** ALL PASSED — load, 2FA, flush zeros device/cluster keys,
  2FA gone, reload works.
- **Java:** `AtnKeystore` + `AtnBootReceiver` compile against android-31.
- **TIMA:** not called (deprecated API 33). DEC-0016.

---

## BN-0012 — Android NDK r27d installed; Knox jar still Partner-gated (2026-09-04)

- **Host:** Windows x86_64. LAN DNS (`10.1.1.1`) does not resolve
  `dl.google.com` / `github.com`. Used `curl --resolve` with 1.1.1.1 answers.
- **cmdline-tools:** `commandlinetools-win-14742923_latest.zip` (150532528 bytes)
  → `%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest`. sdkmanager 20.0 with
  JDK 19.
- **NDK r27d:** `android-ndk-r27d-windows.zip` SHA1
  `56607cbccd3642d4a1991f6bb3114a00f884f426` matches Google’s published
  checksum. Installed at
  `Sdk\ndk\27.3.13750724`. clang 18.0.4,
  `aarch64-linux-android21-clang`.
- **`make CC=<ndk clang> lib`:** aarch64 android objects + `libatn_crypto.a`.
- **`make android-so`:** `android/libatn.so` linked with `-llog`.
- **`make android-java`:** daemon classes compile against
  `platforms/android-31/android.jar` + in-tree Knox **stubs**.
- **Not installed:** `knoxsdk.jar` (ISS-0016). Not a device APK. SoT 4.1
  stays `[ ]`.

---

## BN-0011 — REQ-3.1 replication + REQ-3.3 heartbeat (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Command:** `tests/test_repl.exe` and `tests/test_hb.exe`
- **Result:** both ALL PASSED
  - repl: shard deterministic, put A → get B, wipe A / B still serves,
    flipped tag AUTH, catch-up after offline put
  - hb: 3 nodes LIVE, forged MAC AUTH, silence → C DEAD and key
    zeroed, A/B LIVE, one tunnel emit/pump
- **CI (prior commit a127344):** all four jobs success.

---

## BN-0010 — REQ-3.2 AVL tree (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Command:** `tests/test_tree.exe` (then full `make test`)
- **Result:** ALL PASSED — put/get/del/ordered scan, 64 reverse-order
  inserts, snapshot AEAD hides `TREE-SECRET-VALUE-NOT-ON-DISK`, restore
  recovers the value. No database library.

---

## BN-0009 — REQ-2.3 DNS + CI compiler fixes (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Command:** `make test`
- **Result:** ALL PASSED including `tests/test_dns.exe` and `atndns demo`.
- **CI evidence (run 33851841144, billing unlocked):**
  - linux-x86_64: success (`make test` + unsigned-char + lib)
  - linux-aarch64: success — ISS-0004 closed
  - windows-x86_64: fail `-Werror=unterminated-string-initialization`
    on `uint8_t hello[11] = "hello-plain"` (fixed: memcpy 11 bytes)
  - darwin: fail undeclared `arc4random_buf` under `-std=c99`
    (fixed: `_DARWIN_C_SOURCE`)
- **DNS:** in-zone A 127.0.0.1, example.com REFUSED, no forward peer.

---

## BN-0008 — REQ-2.2 admin console (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Command:** `make test`
- **Result:** `tests/test_http.exe` ALL PASSED including:
  - GET `/admin` serves embedded login (ATN-LOGIN-PAGE)
  - POST `/admin/do` without 2FA login returns 401 and does not arm wipe
  - challenge → login → console (ATN-CONSOLE-PAGE) → wipe with fresh 2FA
  - pages contain no cdn./googleapis/cloudflare/npmjs/unpkg
- **Not claimed:** percent-decoding (ISS-0011), browser TLS (ISS-0009).

---

## BN-0007 — REQ-2.1 HTTP/1.1 listener (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0, `-lbcrypt -lws2_32`
- **Command:** `make test`
- **Result:**
  - previous binaries still ALL PASSED
  - `tests/test_http.exe` ALL PASSED (parse GET/POST/Host/1.0/oversize,
    GET `/` and `/admin` exact memory pages, HEAD has no body, POST 405,
    404, unauthenticated raw GET writes no admin bytes, ciphertext hides
    the admin page)
  - `atnhttp.exe demo` OK
- **Link:** OS TCP sockets only. Zero HTTP/TLS libraries.
- **Not claimed:** RFC 8446 (ISS-0009), keep-alive (ISS-0010).

---

## BN-0006 — REQ-1.2 tunnel + REQ-1.3 2FA (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0, `-lbcrypt -lws2_32`
- **Command:** `make test`
- **Result:**
  - `tests/test_crypto.exe` ALL PASSED
  - `tests/test_tun.exe` ALL PASSED (handshake, echo, no plaintext on wire,
    replay drop, bad-MAC close)
  - `tests/test_2fa.exe` ALL PASSED (enroll/verify/wrong key/replay/lockout)
  - `atn2fa.exe demo` OK
- **Link:** OS sockets only (ws2_32 / BSD sockets). No libuv.

---

## BN-0005 — Local CI replay before GitHub Actions (2026-09-04)

- **Host:** Windows x86_64 MinGW
- **Command:** `powershell -File tools\ci_local.ps1`
- **Result:** LOCAL CI OK — `make info`, `make test` ALL PASSED, `make lib` produced `libatn_crypto.a`
- **Hooks:** `git config core.hooksPath .githooks` set in this clone
- **No new packages installed** — gcc 11.3.0 and GNU Make 4.3 already on PATH

---

## BN-0004 — ML-KEM-1024 + SHA-3/SHAKE + SHA-512 (2026-09-04)

- **Host:** Windows x86_64 MinGW, gcc 11.3.0
- **Command:** `make test` (CFLAGS include `-Itests` for the KAT header)
- **Result:** `ALL PASSED`
  - FIPS 202: SHA3-256/512 empty, SHAKE128/256 empty 32-byte
  - SHA-512 empty/abc, HMAC-SHA-512 RFC 4231 tc1
  - ML-KEM-1024: keygen/encaps match first FIPS-203-style KAT
    (ek, dk, ct, ss); decaps recovers ss; random roundtrip; flipped-ct
    implicit reject produces a different ss
- **Not in this build:** ML-DSA-87 (ISS-0005)

---

## BN-0003 — Cross-platform build (DEC-0004) (2026-09-04)

- **Host:** Windows x86_64 MinGW, `gcc -dumpmachine` = `x86_64-w64-mingw32`
- **Make:** GNU Make 4.3. Default `CC` is `cc` (does not exist here); Makefile
  now sets `CC=gcc` when origin is `default`.
- **Native `make test`:** ALL PASSED, `platform=windows-x86_64 sizeof(void*)=8`,
  linked `-lbcrypt` only.
- **`gcc -funsigned-char`:** ALL PASSED (ARM AAPCS default `char` is unsigned;
  we do not use bare `char` for numbers).
- **`gcc -m32`:** ALL PASSED, `platform=windows-x86 sizeof(void*)=4`.
- **`make lib`:** `ar t libatn_crypto.a` lists all nine `atn_*.o`.
- **Not present on this host:** ARM GCC, WSL distros. ARM execution is ISS-0004.
- **Network:** none for these builds.

---

## BN-0002 — REQ-1.1 compile + KATs (2026-09-04)

- **Host:** Windows, user `Blkph0x`
- **Compiler:** gcc.exe (GCC) 11.3.0
- **Flags:** `-std=c99 -Wall -Wextra -Werror -O2 -Iinclude`
- **Link:** `-lbcrypt` (Windows OS CSPRNG only; not a third-party crypto lib)
- **Network during compile:** not required. RFCs were read earlier for SPEC_INDEX.
- **Command:**
  ```
  gcc -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -o tests\test_crypto.exe
    src\crypto\atn_secure.c src\crypto\atn_sha256.c src\crypto\atn_hmac.c
    src\crypto\atn_hkdf.c src\crypto\atn_chacha20.c src\crypto\atn_poly1305.c
    src\crypto\atn_aead.c src\crypto\atn_nonce.c tests\test_crypto.c -lbcrypt
  tests\test_crypto.exe
  ```
- **Result:** `ALL PASSED`
  - SHA-256 empty / abc / two-block
  - HMAC-SHA-256 RFC 4231 tc1–tc7
  - HKDF-SHA-256 RFC 5869 A.1–A.3
  - ChaCha20 RFC 8439 §2.3.2 and §2.4.2
  - Poly1305 RFC 8439 §2.5.2
  - AEAD RFC 8439 §2.8.2 + bad-tag wipe + 8× random roundtrip
  - ct_equal, memzero, BCryptGenRandom, nonce replay reject
- **SoT:** REQ-1.1 marked [X]. Residual ISS-0003 (no timing lab).

---

## BN-0001 — Toolchain survey (2026-09-04)

- **Host:** Windows, user `Blkph0x`
- **cwd:** `YOUR_REPO_ROOT`
- **Measured:**
  - `gcc --version` → `gcc.exe (GCC) 11.3.0`
  - `clang` → not on PATH
  - `cl` → not on PATH
  - `go` → not on PATH
  - `rustc` → not on PATH
- **Network:** used only to read RFC Editor HTML for SPEC_INDEX (RFC 8439,
  6234, 4231, 5869). No package install.
- **Result:** DEC-0001 (C99 / GCC) is based on this measurement.
- **REQ-1.1 compile:** not yet. See later entries after `Makefile` exists.
