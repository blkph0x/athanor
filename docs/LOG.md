# Development log

Newest at the top.

---

## 2026-09-04 — REQ-3.2 memory tree (DEC-0012)

- AVL tree of length-prefixed blobs. Snapshot is nonce||ct||tag.
- tests/test_tree ALL PASSED (BN-0010). SoT REQ-3.2 [X].
- Next legal: REQ-3.1 replication (T-0301). Heartbeat waits on 3.1+3.3
  depends (2.3 and 3.2 now green; 3.1 still open).

---

## 2026-09-04 — REQ-2.3 DNS + GitHub Actions actually ran

- Billing unlocked. Run 33851841144 executed: Linux x64 and ARM green.
  Windows/Darwin compiler misses fixed (string init, Darwin CSPRNG).
- DEC-0011 / docs/DNS.md. RFC 1035 authoritative `atn.test`, recursion
  off, REFUSED out of zone. `make test` ALL PASSED (BN-0009).
- SoT REQ-2.3 [X]. ISS-0004 and ISS-0006 closed. Next: REQ-3.2 tree.

---

## 2026-09-04 — REQ-2.2 console (DEC-0010)

- Handwritten HTML+CSS, zero JS, compiled into `atnhttp`.
- POST `application/x-www-form-urlencoded`, CSRF HMAC-SHA-512, 2FA on
  login and on `POST /admin/do`. Unauthed mutate leaves wipe idle.
- `make test` ALL PASSED (BN-0008). SoT REQ-2.2 [X].
- Next legal: REQ-2.3 DNS (T-0202). Phase 3 panels wait on REQ-3.x.

---

## 2026-09-04 — REQ-2.1 listener (DEC-0009)

- Did not implement TLS 1.3 (no ML-DSA yet, would be a guess).
- TCP loopback + TUNNEL.md records + HTTP/1.1 GET/HEAD from memory.
- `make test` ALL PASSED including `tests/test_http` and `atnhttp demo`
  (BN-0007). SoT REQ-2.1 [X].
- Next legal: REQ-2.2 handwritten admin console (T-0201). DNS is T-0202.

---

## 2026-09-04 — Phase 1 closed: tunnel + 2FA

- DEC-0007 wire format in docs/TUNNEL.md. DEC-0008 HMAC-SHA-512 2FA.
- UDP tunnel: ML-KEM-1024 handshake, ChaCha20-Poly1305 data, 64-seq window.
- 2FA library + atn2fa CLI. make test ALL PASSED (BN-0006).
- SoT REQ-1.2 and REQ-1.3 [X]. Next legal: REQ-2.1 listener (T-0200).
- Residuals: ISS-0007 IPv6, ISS-0008 rekey, ISS-0005 ML-DSA.

---

## 2026-09-04 — Actions queued then refused (ISS-0006)

- Pushed 2ca2be9. Pre-push hook ran `make test` ALL PASSED. Local HEAD
  equals origin/main.
- GitHub run 33848702338 failed in ~4–9s on all four jobs with
  “account is locked due to a billing issue.” No compile happened.
- Workflow stays in tree. Re-run after billing is fixed.

---

## 2026-09-04 — GitHub Actions + local gate (DEC-0006)

- User wanted a full GitHub build/test pipeline, local testing before
  push, and laptop/GitHub always matching.
- Workflow `.github/workflows/ci.yml` runs only Makefile targets on
  Linux x86_64, Linux aarch64, macOS, Windows MinGW.
- `git config core.hooksPath .githooks`; pre-push runs `make test`.
- `tools/ci_local.ps1` is the local full replay. No extra packages were
  required on this machine (gcc 11.3 + make 4.3 already present).

---

## 2026-09-04 — Quantum-resistant KEM (DEC-0005)

- User required bleeding-edge quantum-proof encryption, no exceptions.
- Did not invent a cipher. Implemented FIPS 202 + FIPS 203 ML-KEM-1024
  (NIST category 5) and SHA-512/HMAC-SHA-512 from RFC 6234.
- `make test` ALL PASSED including official-style ML-KEM-1024 KAT and
  implicit rejection.
- Handshake algorithm for REQ-1.2 is now ML-KEM-1024. Packet bytes still
  T-0100. Signatures are ISS-0005 (ML-DSA-87).

---

## 2026-09-04 — Windows / Linux / ARM build path (DEC-0004)

- User required the tree to be buildable on Windows, Linux, and ARM.
- Makefile now reads `$(CC) -dumpmachine` (not uname). `-lbcrypt` only for
  Windows targets. Default CC is gcc because GNU Make's `cc` is missing here.
- CSPRNG: BCrypt (Windows including ARM), arc4random (Darwin/BSD), getrandom
  then urandom (Linux), urandom (Android < 28 and other unix). Unknown OS
  is `#error`.
- Verified on this host: x64 native, `-funsigned-char`, `-m32` (4-byte
  pointers). All ALL PASSED. No ARM compiler here (ISS-0004).

---

## 2026-09-04 — REQ-1.1 compiled and verified

- Implemented in-house C99 core: SHA-256, HMAC-SHA-256, HKDF-SHA-256,
  ChaCha20-Poly1305 AEAD, ct_equal, memzero, OS CSPRNG, nonce sequencer.
- Specs: RFC 6234, 2104, 4231, 5869, 8439 as listed in SPEC_INDEX.
- `tests/test_crypto.exe` printed `ALL PASSED` (BN-0002).
- SoT REQ-1.1 → [X]. Next legal work is T-0100 / ISS-0001 (handshake
  decision). Do not write tunnel handshake bytes until that DEC exists.

---

## 2026-09-04 — Rules, trackers, start of REQ-1.1

- Named public project **Athanor**; architecture name remains SF-ARCH.
- User required: never guess; document and comment everything; maintain a
  task list; keep code notes, build notes, issues.
- Wrote `DEVELOPMENT_RULES.md` and the `docs/` desk.
- Recorded DEC-0001 (C99/GCC — only compiler present), DEC-0002 (RFC suite),
  DEC-0003 (in-tree trackers canonical).
- Opened ISS-0001 (handshake primitive unknown — do not guess in REQ-1.2),
  ISS-0002 (FIPS page numbers), ISS-0003 (Poly1305 timing not yet measured).
- Next in this session: implement REQ-1.1 from the cited RFCs and run KATs.
