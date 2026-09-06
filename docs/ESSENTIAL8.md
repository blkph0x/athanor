# Essential Eight map for Athanor (DEC-0033)

**Citation:** ASD / ACSC Essential Eight — prioritised strategies from the
*Strategies to Mitigate Cyber Security Incidents*
([cyber.gov.au Essential Eight](https://www.cyber.gov.au/business-government/asds-cyber-security-frameworks/essential-eight/essential-eight-maturity-model);
PROTECT “Essential Eight Explained”, Nov 2023).

**Honesty rule:** Essential Eight targets **internet-connected enterprise IT**.
Athanor is a **sovereign compile-yourself stack**. This file maps each
strategy to an in-tree control **or** an explicit operator/OS gap. It does
**not** claim an assessed Maturity Level for a Windows estate.

Crypto that carries data is governed by [`CRYPTO.md`](CRYPTO.md) (floor =
ML-KEM-1024 + ML-DSA-87 + ChaCha20-Poly1305-256). No E8 shortcut lowers that.

| # | Essential Eight strategy | Athanor analogue | Status |
|---|---|---|---|
| 1 | **Application control** | Only our binaries from this tree; no npm/apt product deps; recipe URL scan | **In tree** (`test_recipe`, SoT) |
| 2 | **Patch applications** | Air-gap sign + emulate-before-flash (REQ-5.x); no silent auto-update CDN | **Partial** — pen done; air-gap host open |
| 3 | **Restrict Microsoft Office macros** | N/A to product (no Office). Operator workstation policy | **Operator OS** |
| 4 | **User application hardening** | No browser console (DEC-0009); `atnhttp` operator client; charge-only USB (REQ-4.3) | **Partial** — device Knox blocked T-0400 |
| 5 | **Restrict administrative privileges** | Console mutate requires session + CSRF + **2FA**; Device/Profile Owner for Knox | **Partial** — console 2FA yes; Knox enroll open |
| 6 | **Patch operating systems** | Builder/CI OS is operator duty; we do not ship an OS | **Operator OS** |
| 7 | **Multi-factor authentication** | In-house HMAC-SHA-512 2FA on admin mutate (REQ-1.3 / 2.2); outage_class via 2FA (DEC-0034); phone password+biometric (REQ-4.2) | **Partial** — console yes; device open |
| 8 | **Regular backups** | Repl factor ≥2 + AEAD snapshots (REQ-3.1/3.2); export-tree for source | **Partial** — repl yes; operator backup schedule open |

## Beyond Essential Eight (project bar)

Essential Eight is a **floor for org IT**, not our ceiling:

- Post-quantum category-5 KEM + signatures on the wire and pen.
- Closed-loop: no package mirror, no CDN, no rented VPN.
- Self-wipe of **our** keys on DEAD / lockout / Faraday class (policy).
- Diag profile so lab soak does not brick (DEC-0027).

## Gaps we still track

| Gap | Tracker |
|---|---|
| Enrolled Knox endpoint hardening | T-0400 / ISS-0016 |
| PQ rekey on long-lived tunnels | Done (DEC-0035) |
| Console-set `outage_class` after 2FA | Done (DEC-0034) |
| Disconnected build proof | T-0601 / ISS-0021 |
| Air-gap sign host | REQ-5.1 |

Update this table in the same commit when a control lands.
