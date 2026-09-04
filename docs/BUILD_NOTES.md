# Build notes

Newest entry at the top. Record facts, not hopes.

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
