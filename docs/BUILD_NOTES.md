# Build notes

Newest entry at the top. Record facts, not hopes.

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
