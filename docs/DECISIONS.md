# Decisions

Format: ID, date, status, evidence, decision, consequences.
A decision is recorded **before** code that depends on it is written.

---

## DEC-0001 — Implementation language for the crypto core (REQ-1.1)

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** On the development host, `gcc --version` reported
  `gcc.exe (GCC) 11.3.0`. `go`, `rustc`, `clang`, and MSVC `cl` were not
  present on `PATH`. SoT names "raw C, Go, or Rust" as examples. C is listed
  first and needs no package ecosystem.
- **Decision:** REQ-1.1 is written in C99, compiled with GCC, using only the C
  standard library plus the OS CSPRNG (`BCryptGenRandom` on Windows,
  `getrandom`/`/dev/urandom` on POSIX).
- **Consequences:** Later binaries (tunnel, listener, DNS) can link this
  static core. Android NDK can compile the same `.c` files. We do not pull
  Go/Rust until a new decision records that a compiler exists on the builder.

---

## DEC-0002 — Cryptographic suite (REQ-1.1)

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect map REQ-1.1 requires: constant-time compare,
  OS-seeded CSPRNG, hash, MAC, AEAD, KDF, nonce/sequence that cannot repeat
  under a key. SoT forbids outside crypto libraries and forbids inventing
  math we cannot cite. RFC 8439, RFC 6234, RFC 2104, RFC 4231, RFC 5869 are
  public, have test vectors, and cover the required set without AES hardware.
- **Decision:** Implement, line-by-line from those documents:
  - SHA-256 (RFC 6234 / FIPS 180-4)
  - HMAC-SHA-256 (RFC 2104; KATs RFC 4231)
  - HKDF-SHA-256 (RFC 5869)
  - ChaCha20-Poly1305 AEAD (RFC 8439)
  - Nonce = 32-bit sender id || 64-bit counter, little-endian on the wire
    (RFC 8439 §2.3)
  - `atn_ct_equal` for tags (RFC 8439 §4)
  - `atn_memzero` for secret buffers
  - `atn_random_bytes` from the OS only
- **Consequences:** We are not designing a cipher. We are owning the
  implementation. Handshake/public-key (REQ-1.2) is **not** in this decision;
  it will need its own DEC when we reach the tunnel.

---

## DEC-0003 — In-tree trackers are canonical

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** SoT infrastructure model is air-gapped. GitHub cannot be the
  only task database.
- **Decision:** `docs/TASKS.md` and `docs/ISSUES.md` are source of truth for
  work tracking. GitHub issues are a mirror when the network is up.
- **Consequences:** A closed GitHub issue with an open in-tree issue means
  the work is still open.

---

## DEC-0004 — One C99 tree for Windows, Linux, and ARM

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** REQ-4.x targets Galaxy S24–S26 (ARM64). Servers are Windows
  and Linux. BN-0001 host is `x86_64-w64-mingw32` only: no ARM GCC, no WSL
  distro. Crypto loads/stores are already byte-wise (endian-neutral).
  `uname` is not available on Windows, so it cannot drive the build.
- **Decision:**
  - Keep one C99 source tree. No `#ifdef` around algorithm math.
  - Select CSPRNG by compiler OS macros (`_WIN32`, `__linux__`, `__APPLE__`,
    BSD). Windows including Windows-on-ARM uses BCrypt. Linux including
    aarch64/armhf uses getrandom. Darwin/BSD uses `arc4random_buf`.
  - Makefile keys off `$(CC) -dumpmachine`, not `uname`.
  - Link `-lbcrypt` only when the *target* is Windows/MinGW.
  - Do not execute a cross-compiled binary on the builder (`CROSS=1`).
  - Unknown OS is a compile error, not a guessed device node.
- **Consequences:** The same `src/crypto/*.c` builds for Windows x64,
  Windows ARM64, Linux x86_64, Linux aarch64, Linux armhf, Android NDK.
  Executing tests on ARM hardware is ISS-0004 until we have that compiler
  or board. Do not claim an ARM run we did not perform.
