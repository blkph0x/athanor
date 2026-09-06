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

Phase 4 Device Owner waits on T-0400. Phase 5 air-gap **release** host
(REQ-5.1) unmeasured. ISS-0003 aarch64/S24 Poly1305. ISS-0007 IPv6.
Diag-first flash when jar lands (DEC-0027).
**T-0601 / ISS-0021:** release-only NIC-down measure (DEC-0037) — not lab.

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
| T-0303 | done | REQ-3.3 | ISS-0015: three UDP pairs + lossy AC drop in `test_hb` |
| T-0103 | done | REQ-1.2 | DEC-0022: IPv4 required path; pin source; HS retry; KA 15s |
| T-0513 | done | REQ-5.2 | DEC-0023: in-house fuzz + isolation scan + wrong-ek handshake. SoT still open. |
| T-0203 | done | REQ-2.1 | HTTP/1.1 keep-alive max 8 (DEC-0024). ISS-0010 closed. |
| T-0204 | done | REQ-2.3 | DNS TCP on recorded tcp_port when UDP port is busy (DEC-0024). |
| T-0405 | done | REQ-4.1 | `atnnode connect <file>` initiator (DEC-0024). Device SoT still blocked. |
| T-0600 | done | REQ-6.2 | `docs/ISOLATION.md` + export.ps1 scaffolding. SoT 6.x still open. |
| T-0304 | done | REQ-3.3 | DEC-0025: WARN/vote/grace, witness retrieve, console roster, atn_lock |
| T-0205 | done | REQ-2.1 | DEC-0026: `atnhttp serve-once` / `get`; ISS-0009 narrowed |
| T-0006b | done | REQ-1.1 | DEC-0026: Poly1305 wall-clock note in test_crypto (ISS-0003 still open) |
| T-0602 | done | REQ-6.3 | DEC-0026: `make export-tree`; isolation doc notes SoT in export tree |
| T-0700 | done | REQ-3.3/4.4 | DEC-0027/0028/0029 + cfg/dmon/report gates (BN-0025) |
| T-0406 | done | REQ-4.1 | DEC-0030: document stub→jar path; reject foreign Gradle/Node guide |
| T-0701 | done | REQ-3.3 | DEC-0031: dmon hub failover + `test_hub_failover` D-08 (BN-0027) |
| T-0702 | done | REQ-3.1 | DEC-0032: `ATN_REPL_MAX_NODES`/`ATN_CFG_MAX_HUBS` = 16 |
| T-0703 | done | REQ-4.1 | `atnnode connect` multi-hub failover (DEC-0031) |
| T-0800 | done | — | DEC-0033 crypto floor + Essential Eight map + recipe gate |
| T-0801 | done | REQ-1.2 | DEC-0035: PQ rekey REKEY_INIT/ACK + `test_tun` gate (ISS-0008) |
| T-0802 | done | REQ-2.2/3.3 | DEC-0034: console 2FA sets `outage_class` |
| T-0803 | done | REQ-2.2 | DEC-0036: WHATWG form percent-decode (ISS-0011) |
| T-0804 | done | REQ-6.2 | DEC-0037: lab online; NIC-down = release gate only |
| T-0805 | done | REQ-4.1 | DEC-0038: stub lab APK + skip USB policy; docs/LAB.md |
| T-0401b | done | REQ-4.1 | S24 stub APK ↔ atnnode listen ESTABLISHED (BN-0034) |
| T-0806 | done | REQ-4.1 | Lab UI live status + reconnect/ping; S24 situation matrix (BN-0035) |
| T-0807 | done | REQ-4.1/5.3 | DEC-0039 lab BOOM: 30s hub silence + wrong code x5 (BN-0036) |
| T-0808 | done | REQ-4.2 | DEC-0040 lab Device Admin watch-login K=5 BOOM (BN-0037) |
| T-0809 | done | REQ-4.1/5.3 | DEC-0041 lab BOOM on airplane/handshake without ESTABLISHED |
