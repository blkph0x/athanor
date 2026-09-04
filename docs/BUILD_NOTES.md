# Build notes

Newest entry at the top. Record facts, not hopes.

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
