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
  Percent-encoded bodies are ISS-0011. Browser cookie jars are unused
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
