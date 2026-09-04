# Development log

Newest at the top.

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
