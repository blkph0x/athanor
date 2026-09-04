# Task list (canonical)

Update this file in the same commit as the work. SoT checkboxes move only
after the cause/effect verification gate is green.

Status: `open` | `in_progress` | `blocked` | `done`

---

## Now

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0400 | blocked | REQ-4.1 | Drop `vendor/knox/knoxsdk.jar` (Partner / PTR) then device-enroll |

---

## Backlog (do not start early)

Phase 4 device enroll waits on T-0400 (real knoxsdk.jar). Stub
classpath is in place. Phase 5 air-gap host (no default route, frozen
compiler) is still unmeasured.

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
| T-0007 | done | REQ-1.1 | Portable Windows/Linux/ARM build (DEC-0004). Native + unsigned-char + m32 KATs pass. ARM run is ISS-0004. |
| T-0008 | done | REQ-1.1-PQ | FIPS 202 SHAKE/SHA3 + FIPS 203 ML-KEM-1024 + SHA-512. KATs ALL PASSED. |
| T-0009 | done | — | GitHub Actions + local pre-push so laptop and origin/main run the same Makefile (DEC-0006). |
| T-0100 | done | REQ-1.2 | Tunnel packet spec `docs/TUNNEL.md` (DEC-0007) |
| T-0101 | done | REQ-1.2 | UDP tunnel: ML-KEM handshake, AEAD data, replay window, MAC-fail close |
| T-0102 | done | REQ-1.3 | HMAC-SHA-512 2FA library + `atn2fa` CLI |
| T-0200 | done | REQ-2.1 | HTTP/1.1 listener on loopback TCP + DEC-0007 records (`atnhttp`) |
| T-0201 | done | REQ-2.2 | Embedded admin console, POST + CSRF + 2FA on mutate |
| T-0202 | done | REQ-2.3 | Authoritative DNS RFC 1035, zone `atn.test`, no recursion |
| T-0300 | done | REQ-3.2 | AVL memory tree + ChaCha20-Poly1305 snapshot |
| T-0301 | done | REQ-3.1 | Sharded AEAD blocks + vector clocks over the UDP tunnel |
| T-0302 | done | REQ-3.3 | HMAC-SHA-512 heartbeat, UNTRUSTED + self-wipe |
| T-0401 | done | REQ-4.4 | Native dmon binds hb/2FA lockout flush; Keystore wrap; DPM K=5 (DEC-0017). Device SoT still blocked on T-0400. |
| T-0500 | done | ISS-0005 | ML-DSA-87 from FIPS 204; ACVP keyGen + Sign_internal KATs |
| T-0510 | done | REQ-5.1 | `atnsign` SHA3-256 manifest + ML-DSA-87 (DEC-0019). Air-gap host still open. |
| T-0402 | done | REQ-4.1 | Stub/real knoxsdk.jar classpath switch; ATN_STUB marker |
| T-0403 | done | REQ-4.1 | dmon owns DEC-0007 tunnel; two-dmon loopback handshake (DEC-0020) |
| T-0511 | done | REQ-5.1 | `atnsign manifest` + `tools/src.list` + Makefile recipe-check |
| T-0404 | done | REQ-4.1 | Lab `atn-node.conf` + `bind_any` + `atnnode` responder (DEC-0021). Device SoT still blocked on T-0400. |
| T-0512 | done | REQ-5.2 | `ATN-REPORT-1` + `atnsign report` / `make report`. Emulator/S24 still open. |
