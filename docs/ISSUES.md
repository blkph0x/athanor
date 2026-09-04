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

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-1.2 / 5.1 (code signing)
- **Unknown:** DEC-0005 covers encryption/KEM. Category-5 *signatures* are
  ML-DSA-87 (FIPS 204). Not compiled in this tree yet. Do not claim
  quantum-proof authentication of binaries or handshake transcripts.
- **Must not invent:** a hash-based signature of our own; wait for FIPS 204
  line-by-line + KATs.
- **Unblock by:** implement ML-DSA-87 from FIPS 204 with ACVP/KAT vectors.

## ISS-0007 — Tunnel is IPv4-only

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-1.2 follow-on
- **Unknown:** DEC-0007 froze IPv4 UDP. IPv6 sockaddr layout is not specified.
- **Must not invent:** dual-stack without a DEC.
- **Unblock by:** DEC for IPv6 bind/send/recv, then code.

## ISS-0008 — Tunnel rekey not implemented

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-1.2 follow-on
- **Unknown:** Cause/effect map lists REKEY in the state machine. DEC-0007
  closes the session before seq wrap instead.
- **Must not invent:** a rekey message type not in TUNNEL.md.
- **Unblock by:** extend TUNNEL.md + DEC, then implement.

## ISS-0006 — GitHub Actions will not start: account billing lock

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** DEC-0006 pipeline
- **Evidence:** Run https://github.com/blkph0x/athanor/actions/runs/33848702338
  — all four jobs (linux-x86_64, linux-aarch64, darwin, windows-x86_64)
  annotated: "The job was not started because your account is locked due
  to a billing issue." Steps arrays were empty; this is not a Makefile
  failure.
- **Must not invent:** a green badge. Local `make test` passed; GitHub did
  not execute.
- **Unblock by:** fix billing on the GitHub account (Settings → Billing),
  then Actions → ci → Re-run all jobs on commit `2ca2be9` or later.

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
- **REQ:** REQ-1.1
- **Unknown:** RFC 8439 §3–§4 warn that naive bigint Poly1305 leaks via
  timing. Our implementation uses fixed 26-bit limbs (no secret-dependent
  branches in the inner mul). We have not yet timed it on the target CPUs.
- **Must not invent:** a "constant-time" badge in the SoT checkbox until a
  measurement exists in BUILD_NOTES or a dedicated test.
- **Unblock by:** after KATs pass, add a note on ISS-0003 in BUILD_NOTES with
  what we did (or did not) measure. SoT gate "constant-time checks" stays
  honest.

## ISS-0004 — ARM binaries not executed on the current builder

- **Status:** open
- **Opened:** 2026-09-04
- **REQ:** REQ-1.1 portability (DEC-0004)
- **Unknown:** This host has `gcc -dumpmachine` = `x86_64-w64-mingw32`, no
  `aarch64-linux-gnu-gcc` / `arm-linux-gnueabihf-gcc`, and WSL has zero
  distros. We cannot compile-or-run ARM here.
- **Must not invent:** a green ARM checkbox from an x86 run.
- **Unblock by:** run `make test` on an aarch64 Linux box, an ARM Windows
  box, or an Android NDK adb-run, and append BUILD_NOTES with that
  dumpmachine and `ALL PASSED`. GitHub job `linux-aarch64`
  (`ubuntu-24.04-arm`) is the public attempt (DEC-0006).
