# Issues (canonical)

Never delete a row. Close with a commit hash and a sentence.

Status: `open` | `closed`

---

## ISS-0001 — Public-key primitive for the tunnel handshake is unspecified

- **Status:** closed (narrowed)
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — DEC-0005: KEM is ML-KEM-1024 (FIPS 203). Remaining
  work is packet layout (T-0100), not algorithm choice.
- **REQ:** REQ-1.2

## ISS-0005 — ML-DSA-87 signatures not yet implemented

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — DEC-0018, `atn_mldsa.c`, ACVP keyGen +
  Sign_internal KATs ALL PASSED (`tests/test_mldsa`). REQ-5.1 pipeline
  (air-gap compile + sign artifacts) is still open; the primitive is not.
- **REQ:** REQ-1.2 / 5.1 (code signing)

## ISS-0007 — Tunnel is IPv4-only

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-1.2 follow-on
- **Unknown:** DEC-0007 froze IPv4 UDP. IPv6 sockaddr layout is not specified.
- **Must not invent:** dual-stack without a DEC. DEC-0022 forbids
  IPv4-mapped `AF_INET6` (breaks IPv4-only nets). IPv4 is the required
  heartbeat path; IPv6 must be a second socket.
- **Unblock by:** DEC for a separate `AF_INET6` bind/send/recv that
  cannot fail the IPv4 path, then code.

## ISS-0008 — Tunnel rekey not implemented

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-06 — DEC-0035 + TUNNEL.md types 6/7;
  `atn_tun_rekey_send` + `tests/test_tun` ESTABLISH→echo→rekey→echo.
- **REQ:** REQ-1.2 follow-on
- **Unknown:** Cause/effect map lists REKEY in the state machine. DEC-0007
  closes the session before seq wrap instead.
- **Must not invent:** a rekey message type not in TUNNEL.md.
- **Unblock by:** extend TUNNEL.md + DEC, then implement.

## ISS-0006 — GitHub Actions will not start: account billing lock

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — Run https://github.com/blkph0x/athanor/actions/runs/33851841144
  actually started jobs. linux-x86_64 and linux-aarch64 **passed**.
  windows-x86_64 and darwin failed on real compiler issues (fixed in
  the REQ-2.3 commit: unterminated-string-init; Darwin `_DARWIN_C_SOURCE`
  for `arc4random_buf`). Billing is no longer the blocker.
- **REQ:** DEC-0006 pipeline

## ISS-0002 — SHA-256 empty/abc/two-block fixtures need FIPS page confirmation on audit

- **Status:** open (accepted for use with citation; confirm page numbers when
  a FIPS 180-4 PDF is in the air-gap library)
- **Opened:** 2026-09-04
- **REQ:** REQ-1.1
- **Unknown:** Exact FIPS 180-4 appendix page numbers were not fetched as a
  PDF in this session. The three classic fixtures are universally published
  and match every independent SHA-256; RFC 4231/5869/8439 KATs *were* read
  from the RFC Editor in this session.
- **Must not invent:** additional SHA-256 constants beyond RFC 6234 §5.1 and §6.1
  (those *were* transcribed from the RFC text this session).
- **Unblock by:** drop FIPS 180-4 into `docs/refs/` (plain documentation, not
  a linked library) and tick the page numbers.

## ISS-0003 — Constant-time Poly1305 is a property we must measure, not claim

- **Status:** open
- **Opened:** 2026-09-04
- **Updated:** 2026-09-06 — DEC-0026: `tests/test_crypto` prints
  `note poly1305 wall N=50000 msg=1024 key_a_ms=… key_b_ms=…` via
  `clock()` on this builder. Gate is “ops completed”, not a delta
  threshold. Numbers recorded in BUILD_NOTES. Still **not** measured on
  enrolled S24 / ARM targets; still not a formal CT proof.
- **REQ:** REQ-1.1
- **Unknown:** RFC 8439 §3–§4 warn that naive bigint Poly1305 leaks via
  timing. Our implementation uses fixed 26-bit limbs (no secret-dependent
  branches in the inner mul). Target-CPU timing lab still missing.
- **Must not invent:** a "constant-time" badge in the SoT checkbox until a
  measurement exists on the CPUs we care about.
- **Unblock remaining:** repeat the probe (or a better harness) on
  aarch64 / S24 and record BN; do not invent a pass/fail delta.

## ISS-0018 — TIMA Keystore enable APIs are dead on S24–S26

- **Status:** open (accepted: we do not call them)
- **Opened:** 2026-09-04
- **REQ:** REQ-4.1 / 4.4
- **Evidence:** Samsung deprecated TIMA/CCM in Knox 3.7; APIs fail on
  Android 12 / Knox 3.8. Target phones are newer.
- **Must not invent:** a TIMA firmware hook.
- **Unblock by:** DEC-0016 Android Keystore path (implemented). Close
  when a device run shows `atn-device` is hardware-backed.

## ISS-0019 — Heartbeat bucket period is policy, not a measured Faraday test

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-4.4 / 5.3
- **Unknown:** DEC-0017 set 60-second buckets so N=3 is 180 seconds of
  silence before UNTRUSTED flush. That is a documented policy choice
  from the elevator note in cause/effect, not a bag-in-lab measurement.
- **Must not invent:** a “Faraday verified” badge from the Windows
  `test_dmon` counter ticks.
- **Unblock by:** enrolled S24–S26 in a measured Faraday bag (REQ-5.3
  gate) after knoxsdk.jar (ISS-0016).

## ISS-0016 — knoxsdk.jar is not downloadable without a Knox Partner login

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-4.1
- **Unknown:** Samsung hosts the SDK behind partner.samsungknox.com. This
  session cannot complete that login. NDK r27d and cmdline-tools **were**
  installed. Daemon Java compiles against in-tree stubs.
- **Must not invent:** a fake Knox jar, or a SoT [X] for 4.1 without a
  device run.
- **Unblock by:** user signs in (after PTR/ISP if needed), drops
  `vendor/knox/knoxsdk.jar`, enrolls an S24–S26 as Device Owner /
  Profile Owner. Until then `make android-java` compiles
  `android/stubs` (`ATN_STUB=true`). That is not a device build.

## ISS-0017 — Builder LAN DNS does not resolve dl.google.com / github.com

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** toolchain
- **Evidence:** `nslookup` via local LAN DNS timed out.
  `1.1.1.1` resolved the names. Install used curl `--resolve`.
- **Must not invent:** product code that talks to 8.8.8.8.
- **Unblock by:** fix LAN DNS or keep `--resolve` in the builder notes.

## ISS-0015 — Three-node heartbeat mesh is not three UDP sockets

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — `tests/test_hb` now handshakes three UDP pairs
  (AB, AC, BC), keeps all three LIVE, then drops AC both ways for N
  buckets. A and C mark each other UNTRUSTED; B still trusts A; nodes
  stay LIVE. In-process 3-node path remains as the original gate.
- **REQ:** REQ-3.3 follow-on

## ISS-0012 — No tcpdump of DNS; TCP may not share the UDP ephemeral port on Windows

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-2.3 follow-on
- **Unknown:** Cause/effect asked for a packet capture showing no
  8.8.8.8 / 1.1.1.1. We proved construction: last peer is 127.0.0.1 and
  there is no forward `sendto`. TCP bind to the same ephemeral UDP port
  failed on this Windows host; UDP still answers. POSIX should bind both.
- **Must not invent:** a pcap we did not take.
- **Progress:** DEC-0024 binds a second TCP port when same-port bind
  fails; `atn_dns_tcp_port` + TCP A query in `test_dns`.
- **Unblock remaining:** run tcpdump/windump in BN (pcap still not
  taken).

## ISS-0011 — `application/x-www-form-urlencoded` percent-decoding not implemented

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-06 — DEC-0036; WHATWG URL Standard §5.1 / §1.3 in
  `atn_http_form_get`; `tests/test_http` form_get gates.
- **REQ:** REQ-2.2
- **Unknown:** DEC-0010 rejects `%` and `+` so we do not ship a guessed
  decoder. Field values are hex or tokens (`wipe`).
- **Must not invent:** a partial `%XX` decoder “good enough for now”.
- **Unblock by:** transcribe application/x-www-form-urlencoded from the
  WHATWG URL spec or RFC 1866 §8.2.1 with tests, then a DEC.

## ISS-0009 — Listener is not RFC 8446 TLS; browsers cannot connect

- **Status:** closed (narrowed)
- **Opened:** 2026-09-04
- **Closed:** 2026-09-06 — DEC-0026 ships in-tree operator client
  (`atnhttp serve-once` / `atnhttp get` + conf reload in `demo`).
  Option (b) from the unblock list. Browsers still cannot connect;
  that is intentional until a new DEC cites a TLS 1.3 subset (option a).
  Do not advertise DEC-0009 records as “TLS”.
- **REQ:** REQ-2.1 follow-on

## ISS-0010 — HTTP/1.1 keep-alive and pipelining are not implemented

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — DEC-0024: HTTP/1.1 persist, max 8 requests,
  honor `Connection: close`. No pipelining we emit. `tests/test_http`
  two-GET keep-alive. Pipelining as a client feature is still not
  offered.
- **REQ:** REQ-2.1 follow-on

## ISS-0020 — No automatic PSTN/SMS heartbeat fallback

- **Status:** open (accepted: we do not dial)
- **Opened:** 2026-09-04
- **REQ:** REQ-3.3 follow-on
- **Evidence:** DEC-0025. Carrier SMS/voice APIs are third-party. Witness
  retrieve is HMAC WARN + immediate H over our tunnel. `witness_id` is
  a roster node, not an E.164 auto-dial.
- **Must not invent:** Twilio/Nexmo/modem AT without a new DEC and a
  present device.
- **Unblock by:** an in-house modem/AT DEC if hardware exists.

## ISS-0021 — Disconnected-NIC / no-default-route build not measured

- **Status:** open (release gate only — DEC-0037)
- **Opened:** 2026-09-06
- **REQ:** REQ-6.2
- **Unknown:** Lab/development builders stay online (DEC-0037). Taking
  the NIC down or deleting `0.0.0.0/0` is a **release** measurement, not
  a daily testing requirement. Recipe URL scan + `make export-tree` are
  the continuous isolation gates (DEC-0024/0026).
- **Must not invent:** a SoT 6.2 `[X]` from a connected `make test`; must
  not air-gap the development host as standing practice.
- **Unblock by:** on a dedicated release host (or an explicit elevated
  release session), run `make test` + `make export-tree` with no default
  route and file BN. Do not treat T-0601 as lab “Now” work.

## ISS-0022 — Blackout vs Faraday wipe policy is ambiguous

- **Status:** closed (narrowed)
- **Opened:** 2026-09-06
- **Closed:** 2026-09-06 — DEC-0029: `outage_class` conf
  (normal/maintenance/blackout/faraday/capture). Blackout/maintenance
  inject HOLD (no DEAD wipe). Faraday/capture keep wipe path. Automatic
  RF/power sensing remains out of scope (no cited sensor).
- **REQ:** REQ-3.3 / 4.4 / 5.3
- **Residual:** console 2FA to set class; Faraday bag measurement ISS-0019.

## ISS-0023 — Single `peer_ipv4` blocks IRC-like multi-hub failover

- **Status:** closed
- **Opened:** 2026-09-06
- **Closed:** 2026-09-06 — DEC-0028 conf list; DEC-0031
  `atn_dmon_tun_connect_hub` / `atn_dmon_tun_failover` +
  `tests/test_hub_failover.c` (D-08). Cap 4 until T-0702.
- **REQ:** REQ-4.1 / 3.3

## ISS-0024 — Phone stays ESTABLISHED after hub process kill

- **Status:** closed (lab narrowed)
- **Opened:** 2026-09-06
- **Closed:** 2026-09-06 — DEC-0039 lab 30s hub-silence BOOM on S24
  (BN-0036). Production Faraday timing still ISS-0019 / REQ-5.3.
- **REQ:** REQ-4.1 / 1.2 lab soak
- **Residual:** production dead-peer / Faraday bag constants unchanged.

## ISS-0004 — ARM binaries not executed on the current builder

- **Status:** closed
- **Opened:** 2026-09-04
- **Closed:** 2026-09-04 — GitHub job `linux-aarch64` on
  https://github.com/blkph0x/athanor/actions/runs/33851841144
  ran `make test` and `make lib` successfully on `ubuntu-24.04-arm`.
  This Windows builder still has no ARM compiler; public ARM execution
  is the evidence.
- **REQ:** REQ-1.1 portability (DEC-0004)
