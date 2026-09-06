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

---

## DEC-0005 — Quantum-resistant suite (no homemade “quantum” cipher)

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** User required bleeding-edge quantum-proof encryption with no
  exceptions. DEVELOPMENT_RULES forbid inventing algorithms. NIST FIPS 203
  (2024-08-13) is the U.S. standard ML-KEM; category 5 is ML-KEM-1024
  (equivalent claim to AES-256). FIPS 202 supplies SHAKE/SHA3 required by
  FIPS 203. Grover’s algorithm halves symmetric key search; 256-bit keys
  (ChaCha20) and SHA-512 remain the conservative symmetric/hash margin
  (CNSA 2.0 uses SHA-384+ / AES-256). HQC and FN-DSA are not FIPS yet —
  using them would be guessing.
- **Decision:**
  - Key establishment: **ML-KEM-1024 only** (FIPS 203 Table 2). Not 512, not 768.
  - Hashes used by ML-KEM: SHA3-256, SHA3-512, SHAKE128, SHAKE256 (FIPS 202).
  - Long-term MAC/KDF: HMAC-SHA-512 / HKDF-SHA-512 (RFC 2104 / 5869 / 6234).
  - Packet AEAD stays ChaCha20-Poly1305 (256-bit key). Grover still leaves
    ~128-bit quantum security, same class as AES-256.
  - We do **not** invent a lattice/hash scheme. We transcribe FIPS 203/202.
  - ML-DSA-87 (FIPS 204 category 5 signatures) is **not** in this decision’s
    compile; that is ISS-0005. Encryption/KEM does not wait on signatures.
- **Consequences:** REQ-1.2 handshake SHALL encapsulate with ML-KEM-1024.
  Shared secret feeds HKDF-SHA-512 into ChaCha20-Poly1305 session keys.
  ISS-0001 is narrowed from “unknown primitive” to “packet layout around
  ML-KEM-1024.”

---

## DEC-0006 — GitHub Actions is a public replay of `make test`, not a second build system

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** User asked for GitHub build/test pipeline and transparency,
  plus local testing before push, with this machine and GitHub always
  matching. SoT Phase 5 still forbids GitHub Actions as the *production
  signer* (REQ-5.1). `gh api` showed Actions already enabled on
  blkph0x/athanor. Local gcc 11.3.0 + GNU Make 4.3 already work
  (`make test` ALL PASSED).
- **Decision:**
  - The Makefile is the only build recipe. CI YAML may only call
    `make info`, `make test`, `make lib`, and `make test-unsigned-char`.
  - Local pre-push hook runs `make test`. Push is refused on failure.
  - `tools/ci_local.ps1` / `tools/ci_local.sh` replay the full CI command
    set on this laptop.
  - GitHub runs the same Makefile on Linux x86_64, Linux aarch64,
    macOS, and Windows MinGW so ARM execution (ISS-0004) gets a public log.
  - Green badge means “this commit built in public.” It does not replace
    the air-gapped signer.
  - Direct pushes to `main` stay allowed (solo foundry). We do not enable
    “must use PRs” so we cannot lock ourselves out. Force-push is
    discouraged; not blocked yet.
- **Consequences:** Adding a test means adding it to `make test`, never
  only to YAML. Local HEAD and `origin/main` match after every successful
  push.

---

## DEC-0007 — Tunnel packet format and one-way ML-KEM handshake

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** REQ-1.2 and T-0100 required a documented header before code.
  DEC-0005 already chose ML-KEM-1024 + HKDF-SHA-512 + ChaCha20-Poly1305.
  WireGuard-style static public keys (here: static ML-KEM ek) are a known
  out-of-band identity model. ISS-0005 signatures are not ready.
- **Decision:** Wire format and handshake as written in `docs/TUNNEL.md`.
  IPv4 UDP only. Initiator encapsulates to responder ek. Identity = knowing
  the correct ek. No tun/tap in this REQ — datagram API only.
- **Consequences:** REQ-1.2 code must match TUNNEL.md. IPv6 is ISS-0007.
  Rekey is ISS-0008.

---

## DEC-0008 — 2FA is HMAC-SHA-512 challenge-response, not TOTP

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect map prefers challenge-response so clock skew
  cannot lock out. HMAC-SHA-512 is already in-tree (DEC-0005).
- **Decision:** `HMAC-SHA-512(K, challenge ‖ "atn-2fa-v1")`. One-shot
  challenges. Five failures lock the slot until revoke/re-enroll. Phase 1
  store is in-memory; Knox replaces the backend in REQ-4.1.
- **Consequences:** `atn2fa` CLI is the standalone binary. No TOTP.

---

## DEC-0009 — REQ-2.1 listener is HTTP/1.1 inside DEC-0007 records on loopback TCP

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** SoT Tier 2 asks for custom HTTP/TLS handshakes and static
  pages from memory. Cause/effect REQ-2.1 says TCP + HTTP/1.1 parse, then
  “TLS-equivalent handshake using REQ-1.1 … or our tunnel carrying HTTP,”
  and forbids calling OpenSSL. RFC 8446 TLS 1.3 needs certificates and
  signatures; ML-DSA-87 is ISS-0005. Inventing a “TLS” dialect and calling
  it TLS would be a guess. DEC-0007 already specifies ML-KEM-1024 +
  HKDF-SHA-512 + ChaCha20-Poly1305. RFC 9112 specifies HTTP/1.1 framing.
- **Decision:**
  - IPv4 TCP, bind `127.0.0.1` only. No bind-any API in this DEC.
  - Record layer = `docs/TUNNEL.md` headers on a TCP stream
    (`docs/HTTP.md`). Handshake is DEC-0007 unchanged.
  - Application DATA is HTTP/1.1. Methods GET and HEAD only.
  - Pages are compile-time byte arrays. Header cap 8192. Backlog 8.
    Idle timeout 5000 ms. One request then close.
  - This is not RFC 8446. Chrome/Firefox will not connect (ISS-0009).
- **Consequences:** `atnhttp` is the listener binary. REQ-2.2 may add
  POST + 2FA. A real TLS 1.3 stack would need ISS-0005 closed and a new
  DEC; until then we do not claim browser TLS.

---

## DEC-0010 — Admin console is embedded HTML+CSS; POST + CSRF + 2FA

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-2.2 forbids frameworks/CDNs, requires
  handwritten pages, POST to our listener, CSRF from REQ-1.1, and a
  fresh 2FA response (REQ-1.3) on every mutating action. DEC-0009 only
  allowed GET/HEAD. RFC 9110 form POST is `application/x-www-form-urlencoded`.
  Percent-decoding is easy to get wrong; our field values are hex or
  short tokens, so `%` and `+` are rejected until a later DEC.
- **Decision:**
  - Methods: GET, HEAD, POST. POST body max 1024 bytes, Content-Length
    required, Content-Type `application/x-www-form-urlencoded` only.
  - Session: 16-byte sid, cookie `ATN-SID=<32 hex>`. CSRF =
    `HMAC-SHA-512(server_secret, sid ‖ "atn-csrf-v1")` truncated to 32
    bytes, hex in a hidden form field. Compared with `atn_ct_equal`.
  - Zero JavaScript. Inline CSS only. No `fopen` of a document root.
  - 2FA store is the in-memory DEC-0008 store inside the listener.
  - `POST /admin/do` is the only mutate. It requires an authenticated
    session **and** a fresh 2FA verify. Login without 2FA cannot set
    mutate flags.
  - Phase 3–5 console panels render honest empty/waiting copy (no fake
    node data).
- **Consequences:** `docs/HTTP.md` gains the POST/session section.
  Percent-decoding closed by DEC-0036. Browser cookie jars are unused
  until ISS-0009; our client sends `Cookie` explicitly.

---

## DEC-0011 — Authoritative DNS is RFC 1035, recursion off, zone `atn.test`

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** REQ-2.3 and the cause/effect map forbid bind/unbound and
  public resolvers. RFC 1035 is the message format. `.test` is reserved
  (RFC 2606 / 6761) so the scaffold zone does not collide with a real
  TLD. Port 53 is privileged on this Windows builder; tests cannot bind
  it without an elevation we will not assume.
- **Decision:**
  - Parser and responder from RFC 1035 §§3–4. CLASS IN. Types A, NS,
    SOA, TXT. Recursion available bit is always 0. Out-of-zone →
    REFUSED. No forwarding socket.
  - Bind `127.0.0.1`. Tests: port 0. CLI default 1053. Port 53 is a
    later production DEC.
  - Embedded zone `atn.test` until REQ-3.2. IPv4 A records only.
  - UDP 512-byte cap; TCP length-prefix for overflow (RFC 1035 §4.2.2).
- **Consequences:** `atndns` is the binary. AAAA dual-stack waits on
  ISS-0007. Console DNS forms wait on a small HTTP route; the zone
  mutate API is in the DNS module now.

---

## DEC-0012 — REQ-3.2 store is an in-process AVL tree of length-prefixed blobs

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-3.2 forbids SQLite/Postgres/LevelDB and
  requires insert/get/delete/ordered scan on length-prefixed blobs, plus
  an optional encrypted snapshot. AVL is a published balanced BST
  (Adelson-Velsky & Landis, 1962); we are not inventing a data structure.
  AEAD for the snapshot is already in-tree (RFC 8439).
- **Decision:**
  - One AVL tree. Keys compared as unsigned bytes, then by length
    (memcmp of min length; shorter first if prefix).
  - Values are opaque blobs. Max key/value 4096 bytes (DEC constant).
  - Nodes allocated with libc `malloc`; freed nodes `atn_memzero` then
    `free`. No mmap in this DEC.
  - Snapshot: serialize `count || (klen||key||vlen||val)*` as big-endian
    32-bit lengths, then ChaCha20-Poly1305 with a caller-supplied 32-byte
    key and a random nonce prefixed to the ciphertext. Restore decrypts
    and rebuilds the tree.
  - No SQL. No third-party pager.
- **Consequences:** DNS/2FA persistence can later sit on this tree.
  Replication of snapshots is REQ-3.1, not this DEC.

---

## DEC-0013 — Replication is sharded AEAD blocks over the UDP tunnel

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** REQ-3.1 requires a documented block, a documented shard
  function, vector clocks (not LWW), replica factor, catch-up, and
  transport on REQ-1.2. Tunnel DATA max is 1024 bytes (TUNNEL.md), so
  key/value caps are those that fit a PUT. SHA3-256 is already in-tree
  (FIPS 202). Version vectors are the cited concurrent-update rule
  (clocks / Bayou-style dominance); we do not invent a CRDT merge.
- **Decision:** Wire and shard as `docs/REPL.md`. Factor 2, 8 shards,
  max 4 nodes in this DEC. Cluster AEAD key is out-of-band 32 bytes.
  Conflicts are flagged, not silently merged.
- **Consequences:** `tests/test_repl` is the gate. Console display of
  conflicts is a REQ-2.2 follow-on (the flag is in the record now).
  More than 4 nodes needs a new DEC.

---

## DEC-0014 — Heartbeat is HMAC-SHA-512 over (bucket, epoch, head)

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-3.3 specifies
  `MAC(K_node, time_bucket || roster_epoch || last_block_head)` on
  REQ-1.2, miss → UNTRUSTED → wipe of **our** memory. HMAC-SHA-512 is
  in-tree (DEC-0005). Wall-clock is a side channel and a test flake;
  the caller supplies `bucket` (tests use a counter). ML-DSA signed
  roster is ISS-0005; roster is an explicit id list until then.
- **Decision:**
  - Token = HMAC-SHA-512(K, bucket64le || epoch64le || head32).
  - Accept a token whose bucket is ≥ last-1 (one reorder). A tick
    with no token for this bucket increments miss; one dropped
    interval is recoverable if the next token arrives.
  - `ATN_HB_N=3` consecutive misses (outside the window) → UNTRUSTED.
    `ATN_HB_M=3` further misses → that node wipes **its own** cluster
    key material (`atn_memzero`). Peers record DEAD; they do not wipe
    anyone else.
  - Wire on tunnel DATA: `0x48 ('H') || bucket || epoch || head || mac`.
- **Consequences:** REQ-4.4 mobile notify is a stub return until Knox.
  DNS seeding of addresses is optional; tests use an explicit roster.

---

## DEC-0015 — Knox is attach-via-SDK; Android NDK compiles our C; knox.jar is not vendored

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** SoT HARDWARE REALITY and cause/effect REQ-4.x: we attach
  to Knox/TIMA through Samsung’s supported APIs, we do not patch
  firmware. Samsung distributes the Knox SDK only through the Knox
  Partner Program (docs.samsungknox.com get-started: sign-in required).
  This host’s LAN DNS (`10.1.1.1`) does not resolve `dl.google.com` /
  `github.com`; public resolvers do. Android SDK already present at
  `%LOCALAPPDATA%\Android\Sdk` (platform 31, build-tools 31, adb 31.0.3).
  Java 11 and 19 are installed. Cited Knox APIs (not invented):
  `EnterpriseDeviceManager.getInstance`, `RestrictionPolicy.setUsbMediaPlayerAvailability(false)`,
  `setUsbDebuggingEnabled(false)`, `allowUsbHostStorage(false)`,
  `PasswordPolicy.setBiometricAuthenticationEnabled`,
  `DevicePolicyManager.setPasswordMinimumLength` / `PASSWORD_QUALITY_ALPHANUMERIC`
  (Knox docs + AOSP DPM). Android 15+ requires Device Owner or Profile
  Owner (Knox FAQ, 2024-06-20).
- **Decision:**
  - Native mesh (crypto/tun/hb/2fa) is compiled with the Android NDK
    (`aarch64-linux-android`) into `libatn.so`. Same C99 tree (DEC-0004).
  - Java daemon calls **only** APIs named in this DEC / `docs/KNOX.md`.
  - `knoxsdk.jar` is **not** committed (Samsung license). Drop it at
    `vendor/knox/knoxsdk.jar` after Partner download. Without the jar,
    we compile against in-tree **stubs** that throw; that binary is not
    a device build.
  - USB charge-only = MTP off + USB debugging off + USB host storage
    off (cited RestrictionPolicy methods). Smart Switch is a known
    bypass (KBA); we blacklist `com.sec.android.easyMover` if that API
    is present — recorded as ISS if the jar lacks it.
- **Consequences:** REQ-4.1 cannot be SoT-checked until (1) knoxsdk.jar
  is on disk, (2) an S24–S26 is enrolled as DO/PO, (3) the service
  starts on that device. NDK compile of `libatn.so` can be gated on
  this builder without a phone.

---

## DEC-0016 — On S24–S26, device keys use Android Keystore, not TIMA enable APIs

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Samsung deprecated TIMA/CCM in Knox 3.7 (API 33, Oct 2020)
  and those APIs do not work on Android 12 / Knox 3.8
  (docs.samsungknox.com deprecation-of-tima-ccm-keystore-support).
  S24–S26 ship Android 14+. Calling `enableTimaKeystore` would be a
  dead API. Replacement is Android Keystore2. Hardware-backed keys:
  `KeyGenParameterSpec` with `setIsStrongBoxBacked(true)` when the
  device has StrongBox (AOSP Keystore).
- **Decision:**
  - Native `atn_dmon` holds RAM copies of device/cluster keys, 2FA, hb.
    `atn_dmon_flush` zeros them (REQ-4.4 in-process gate).
  - Java `AtnKeystore` creates an AES-256 key in `AndroidKeyStore`
    alias `atn-device`. Prefer StrongBox; if the provider throws, fall
    back to TEE (still hardware-backed). Keys are non-exportable
    (`setIsExtractable` is not called; default false on Keystore).
  - We do **not** call TIMA enable APIs on this target generation.
- **Consequences:** SoT “TIMA loop” is attach-to-attested-boot +
  Keystore, not `enableTimaKeystore`. ISS-0018 records the deprecation.

---

## DEC-0017 — Daemon flush binds heartbeat, 2FA lockout, and password-fail K

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-4.4 lists flush triggers as REQ-3.3
  UNTRUSTED / missed token, lockout (REQ-4.2), Faraday (REQ-5.3).
  REQ-5.3 says do not wipe every underground elevator ride. DEC-0014
  already set N=3 misses → UNTRUSTED, M=3 further → DEAD. AOSP
  `DevicePolicyManager.setPasswordMinimumLetters` /
  `setPasswordMinimumNumeric` are the APIs Knox documents as the
  DA-deprecation mirrors (docs.samsungknox.com da-deprecation-and-samsung).
  `DeviceAdminReceiver.onPasswordFailed` +
  `getCurrentFailedPasswordAttempts` + `lockNow` are AOSP. AES-GCM
  wrap uses NIST SP 800-38D 96-bit IV (12 bytes) and 128-bit tag, which
  is what `AndroidKeyStore` `AES/GCM/NoPadding` produces. `ATN_2FA_FAIL_MAX`
  is already 5 (DEC-0008).
- **Decision:**
  - `atn_dmon_hb_tick` flushes RAM on `ATN_HB_UNTRUSTED` or `ATN_HB_DEAD`.
    Zero peers never flush (DEC-0014 `n_peers==0` path).
  - Production bucket is 60 seconds (`ATN_DMON_HB_BUCKET_SEC`). N=3 is
    180 seconds of silence before UNTRUSTED flush.
  - Wrap blob is `device_key[32] || cluster[32] || hb_id[8]`, AES-GCM
    under alias `atn-device`, file `atn-wrap.bin`. Flush deletes the
    wrap file so reboot cannot restore keys (4.4 re-enroll gate). The
    Keystore wrapping key stays.
  - Password K=5 (same as 2FA): `onPasswordFailed` → native flush +
    delete wrap + `lockNow`. We do **not** call
    `setMaximumFailedPasswordsForWipe` (factory reset is the optional
    wipe in cause/effect; not enabled).
  - DPM: `PASSWORD_QUALITY_ALPHANUMERIC`, min length 12, min letters 1,
    min numeric 1. Biometric remains convenience-after-quality (Knox
    biometric-authentication page).
  - USB policy is re-asserted on `ACTION_POWER_CONNECTED`.
  - 2FA `ATN_ERR_LOCKOUT` flushes the whole daemon session.
- **Consequences:** In-process 4.4 gates can be proven on this PC.
  Device 4.1–4.3 still need knoxsdk.jar + enrolled S24–S26 (ISS-0016).
  Bucket period is policy, not a Faraday measurement (ISS-0019).

---

## DEC-0018 — Category-5 signatures are ML-DSA-87 (FIPS 204) only

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** DEC-0005 left signatures as ISS-0005. FIPS 204 (2024-08-13)
  Table 1: ML-DSA-87 is NIST category 5 (same claim class as ML-KEM-1024
  / AES-256). Table 2 sizes: pk 2592, sk 4896, sig 4627. q=8380417,
  ζ=1753, (k,ℓ)=(8,7), η=2, τ=60, λ=256, γ1=2^19, γ2=(q-1)/32, β=120,
  ω=75, d=13. CNSA 2.0 pairs ML-KEM-1024 with ML-DSA-87. We do not
  invent a hash-based signature; SLH-DSA is FIPS 205 and is a separate
  later decision if we want a lattice-independent fallback.
- **Decision:**
  - Implement **ML-DSA-87 only**. Not 44, not 65.
  - Pure ML-DSA (Algorithms 1–3, 6–8). HashML-DSA is not compiled.
  - Production `Sign` is hedged (`rnd` from OS CSPRNG). Deterministic
    `rnd={0}^32` exists only so CAVP/ACVP vectors can be replayed.
  - Internal KeyGen/Sign/Verify are test/KAT entry points (FIPS 204 §6).
  - NTT zetas are FIPS 204 Appendix B, not recomputed from memory.
  - KATs are the first ML-DSA-87 records from
    `usnistgov/ACVP-Server` `ML-DSA-keyGen-FIPS204` and
    `ML-DSA-sigGen-FIPS204` `internalProjection.json` (tgId=12,
    Sign_internal, deterministic).
- **Consequences:** REQ-5.1 can sign artifacts with this primitive once
  the air-gap pipeline exists. Handshake transcripts may be signed
  later; that is not this commit. ISS-0005 closes when those KATs pass.

---

## DEC-0019 — Source manifest is SHA3-256 lines, signed ML-DSA-87

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-5.1: “Hash list of every source file
  goes into the signed manifest.” DEC-0018 closed the signature
  primitive. SHA3-256 is already in-tree (FIPS 202). ML-DSA-87 `ctx`
  is an application string ≤255 bytes (FIPS 204 Algorithm 2).
- **Decision:**
  - Manifest bytes (UTF-8, LF newlines, no BOM):
    `ATN-MANIFEST-1\n` then one line per file, paths sorted by
    `strcmp`, each line `lowercase-hex(SHA3-256(file))` + one space +
    path + `\n`. Paths use `/`, no spaces, no newlines.
  - Signature is `atn_mldsa87_sign(sk, manifest, n, ctx="atn-mf-v1", 8)`.
  - Signing key is a 32-byte seed expanded by KeyGen_internal, stored
    off-tree (`keys/`, gitignored). This builder may generate a lab
    key; that is not the air-gap production key.
  - Knox: `vendor/knox/knoxsdk.jar` is the real Partner jar (gitignored).
    In-tree `android/stubs` compile when the real jar is absent and
    export `ATN_STUB=true` so the daemon can tell. Dropping the real
    jar is a classpath switch, not a code rewrite. Stub builds are
    not device builds (DEC-0015).
- **Consequences:** `atnsign` can prove “these bytes were signed by
  our ML-DSA-87”. REQ-5.1 still needs an air-gapped host and a frozen
  toolchain before the SoT checkbox. SoT 4.1 still needs the real jar
  plus an enrolled S24–S26.

---

## DEC-0020 — Daemon session owns the DEC-0007 UDP tunnel

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** Cause/effect REQ-4.1: the phone is a mesh member and
  “Daemon speaks the tunnel protocol to a lab node.” DEC-0007 already
  froze the UDP record. `atn_dmon` already holds 2FA + heartbeat keys
  (DEC-0016/0017) with a single flush. Heartbeat `atn_hb` already
  accepts an optional `atn_tun *`.
- **Decision:**
  - `atn_dmon` embeds one `atn_tun`. JNI exposes bind / peer / handshake
    / send / recv / pump. No new record types.
  - `atn_dmon_flush` also `atn_tun_close` + `atn_tun_wipe`.
  - `atn_dmon_hb_init` passes `&d->tun` when the tunnel is ready,
    otherwise NULL (in-process tokens still work).
  - Lab peer address is not hardcoded. Java/config supplies IPv4+port.
  - `tools/src.list` is the frozen path list for `atnsign manifest`.
    The product `Makefile` contains no `http://` or `https://` fetch
    URLs (REQ-5.1 recipe gate). GitHub Actions may still use
    `actions/checkout` — that is transparency CI, not the product recipe.
- **Consequences:** In-process two-dmon handshake can be proven on this
  PC. A device-to-lab hop still needs an enrolled phone (ISS-0016).

---

## DEC-0021 — Lab node file + signed test report; mesh bind may be INADDR_ANY

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** DEC-0007 bind is loopback (safe default for tests).
  REQ-4.1 needs the phone to speak to a lab node, which loopback cannot
  receive. REQ-5.2 wants a signed PASS/FAIL artifact. We already have
  ML-DSA-87 and `atnsign sign`.
- **Decision:**
  - `atn_tun_bind` stays loopback. New `atn_tun_bind_any` binds IPv4
    `INADDR_ANY` so a mesh member can receive from a non-local peer.
    JNI `tunBind` uses bind-any. Tests keep loopback bind.
  - Lab file `atn-node.conf` (LF or CRLF), comments `#`, keys only:
    `peer_ipv4` (dotted quad), `peer_port` (1–65535), `peer_ek`
    (3136 lowercase or uppercase hex chars = ML-KEM-1024 ek). Unknown
    keys are an error. Incomplete file means “do not connect”.
  - Test report bytes: `ATN-REPORT-1\nstatus=PASS|FAIL\nplatform=<id>\n`
    signed with ML-DSA-87 ctx `atn-rp-v1`. `make report` writes the
    unsigned report after `make test`; signing uses `atnsign sign` when
    a key exists.
  - Lab PC binary `atnnode listen [port]` is the responder: ML-KEM-1024
    keygen, `bind_any`, prints `peer_port`/`peer_ek`. Operator fills
    `peer_ipv4` (we do not guess the LAN address). After handshake it
    echoes DATA. `atnnode demo` is the non-blocking gate.
- **Consequences:** A phone with `atn-node.conf` can initiate to a lab
  node without a hardcoded IP. REQ-5.2 SoT still needs an emulator/lab
  S24 run; this only gates the signed-report format. REQ-4.1 SoT still
  needs knoxsdk.jar + an enrolled device.

---

## DEC-0022 — IPv4 is the required heartbeat path; IPv6 must not replace it

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** User: work on non-IPv6 networks; IPv4 must work reliably
  and safely; no compromise on heartbeat connectivity. ISS-0007 is
  IPv6 sockaddr (still unspecified). Dual-stack `AF_INET6` with
  `IPV6_V6ONLY=0` maps IPv4 into IPv6 and is a known way to break
  IPv4-only LANs. Current daemon ticks every 60s, which is longer
  than typical UDP NAT idle (often ~30s). `atn_tun_pump` currently
  `recvfrom`s DATA/KA and drops them (heartbeat tokens never ingest).
  `hb.tun` is NULL if 2FA/hb is inited before the tunnel. Stray UDP
  to the bound port is fed to AEAD; MAC fail closes the session
  (DEC-0007) — a LAN scanner can kill the heartbeat.
- **Decision:**
  - Tunnel sockets stay `AF_INET` / IPv4 UDP. `atn-node.conf`
    `peer_ipv4` remains sufficient for `ready()`. Do not add a
    required IPv6 key. Do not open `AF_INET6` in this DEC.
  - IPv6, when specified later (ISS-0007), is a **second** `AF_INET6`
    socket. Bind/send IPv6 failure must not prevent IPv4. Mapped
    IPv4-in-IPv6 is forbidden.
  - Once `have_peer` is set, datagrams whose IPv4+port do not match
    the peer are dropped (not AEAD-decrypted). Authenticated MAC
    fail from the **pinned peer** still closes (DEC-0007).
  - Handshake retry: initiator in `HANDSHAKE` resends the last
    HS_INIT (`atn_tun_hs_retry`). Same KEM ciphertext; no second
    encapsulate. Daemon retries once per second until ESTABLISHED.
  - Keepalive: existing type-4 KA every 15s while ESTABLISHED so
    NAT mappings outlive the 60s hb bucket (DEC-0017 bucket stays
    60s; packet cadence is faster). `hb_tick` still once per bucket.
  - When the tunnel becomes ready, attach `hb.tun`. `dmon` pump on
    ESTABLISHED uses `recv_data` and ingests heartbeat DATA. Forged
    hb MAC does not close the tunnel. Tunnel AEAD fail still closes.
- **Consequences:** IPv4-only networks keep the heartbeat. IPv6 is
  still ISS-0007. SoT 4.1 still needs an enrolled device.

---

## DEC-0023 — Headless pipeline gates: in-house fuzz, isolation scan, bad handshake

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** REQ-5.2 wants a signed report, a broken handshake that
  fails, and no test-step package mirror. REQ-6.1 wants an in-house
  mutator on our parsers. REQ-6.2 wants a strings/URL audit. Knox,
  air-gap host, emulator S24, and N-hour fuzz are still unmeasured.
  `atn_http_parse_request` and `atn_dns_parse_query` are public.
- **Decision:**
  - `tests/test_fuzz.c` mutates HTTP, DNS, and lab-cfg inputs with
    SHA-256(counter) (deterministic). 4096 HTTP + 4096 DNS + 1024 cfg
    iterations in `make test`. A crash fails the run. This is **not**
    the REQ-6.1 “N hours” gate.
  - Isolation scan: `tools/src.list` paths under `src/`, `include/`,
    `android/` plus the product `Makefile` must not contain `http://`
    or `https://` except Android XML `xmlns:` namespace URIs (not
    fetches). Docs and GitHub Actions may still cite URLs (DEC-0020).
  - Wrong-ek handshake: initiator encapsulates to a different ML-KEM
    key than the responder holds; initiator must not reach
    ESTABLISHED (`ATN_ERR_AUTH` on ACK).
  - `atnnode demo` drives a loopback initiator from a parsed
    `atn-node.conf` (same keys the phone will use).
- **Consequences:** REQ-5.2 / 6.1 / 6.2 SoT stay `[ ]`. We have
  runnable scaffolding, not an air-gap factory or a Faraday proof.

---

## DEC-0024 — HTTP keep-alive, DNS TCP port, lab connect, isolation export

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** User: finish everything that does not need Knox. ISS-0010
  is RFC 9112 persistent connections (unspecified here). ISS-0012: TCP
  bind to the UDP ephemeral port fails on this Windows host. Lab phone
  is still blocked; a PC initiator reading `atn-node.conf` is not.
  REQ-6.2/6.3 want an isolation write-up and an export of `src.list`.
- **Decision:**
  - HTTP/1.1 default persist (RFC 9112 §9.3). `Connection: close` from
    the client (our existing test client) still ends the session after
    that response. Max **8** requests per TCP session. No pipelining
    we emit; we will process a second DATA already in the socket
    buffer. Idle wait for a *missing* next request is not added to
    `serve_one` when the client asked to close (existing tests stay
    one-shot).
  - DNS: if TCP cannot bind the UDP port, bind an ephemeral TCP port
    and publish it (`tcp_port`). UDP remains the required path.
    `atn_dns_query_tcp` is the TCP querier. No pcap (ISS-0012 pcap
    stays open).
  - `atn_cfg_load_file` + `atnnode connect <file>`: initiator from a
    conf on disk. Operator fills `peer_ipv4`.
  - `docs/ISOLATION.md` records the measured URL scan. `tools/export.ps1`
    copies `tools/src.list` into `export/` and refuses `*.jar`.
- **Consequences:** ISS-0010 closed. ISS-0012 TCP gap closed; pcap is
  still not taken. SoT 4.x / 5.x / 6.x stay `[ ]`.

---

## DEC-0025 — Org failsafe: witness retrieve, vote before wipe, console mesh

- **Date:** 2026-09-04
- **Status:** accepted
- **Evidence:** User: if heartbeat fails, automatically contact a known
  node to retrieve a heartbeat; warn all nodes; collectively decide
  partition vs down before wipe; org interconnected mesh; management
  website; multiple connections; memory- and thread-safe C buffers.
  SoT forbids carrier/SMS/Twilio. DEC-0014 auto-wipes at N+M with no
  vote. Console still prints “nodes: none”. No mutex exists.
- **Decision:**
  - **No PSTN/SMS API.** A “number” is a roster **witness node id**
    (8 bytes). Distress and retrieve ride DEC-0007 DATA. An optional
    `witness_note` (ASCII, ≤32) is shown on the console for a human
    to call; we do not dial.
  - On N consecutive misses: UNTRUSTED, emit **WARN**
    (`W||bucket||epoch||suspect_id||mac`, HMAC-SHA-512, ctx none —
    MAC over those bytes). Recipients **immediately emit H** (retrieve
    heartbeat from a known node). Distressed node keeps emitting H.
    Any valid H during grace resets misses → LIVE.
  - Grace **G=3** buckets after UNTRUSTED. **Vote**
    (`V||bucket||epoch||suspect||0=hold|1=wipe||mac`). Wipe (DEAD,
    `atn_memzero` own keys) only if grace expires **and** `hold_votes==0`.
    A HOLD vote (peer or local operator) cancels wipe. Peers do not
    wipe anyone else.
  - `ATN_HB_MAX_PEERS=16` (DEC-0014’s 4 was the old cap).
  - Daemon flush (DEC-0017) on **DEAD only**, not UNTRUSTED (grace).
  - Console: roster + HOLD/WIPE forms (2FA+CSRF). `atn_http_attach_mesh`.
  - **Threads:** portable `atn_lock` (Windows `CRITICAL_SECTION`,
    POSIX `pthread_mutex`). Guard HTTP sessions and hb. We still
    **accept one TCP client at a time** (`serve_one`); backlog stays 8.
    That is the memory-safe default — no worker pool yet (would be a
    new DEC). All new parsers have explicit caps; secrets `atn_memzero`.
- **Consequences:** Faraday “silence always wipes” is delayed by G and
  can be cancelled by HOLD. Capture without votes still wipes after
  grace. ISS-0020 records “no auto SMS”. SoT 4.x/5.3 stay `[ ]`.

---

## DEC-0026 — Operator HTTP client; Poly1305 wall-clock note; export/isolation evidence

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: keep moving without `knoxsdk.jar`; track everything;
  no guessing. ISS-0009 unblock path (b) is “in-tree operator client”
  now that ML-DSA-87 exists (ISS-0005 closed). Browsers still cannot
  speak DEC-0009. ISS-0003 asks for a BUILD_NOTES timing note, not a
  invented CT badge. REQ-6.2/6.3 still need measured evidence; NIC-down
  is not available without admin on this builder. BN-0023 was pending
  `make test` for DEC-0025 (now green). Cause/effect roll-up still
  showed Phase 2–3 open while SoT had them [X].
- **Decision:**
  - **Operator client (ISS-0009-b):** `atnhttp serve-once [port]` keygens,
    binds loopback, prints `peer_ipv4=127.0.0.1`, `peer_port=`, and
    `peer_ek=` (3136 hex), then accepts **one** client (`serve_one`) and
    exits. `atnhttp get <conf> <path>` loads DEC-0021 keys
    (`peer_ipv4`/`peer_port`/`peer_ek`), requires `127.0.0.1` (listener
    is loopback-only; no inventing bind-any for HTTP), handshakes, GETs
    the path, writes the HTTP response to stdout. `atnhttp demo` also
    writes a temp conf and reloads it so the conf path is gated
    in-process. This is **not** RFC 8446. Browsers remain unsupported.
  - **Poly1305 timing (ISS-0003):** `tests/test_crypto` runs a fixed
    iteration wall-clock probe (`clock()`) for two keys on a 1024-byte
    message and prints `note poly1305 wall …`. The gate is “ops
    completed”, not a delta threshold (inventing a CT bound is a
    guess). Record the numbers in BUILD_NOTES. ISS-0003 stays open
    until measured on enrolled S24/ARM targets.
  - **Isolation / export:** `make export-tree` runs `tools/export.ps1`
    (named so it does not collide with the `export/` directory).
    `docs/ISOLATION.md` records: recipe URL scan (existing), export
    contains SoT + map, and whether a default route was present during
    the measured `make test`. Do **not** claim NIC-down or SoT 6.2 [X].
  - **Tracker hygiene:** Cause/effect roll-up Phase 2–3 mirrors SoT [X].
    REQ-2.1 residual ISS-0010 marked closed (DEC-0024). SoT 4.x/5.x/6.x
    stay `[ ]`.
- **Consequences:** Operators can use `atnhttp` without a browser.
  ISS-0009 narrows to “no browser TLS”. ISS-0003 has a Windows-x86_64
  wall-clock note only. Knox / air-gap / Faraday remain blocked.

---

## DEC-0027 — Diagnostic profile (no-brick first flash)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: first build is a heavy test/diag build; test fully
  without bricking lab phones/systems; prove before SoT mark-off; keep
  trackers current. `docs/DIAG_USECASES.md` §3. DEC-0017 already refuses
  DPM factory-reset wipe. DEC-0025 DEAD still calls `atn_dmon_flush`
  (ZEROIZE). Lab needs a measured non-destructive path.
- **Decision:**
  - Conf keys (unknown still fail closed): `diag=0|1` (default absent=0),
    `flush_mode=zeroize|log_only` (absent: `zeroize` if `diag=0`,
    `log_only` if `diag=1`), `wipe_armed=0|1` (default 0).
  - `flush_mode=log_only` is **illegal** unless `diag=1` (parse error).
  - `atn_dmon`: on DEAD or 2FA lockout, if `log_only` **and**
    `wipe_armed=0`: increment `flush_log_count`, do **not** zero
    device/cluster keys, do **not** clear `loaded`. If `wipe_armed=1`
    or `flush_mode=zeroize`: existing `atn_dmon_flush` ZEROIZE.
  - `ATN-REPORT-1` gains a required line `diag=0|1` after `platform=`
    (ctx still `atn-rp-v1`). Default encode uses `diag=0`.
  - SoT 4.x/5.x stay `[ ]`. Diag is not a device Knox gate.
- **Consequences:** Lab can soak wipe *paths* without destroying keys.
  Production conf omits `diag` → ZEROIZE as today.

---

## DEC-0028 — Multi-hub failover list (IRC-like attach)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: hubs at different sites, synced; any node may use
  any hub (IRC reconnect). ISS-0023: single `peer_ipv4` blocks that.
  `ATN_REPL_MAX_NODES=4` (DEC-0013) caps the roster we can honestly
  mirror without a migration DEC — hub list size matches that cap.
  DEC-0022: IPv4 required; pin stray UDP; intentional peer change is
  not “stray”.
- **Decision:**
  - Primary remains `peer_ipv4` / `peer_port` / `peer_ek` (hub index 0).
    `atn_cfg_ready` unchanged.
  - Optional hubs 2..4: `hub2_ipv4`, `hub2_port`, `hub2_ek`, … `hub4_*`.
    A hub slot counts only if all three keys are present; partial slot
    is parse error. Max hubs = `ATN_CFG_MAX_HUBS` = 4 (= repl cap).
  - `atn_cfg_hub_count`, `atn_cfg_hub_get(i, …)` for i in [0, count).
  - Failover: on tunnel AUTH/close or HS failure, initiator may advance
    to the next hub index, `atn_tun_set_peer` + new ek, clear pin, retry
    HS. Documented in `docs/DIAG_USECASES.md` D-08. Raising hub count
    above 4 requires a repl-cap DEC first.
  - No public DNS forwarding. Hub addresses are conf/roster only.
- **Consequences:** Site blackout of hub0 can still reach hub1..3 without
  wipe. ISS-0023 progresses; full phone daemon wire-up still needs jar.

---

## DEC-0029 — Outage classes (blackout ≠ Faraday)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: power-outage failsafe — phones must not self-wipe
  in a blackout. REQ-5.3 Faraday still wants flush on RF isolation.
  ISS-0022: those collide if silence alone is the only signal. We have
  **no** cited power/RF sensor API in-tree; inventing one is forbidden.
  Operator/conf class is the only evidence-backed discriminator now.
  Multi-hub failover (DEC-0028) already covers single-site blackout when
  another hub answers.
- **Decision:**
  - Conf key `outage_class=` one of: `normal` (default if absent),
    `maintenance`, `blackout`, `faraday`, `capture`.
  - **normal:** DEC-0025 silence → DEAD → dmon flush policy (DEC-0027).
  - **maintenance** / **blackout:** while class is set, daemon injects
    a local HOLD before grace expiry so silence does **not** reach DEAD
    wipe. Keys stay. Class is cleared only by conf/console change (not
    by guessing power restoration).
  - **faraday** / **capture:** DEC-0025 wipe path remains (DEAD → flush
    per DEC-0027 mode). Bag proof still REQ-5.3 / ISS-0019.
  - Automatic “detect blackout vs bag from RF alone” stays **out of
    scope** (ISS-0022 residual). Console may set class after 2FA later.
- **Consequences:** Blackout + `outage_class=blackout` (or multi-hub
  success) will not brick. Faraday/capture still can. SoT 5.3 stays `[ ]`.

---

## DEC-0030 — Knox test-without-jar path is stubs + Makefile (not Gradle/Node)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User asked to adopt a “compile without knoxsdk.jar, drop
  jar later” guide. That paste used: Gradle + AndroidX/Material,
  `app/libs/knox_sdk.jar`, `BuildConfig.USE_REAL_KNOX`, no
  `import com.samsung…` until jar lands, HttpURLConnection enroll/poll
  to a Node/Express registry, and `DEVICE_BRICK_LOCK`. SoT forbids
  third-party modules, package mirrors, and Node. DEVELOPMENT_RULES
  forbid guessing APIs. Cause/effect forbids wiping anyone else’s
  device. DEC-0015/0019 already froze **our** path: in-tree
  `android/stubs` under the real Samsung package names, Makefile
  `$(wildcard vendor/knox/knoxsdk.jar)`, `AtnKnoxBuild.isStub()` via
  `ATN_STUB`. DEC-0027 is the diag/no-brick profile for lab soak.
- **Decision:**
  - **Reject** the pasted Gradle/Node/familymanager design for this
    tree. Do not add npm, Express, OkHttp, AndroidX, or a remote
    “brick lock” command bus.
  - **Keep** `import com.samsung.android.knox…` in product Java. Stubs
    live at `android/stubs/com/samsung/android/knox/…` so javac
    resolves the same names the Partner jar will provide. That is the
    opposite of “forbid imports until the jar arrives.”
  - **Drop-in path (only):** `vendor/knox/knoxsdk.jar` (gitignored).
    Not `app/libs/knox_sdk.jar`. `make android-java` already switches:
    jar absent → compile stubs + daemon; jar present → classpath jar,
    no stubs on the compile line.
  - **Test vs production signal:** `AtnKnoxBuild.isStub()` / log
    `knoxStub=` (stub field present). Not a Gradle `USE_REAL_KNOX`
    BuildConfig (we have no Gradle product recipe).
  - **Lab first:** stub/`diag=1` builds are for PC gates **and** USB lab
    APK connectivity (DEC-0038). Do not claim stub policy as “enrolled
    Knox.” Real device SoT 4.x still needs the jar + Device/Profile
    Owner (T-0400 / ISS-0016).
  - Document the flow in `docs/KNOX.md` and `vendor/knox/README.md`.
- **Consequences:** Developers write final-shaped Knox call sites now;
  jar drop is a path swap. Foreign paste leftovers stay out of tree.

---

## DEC-0031 — Multi-hub wire failover (D-08) on the daemon tunnel

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** DEC-0028 froze the conf list and named the wire step
  (`set_peer` + new ek + clear pin + retry HS) but left the retry budget
  and API unspecified. T-0701 / DIAG D-08 need a PC harness: hub0 dark,
  hub1 answers, initiator ESTABLISHES without wipe. `atn_tun_init_initiator`
  requires `CLOSED` and replaces `peer_ek`; intentional peer change must
  not be treated as DEC-0022 stray pin forever on the dead hub.
- **Decision:**
  - `atn_dmon` tracks `hub_idx` (0 = primary `peer_*`).
  - `atn_dmon_tun_connect_hub(d, cfg, i)`: detach/wipe any prior tunnel
    (keys/device session stay), `atn_tun_init_initiator` with hub `i` ek,
    bind ephemeral loopback/any as today, `atn_tun_set_peer` (new pin),
    `hs_send_init`. Wipe clears the old pin; `set_peer` installs the new
    one — that is the DEC-0028 “clear pin” step.
  - `atn_dmon_tun_failover(d, cfg, start_i, timeout_ms)`: for each hub
    from `start_i` in order, call `connect_hub`, then up to
    `ATN_DMON_HUB_HS_ATTEMPTS` (=3) rounds of `pump(timeout_ms)` +
    `hs_retry` while in HANDSHAKE. Advance to the next hub on
    `ATN_ERR_AUTH`, `CLOSED`, or exhausted attempts without
    ESTABLISHED. Stop on ESTABLISHED. If every hub fails →
    `ATN_ERR_STATE` (no flush by itself).
  - Does not invent RF/power sensing. Blackout wipe policy remains
    DEC-0029 (`outage_class`) / DEC-0027 (`flush_mode`).
  - Gate: `tests/test_hub_failover.c` (D-08: dead hub0 + live hub1).
- **Consequences:** IRC-like attach is proven on PC without Knox.
  Phone JNI can call the same APIs later. T-0702 still needed to raise
  hub/repl cap above 4.

---

## DEC-0032 — Raise repl/hub roster cap to 16 (match hb peers)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** DEC-0013 froze `ATN_REPL_MAX_NODES=4`; DEC-0028 tied
  `ATN_CFG_MAX_HUBS` to that cap. Multi-site IRC-like attach (DIAG UC-02/03)
  needs more than four hubs. DEC-0025 already set `ATN_HB_MAX_PEERS=16`.
  PUT record with 16 clocks + max value is 544 bytes; tunnel DATA max is
  1024 (fits). Factor stays 2; shard count stays 8 (changing either is a
  separate migration DEC).
- **Decision:**
  - `ATN_REPL_MAX_NODES` = **16**. `ATN_CFG_MAX_HUBS` = **16** (same
    number; keep them equal).
  - Conf optional hubs: `hub2_*` … `hub16_*` (decimal after `hub`, then
    `_ipv4`/`_port`/`_ek`). Partial slot and gaps still fail closed.
  - `ATN_REPL_MAX_REC` = packed tree-record ceiling for max clocks +
    max value; replace fixed `512` stack buffers that would truncate.
  - Factor 2 and 8 shards unchanged. On-disk / wire records already
    carry `nclock`; old 4-node data remains readable.
  - Java `AtnNodeConfig.MAX_HUBS` mirrors 16.
- **Consequences:** Lab can list up to 16 failover hubs / repl roster
  slots. SoT 3.1 stays done (gate was factor-2 + clocks; cap is policy).
  Larger mesh soak is still a harness task, not a SoT flip.

---

## DEC-0033 — Crypto floor + Essential Eight posture (no downgrade)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: maintain Essential Eight-or-better; no compromise on
  transport crypto; bleeding-edge / next-gen only. SoT Tier 1 already
  freezes ML-KEM-1024 + ChaCha20-Poly1305-256 + HMAC/HKDF-SHA-512.
  DEC-0018 freezes ML-DSA-87. CNSA 2.0 pairs ML-KEM-1024 with ML-DSA-87
  (same category-5 claim class). ASD Essential Eight
  (cyber.gov.au / ASD Strategies to Mitigate Cyber Security Incidents)
  is an **enterprise IT** baseline — Athanor maps analogues; it is not a
  claim that this repo is an assessed ML3 Windows estate.
- **Decision — crypto floor (never lower without a new DEC):**
  - Key establishment: **ML-KEM-1024 only**. Forbid ML-KEM-512/768 and
    classical-only KEX (RSA, ECDH, X25519) on Athanor tunnels.
  - Signatures: **ML-DSA-87 only** for product signing. Forbid
    ML-DSA-44/65, RSA, ECDSA for our pens.
  - Symmetric AEAD on our wire/store: **ChaCha20-Poly1305** with
    **256-bit** keys (RFC 8439). Forbid AES-128, RC4, 3DES, Blowfish on
    our protocols. Android Keystore AES-256-GCM wrap of our 32-byte
    device key remains allowed (hardware bond, not a tunnel cipher).
  - MAC/KDF: HMAC-SHA-512 / HKDF-SHA-512 (and SHA-3/SHAKE as FIPS 203
    requires). Forbid MD5 / SHA-1 as security primitives.
  - No third-party crypto libs (OpenSSL, libsodium, WireGuard, etc.).
  - Gate: `tests/test_recipe.c` rejects forbidden tokens in product
    paths; `docs/CRYPTO.md` is the floor card.
- **Decision — Essential Eight map:** document in `docs/ESSENTIAL8.md`
  how each of the eight strategies maps to an Athanor control or an
  honest **operator/OS gap**. Do not mark SoT boxes from E8 alone.
- **Consequences:** Downgrades are SoT violations. PQ rekey (ISS-0008)
  and console `outage_class` (2FA) remain open follow-ons, not excuses
  to weaken the floor.

---

## DEC-0034 — Console 2FA sets `outage_class` (DEC-0029 live control)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** DEC-0029 froze conf `outage_class` but left console
  mutate open. Essential Eight MFA (DEC-0033 map) wants admin policy
  changes behind 2FA. Console already gates wipe/HOLD with CSRF+2FA
  (DEC-0009/0025).
- **Decision:**
  - `atn_http_attach_outage(s, uint8_t *outage_class)` — optional pointer
    into daemon/session policy (same byte values as `ATN_CFG_OUTAGE_*`).
  - Console shows current class. POST `/admin/do` `action=outage` with
    `class=normal|maintenance|blackout|faraday|capture` after CSRF+2FA.
  - Invalid class → 400. No attach → 400. Does not write conf files;
    process memory only until next conf apply.
- **Consequences:** Operator can HOLD blackout class without editing
  files mid-incident. T-0802 closes.

---

## DEC-0035 — PQ tunnel rekey (ML-KEM-1024, same floor)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** Cause/effect REQ-1.2 lists REKEY; DEC-0007 closed the
  session before seq wrap (ISS-0008). DEC-0033 forbids classical-only
  rekey. Handshake is already one-way KEM to the responder’s static ek
  (TUNNEL.md). Responder has no initiator ek, so only the **initiator**
  can start a rekey (same encapsulate direction as HS_INIT).
- **Decision:**
  - Wire types (version still 1): **6 = REKEY_INIT** (payload =
    ML-KEM-1024 ct, plaintext, same size as HS_INIT), **7 = REKEY_ACK**
    (AEAD under the *new* `k_ack`, confirm = SHA3-256(new ct), same
    layout as HS_ACK).
  - HKDF: identical to handshake (`atn-tun-v1` ‖ new_ct, ML-KEM-1024 ss).
  - Initiator `atn_tun_rekey_send`: ESTABLISHED, not already pending;
    encapsulate; stage new keys; send REKEY_INIT; keep **old** DATA keys
    until REKEY_ACK verifies.
  - Responder on REKEY_INIT (pinned peer only): Decaps → stage keys →
    send REKEY_ACK → **commit** (install new DATA keys, seq=1, clear
    replay, wipe old session keys). Own dk preserved.
  - Initiator on REKEY_ACK: verify under staged `k_ack` → commit same way.
  - Soft policy: callers should rekey before seq exhaustion; hard close
    at `send_seq == UINT64_MAX` remains. No AES/X25519 rekey path.
  - Gate: `tests/test_tun` ESTABLISH → echo → rekey → echo.
- **Consequences:** ISS-0008 closes. Long-lived tunnels stay on category-5
  KEM without tearing down the UDP socket / peer pin.

---

## DEC-0036 — Form percent-decode (WHATWG urlencoded)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** DEC-0010 rejected `%`/`+` until a cited decoder exists
  (ISS-0011). WHATWG URL Standard §5.1 defines
  `application/x-www-form-urlencoded` parsing; §1.3 defines
  percent-decode (invalid `%` remains literal). Console field values
  remain hex tokens / short ASCII labels after decode.
- **Decision:**
  - `atn_http_form_get` parses per WHATWG §5.1: split on `&`, first `=`
    separates name/value, empty segments skipped, `+` → SP, then
    percent-decode (§1.3).
  - Product restriction: decoded name and value must be US-ASCII with no
    NUL (console is ASCII; non-ASCII / NUL → `ATN_ERR_PARAM`). Full
    Encoding-Standard UTF-8 replacement is not required while fields
    stay ASCII.
  - Gate: `tests/test_http` direct `form_get` cases (`+`, `%2B`, `%20`,
    encoded name, invalid `%` literal, missing key).
- **Consequences:** ISS-0011 closes. `docs/HTTP.md` drops the reject-%
  rule.

---

## DEC-0037 — Lab builds stay online; NIC-down is a release gate

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: testing builds must not air-gap this development
  host (git push, Actions, Partner jar path). REQ-6.2 / ISS-0021
  “default route down” is a **release** proof, not a daily lab
  requirement. Removing `0.0.0.0/0` on the Windows builder is
  counter-productive to development and needs Admin elevation the
  agent session does not hold.
- **Decision:**
  - **Day-to-day / testing builds:** keep the default route. Continuous
    isolation evidence is `tests/test_recipe` (no product fetch URLs) +
    `make export-tree` (DEC-0024/0026). Document route-up in BN.
  - **Release / SoT REQ-6.2 [X]:** one-shot measurement on a **dedicated
    release host** or a deliberately elevated window — never as the
    standing posture of the development machine. File BN when done.
  - **REQ-5.1 air-gap compiler:** separate Phase 5 host; not this lab.
  - Agents must **not** delete the default route or disable NICs on the
    development builder unless the operator explicitly opens a release
    measurement session and elevates.
  - T-0601 moves to release backlog (not “Now”).
- **Consequences:** ISS-0021 stays open until release measure; lab work
  continues online. No Admin elevation required for ordinary Athanor
  coding/gates.

---

## DEC-0038 — Stub lab APK for phone↔hub connectivity (no Knox yet)

- **Date:** 2026-09-06
- **Status:** accepted
- **Evidence:** User: start phone + site testing with one hub and
  makeshift/mock policy; USB plug-in for adb; USB charge-only is a
  **release** requirement; keep stubs until `knoxsdk.jar` drop-in.
  DEC-0030 already owns stubs+Makefile (rejects Gradle/Node). DEC-0027
  owns diag/`log_only`. Prior wording that stub builds are “compile+PC
  only” blocked the lab path the user needs.
- **Decision:**
  - **Allowed now:** USB-install a **stub** APK (`make android-apk`) for
    LAN connectivity soak: phone initiator ↔ `atnnode listen` hub.
    Mesh crypto is real (ML-KEM-1024 + AEAD). Conf: `diag=1` +
    `flush_mode=log_only`.
  - **Skipped on stub:** USB charge-only, Knox password/biometric policy
    (must keep adb). `AtnKnoxBuild.isStub()` gates this; PowerReceiver
    also skips USB on stub.
  - **Packaging:** aapt2 + d8 + apksigner via Makefile/`tools/android-apk.ps1`
    — **no** Gradle, AndroidX, npm, or `USE_REAL_KNOX` flag (DEC-0030).
  - **Claims forbidden:** do not mark SoT REQ-4.x `[X]`; do not call the
    stub build “enrolled Knox.” Log `knoxStub=true`.
  - **Later:** jar at `vendor/knox/knoxsdk.jar` → same APK recipe with
    REAL classpath → Device/Profile Owner + USB/password release gates.
- **Consequences:** `docs/LAB.md` is the operator recipe. T-0400 remains
  the Partner jar gate for release Knox.
