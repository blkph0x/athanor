# Task list (canonical)

Update this file in the same commit as the work. SoT checkboxes move only
after the cause/effect verification gate is green.

Status: `open` | `in_progress` | `blocked` | `done`

---

## Now

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0100 | open | REQ-1.2 | DEC for tunnel packet header + handshake (needs public-key or PSK decision — do not guess) |

---

## Backlog (do not start early)

| ID | Status | REQ | Task | Depends |
|---|---|---|---|---|
| T-0101 | open | REQ-1.2 | UDP tunnel from OS sockets | T-0100 |
| T-0102 | open | REQ-1.3 | 2FA challenge-response binary on HMAC | REQ-1.1 |

Full SoT REQs 2.x–6.x stay in `SOURCE_OF_TRUTH.md` until their phase is legal
to enter. Do not spawn implementation tasks for them here until then.

---

## Done

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0001 | done | — | Write development rules (never guess, document, comment, track) |
| T-0002 | done | — | Stand up docs/ trackers, SPEC_INDEX, DEC-0001/0002/0003 |
| T-0003 | done | REQ-1.1 | Implement SHA-256, HMAC, HKDF, ChaCha20-Poly1305, ct_equal, memzero, OS CSPRNG, nonce sequencer from cited RFCs |
| T-0004 | done | REQ-1.1 | Known-answer tests transcribed from RFC 6234/FIPS, RFC 4231, RFC 5869, RFC 8439 |
| T-0005 | done | REQ-1.1 | Compile with GCC 11.3.0, record BUILD_NOTES BN-0002, ALL PASSED |
| T-0006 | done | REQ-1.1 | SoT REQ-1.1 marked [X]. Residual ISS-0003 remains open (timing not measured). |
