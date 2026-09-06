# Diagnostic build, use cases, multi-hub model

**Status:** planning companion (2026-09-06). Not a SoT checkbox flip.  
**Law:** `SOURCE_OF_TRUTH.md`. Sequencing: `CAUSE_EFFECT_MAP.md`.  
**Rule:** never guess. Anything marked **NEEDS DEC** must get a decision
record before code ships that behavior.

This file answers: how we test without bricking phones/hubs; how multi-site
hubs should behave (IRC-like); why power blackouts must not self-wipe
enrolled devices; and what features exist vs wait.

---

## 1. Target operating picture (desired)

Think **IRC network**, not “one VPN box”:

| IRC idea | Athanor analogue |
|---|---|
| IRC server / hub | Site hub (`atnnode` + repl + hb + dns + admin) |
| Network of servers | Multi-jurisdiction hub mesh (REQ-3.x) |
| Channel still up if one server dies | Sharded replicas (factor ≥2) + catch-up |
| Client reconnects to another server | Phone/daemon may attach to **any enrolled hub** |
| Netsplit | Partition: WARN + grace + HOLD vs wipe (DEC-0025) |
| Oper / admin | 2FA console (`atnhttp`) |

**Power-outage failsafe (user requirement):** a site blackout must **not**
cause phones to flush keys just because every local radio path went dark.
Capture / Faraday / seizure still must be able to flush. Those are different
causes and must stay distinguishable — **NEEDS DEC** (see §4).

---

## 2. What we already have (evidence)

### Hub / mesh (Phase 1–3 SoT [X])

| Capability | Where | Limits today |
|---|---|---|
| PQ tunnel (ML-KEM-1024 + AEAD) | `atn_tun`, DEC-0007/0022/0035 | IPv4 required; PQ rekey (DEC-0035); no IPv6 (ISS-0007) |
| Lab hub responder / initiator | `atnnode listen|connect|demo` | One peer in conf |
| Replication factor 2, vector clocks | `atn_repl`, DEC-0013/0032 | Cap **16** nodes/hubs |
| Heartbeat + WARN + grace G=3 + HOLD | `atn_hb`, DEC-0025 | Flush on **DEAD only** (dmon); ~N+G buckets silence |
| Witness retrieve (HMAC, no SMS) | DEC-0025 / ISS-0020 | Witness is roster id, not auto-dial |
| Admin console + roster HOLD/WIPE | `atnhttp` | Loopback / DEC-0009 records; operator CLI DEC-0026 |
| Authoritative DNS scaffold | `atndns` `atn.test` | Static zone; not live hub directory yet |
| Phone daemon scaffolding | `android/` + `atn_dmon` | Stub Knox; **single** `peer_ipv4` in conf |

### Timing math already frozen (policy, not Faraday-proven)

- Bucket = 60 s (`ATN_DMON_HB_BUCKET_SEC`, DEC-0017 / ISS-0019)
- N = 3 → UNTRUSTED + WARN (~180 s silence)
- G = 3 grace → DEAD/wipe if `hold_votes==0` (~180 s more)
- **Worst case auto-wipe after total silence ≈ 6 minutes** if no HOLD

That is **too aggressive for a multi-hour regional blackout** unless HOLD
is automatic for “all hubs dark / power class” events — **NEEDS DEC**.

### What DEC-0025 already bought us

- Silence ≠ instant wipe (grace)
- Operator/peer HOLD cancels wipe
- Peers never wipe someone else’s RAM
- Daemon flush moved to DEAD only (not UNTRUSTED)

What it does **not** yet buy:

- Live multi-hub wire failover on the phone (PC harness done T-0701; device still needs jar)
- Automatic RF/power sensing of blackout vs bag (out of scope; DEC-0029 is conf class)
- Hub roster > 16 for replication (would need a new DEC; 16 = DEC-0032)
- Device Knox enroll (needs jar — T-0400)

---

## 3. Diagnostic / first-field build (no brick)

### Lab / CI (now) — stub Knox + diag

See `docs/KNOX.md` (DEC-0030). `make android-java` without
`vendor/knox/knoxsdk.jar` is the testing compile. Product Java already
imports `com.samsung.android.knox.*`; stubs supply those names. Pair with
`diag=1` / `flush_mode=log_only` (DEC-0027) on the PC mesh. Do not flash
stub builds as enrolled devices.

**Goal:** first flashed/enrolled build is a **heavy test + diag** profile.
Keys can be exercised; wipe paths are **logged and gated**, not destructive
to the lab fleet by default.

### 3.1 Profile flags (DEC-0027 — proven on builder)

| Flag | Diag default | Production default | Purpose |
|---|---|---|---|
| `diag=1` | on | absent/`0` | Entire profile; stamped into `ATN-REPORT-1` |
| `flush_mode` | `log_only` | `zeroize` | Diag counts DEAD events without zeroizing keys |
| `wipe_armed=1` | off | off | Forces ZEROIZE even in diag |
| `outage_class` | conf (DEC-0029) | `normal` | blackout/maintenance HOLD; faraday/capture wipe |
| hub2..hub16 | optional | optional | Failover list (DEC-0028/0032); wire DEC-0031 |

USB charge-only lab unlock still needs Knox + DEC for time-boxed debug.

### 3.2 What “no brick” means (measurable)

| Brick class | Diag rule |
|---|---|
| Phone self-wipe on lab RF drop | Forbidden unless `wipe_armed` + `ZEROIZE` |
| Factory reset via DPM | Already not enabled (DEC-0017) — keep off in diag |
| Lost wrap file / cannot re-enroll | Diag keeps wrap; production deletes wrap on flush |
| Hub OOM / crash loop | Soft restart; do not wipe peer keys on crash |
| Bad OTA | Emulate-before-flash (REQ-5.2); diag refuses unsigned |

### 3.3 Diag artifacts (already partly exist)

| Artifact | Status |
|---|---|
| `make test` / `make report` / `ATN-REPORT-1` | Done (pen); air-gap host open |
| `tests/test_fuzz` short mutator | Done; N-hour REQ-6.1 open |
| `atnhttp get` operator client | Done (DEC-0026) |
| Hub sim: multi-`atnnode` + lossy drop | Partial (`test_hb` 3-pair) |
| Phone diag APK (stub Knox) | Compile only; device SoT blocked |
| Blackout / multi-hub failover harness | Done (DEC-0031 / `test_hub_failover`) |

---

## 4. Use cases (operational)

### UC-01 — Stand up a site hub
Operator compiles, runs `atnnode listen`, publishes `peer_*` into DNS/roster,
opens `atnhttp` console, enrolls 2FA.  
**Gate today:** loopback + lab bind_any.  
**Gap:** signed hub identity in DNS TXT; multi-hub roster UI.

### UC-02 — Second site hub joins and syncs
Hub B handshakes to A, catch-up REPL, both LIVE on hb.  
**Gate today:** `test_repl` A→B + kill-A; `test_hb` 3 nodes.  
**Gap:** >4 nodes; WAN latency; roster epoch rotation.

### UC-03 — Phone attaches to any hub (IRC reconnect)
Daemon tries hub list in order (or DNS SRV/A set we own). One hub down →
next. Same device key / 2FA.  
**Gate today:** single `peer_ipv4`.  
**Gap:** **NEEDS DEC** hub list + pin rules (DEC-0022 pin must not forbid failover).

### UC-04 — Site blackout (power failsafe)
All hubs at site S lose power. Phones elsewhere still reach other sites.
Phones **at** site S may see total mesh dark if they only listed S.  
**Required effect:** no DEAD wipe solely from blackout class.  
**Gap:** event class + auto-HOLD or longer grace — **NEEDS DEC**. Conflicts
with naive Faraday (REQ-5.3) unless causes are separated.

### UC-05 — Faraday / capture
RF isolation while enrolled, or seizure.  
**Required effect:** flush after policy (grace/HOLD rules).  
**Gap:** bag test (REQ-5.3); ISS-0019.

### UC-06 — Hub seized, replicas live
Kill hub A; factor-2 copy on B still serves. Console marks A DEAD.  
**Gate today:** `test_repl` kill-A.  
**Gap:** console wipe of A’s *disk* snapshot policy.

### UC-07 — Operator HOLD during maintenance
Planned outage: operator HOLD before taking hubs down.  
**Gate today:** console HOLD form + `test_hb` hold survives.  
**Gap:** scheduled HOLD window; multi-operator audit log.

### UC-08 — Wrong peer / forged heartbeat
Forged MAC ignored; stray UDP does not AEAD-close (DEC-0022).  
**Gate today:** `test_hb` forge; `test_tun` stray/bad-ek.

### UC-09 — Diag fleet soak
N phones + M hubs, forced partitions, no ZEROIZE. Counters in report.  
**Gap:** harness + `ATN_DIAG` DEC.

### UC-10 — Air-gap sign + emulate flash
Build disconnected, sign manifest, PASS report before flash.  
**Gap:** REQ-5.1 host, REQ-5.2 emulator/S24, REQ-6.2 NIC-down (ISS-0021).

---

## 5. Diagnostic cases (test matrix)

Legend: **H** = hub PC, **P** = phone (or dmon sim), **D** = diag profile.

| ID | Scenario | Actors | Expect | Today |
|---|---|---|---|---|
| D-01 | Happy path handshake + echo | H↔H | ESTABLISHED, payload hidden | `test_tun` PASS |
| D-02 | Replay / bad MAC / bad ek | H↔H | drop / close / not ESTABLISHED | PASS |
| D-03 | Repl factor-2, kill A | H₁ H₂ | B serves | PASS |
| D-04 | 3-hub hb, lossy link | H×3 | UNTRUSTED pair, others LIVE | PASS |
| D-05 | Silence → grace → HOLD | H | stays LIVE | PASS |
| D-06 | Silence → grace → DEAD wipe | H/Dmon | keys zero **only if ZEROIZE** | PASS on PC; phone open |
| D-07 | Total dark + diag auto-HOLD | P+D | no wipe | **NEEDS DEC + test** |
| D-08 | Blackout site S, hubs T/U up | P with hub list | failover to T | PASS (`test_hub_failover`) |
| D-09 | Faraday bag production | P | flush after policy | REQ-5.3 open |
| D-10 | USB charge-only | P | no MTP/ADB | Knox blocked |
| D-11 | 2FA lockout K=5 | P/dmon | flush | dmon PASS; device open |
| D-12 | Console wipe without 2FA | H | 401 | PASS |
| D-13 | DNS out-of-zone | H | REFUSED, no forward | PASS |
| D-14 | Fuzz HTTP/DNS/cfg | H | no crash (short) | PASS; N-hour open |
| D-15 | Recipe URL scan | build | no fetch URLs | PASS |
| D-16 | Export tree | build | SoT+map present | `export-tree` PASS |
| D-17 | Disconnected build | build | PASS offline | ISS-0021 |
| D-18 | Multi-hub catch-up after split | H×3 | clocks converge | partial |
| D-19 | Phone switches hub mid-session | P | re-HS, hb LIVE | **missing** |
| D-20 | Diag report stamps ATN_DIAG | all | report field | **NEEDS DEC** |

---

## 6. Gaps vs IRC multi-hub + blackout (priority)

Ordered for **diag-first**, still no Knox required until noted.

1. **DEC: diag profile** (`ATN_DIAG`, `flush_mode`, wipe arming, report stamp)
2. **DEC: multi-hub peer list** for daemon/conf (ordered failover; pin rules vs DEC-0022)
3. **DEC: outage classes** — blackout / maintenance / Faraday / capture → different wipe policy
4. **DEC: raise `ATN_REPL_MAX_NODES`** (and maybe factor) with migration note
5. **Harness:** 3+ `atnnode` processes, scripted partition, D-07/D-08 without phones
6. **DNS as hub directory** — A/TXT for enrolled hubs only (still no public recursion)
7. Then Knox jar → device gates for D-09/D-10 on **diag** APK first

---

## 7. Feature inventory (pipeline)

### Basic — completed (SoT / gates green on builder)

- SHA-256, HMAC, HKDF, ChaCha20-Poly1305, ct_equal, memzero, OS CSPRNG
- SHA-512 / HMAC-SHA-512 / HKDF-SHA-512
- SHA-3 / SHAKE (FIPS 202)
- ML-KEM-1024 key establishment (FIPS 203)
- ML-DSA-87 signatures (FIPS 204) — pen, not air-gap factory
- UDP tunnel: HS, AEAD data, replay window, KA, IPv4 pin, HS retry
- Challenge-response 2FA + lockout
- HTTP/1.1 listener on tunnel records (not browser TLS)
- Embedded admin console (CSRF + 2FA mutate)
- Operator CLI `atnhttp serve-once` / `get`
- Authoritative DNS `atn.test` (UDP + fallback TCP port)
- AVL memory tree + AEAD snapshot
- Sharded replication + vector clocks + catch-up
- Heartbeat mesh WARN / grace / HOLD / self-wipe
- Lab hub `atnnode` + `atn-node.conf`
- Native dmon session, flush-on-DEAD, 2FA lockout flush (PC)
- Manifest + report signing tools
- Short in-house fuzz + recipe isolation scan
- `make export-tree` scaffolding
- Portable Makefile / CI same as local

### Basic — waiting / blocked

- Real Knox SDK + Device Owner enroll (T-0400 / ISS-0016)
- Device biometric + 12-char policy on hardware (REQ-4.2)
- USB charge-only on hardware (REQ-4.3)
- Attested flush on hardware (REQ-4.4)
- Air-gap builder (REQ-5.1)
- Emulate-before-flash on emulator/S24 (REQ-5.2)
- Faraday bag verification (REQ-5.3 / ISS-0019)
- N-hour fuzz + leak (REQ-6.1)
- Disconnected NIC proof (REQ-6.2 / ISS-0021)
- Clean-room export hash match (REQ-6.3)
- Form percent-decoding (ISS-0011)
- DNS pcap proof (ISS-0012)
- Poly1305 timing on ARM/S24 (ISS-0003)

### Advanced — completed (builder)

- Category-5 PQ handshake + signatures
- Implicit reject path (ML-KEM)
- Peer pin + stray-UDP immunity
- Wrong-ek handshake fail closed
- Org failsafe: witness retrieve + vote before wipe
- Mesh roster on console
- Stub/real Knox classpath switch
- Android Keystore wrap path (code; device unproven)
- Cross-OS crypto portability + aarch64 CI

### Advanced — in pipeline / needs DEC or hardware

| Feature | Status |
|---|---|
| Multi-site hubs, any phone → any hub | **Desired; NEEDS DEC** (hub list + failover) |
| IRC-like channel continuity across hub death | Partial (repl factor 2); scale NEEDS DEC |
| Power-blackout failsafe (no phone brick) | Partial (grace/HOLD); class-based policy **NEEDS DEC** |
| Diag/heavy test build profile | **NEEDS DEC** + harness |
| Automatic HOLD on total darkness | **NEEDS DEC** (tension with Faraday) |
| Hub directory via our DNS | Scaffold only |
| Tunnel rekey | Done — DEC-0035 / ISS-0008 |
| IPv6 second socket | ISS-0007 open |
| Browser TLS 1.3 subset | ISS-0009 option (a) open; (b) operator client done |
| Concurrent HTTP worker pool | Explicitly deferred DEC-0025 |
| PSTN/SMS heartbeat | Forbidden without DEC (ISS-0020) |
| TIMA firmware hook | Will not do (ISS-0018) |

---

## 8. Recommended sequence (no Knox yet)

1. Record **DEC-diag** + **DEC-hub-list** + **DEC-outage-class** (before more wipe logic).
2. Build hub-only failover/blackout harness (D-07/D-08 with `flush_mode=LOG_ONLY`).
3. Raise repl node cap under a DEC; extend soak tests.
4. Stamp `ATN_DIAG` into reports; keep SoT 4.x `[ ]`.
5. When `knoxsdk.jar` lands: flash **diag** APK first, never production wipe profile.

---

## 9. Tracker hooks

| ID | Action |
|---|---|
| T-0700 | open — author DECs for diag profile + multi-hub + outage class |
| T-0701 | done — hub failover / blackout harness (DEC-0031) |
| ISS-0022 | closed (narrowed) — blackout vs Faraday via outage_class |
| ISS-0023 | closed — hub list + wire failover |

Do not mark SoT Phase 4–6 until their cause/effect gates run.
