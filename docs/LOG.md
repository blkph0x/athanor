# Development log

Newest at the top.

---

## 2026-09-06 — DEC-0043 corrected: boom on airplane/hub silence

- Freeze-on-ESTABLISHED was wrong: airplane keeps UDP ESTABLISHED.
- Restore lastHubMs + no-net clocks; MESH UP only when hub live.

---

## 2026-09-06 — Fix Linux CI: portable atnenroll + POSIX enroll UI

- CI linux-x86_64/aarch64 failed: `-Werror=unused-result` on `system(mkdir)`.
- Portable `mkdir`/`CreateDirectory`; `tools/enroll-console.sh` for POSIX serve.

---

## 2026-09-06 — DEC-0043 unreachable timer off while ESTABLISHED

- User: first enroll with MESH UP still showed unreachable timer running.
- Lab: freeze unreachable watch/BOOM while `TUN_ESTABLISHED`; arm only
  after leaving ESTABLISHED (post-join). UI: "unreachable timer OFF".

---

## 2026-09-06 — DEC-0042 lab enroll console (USB Connect & Enroll)

- User: signed enroll without air-gap yet; local-only web UI for policy +
  phone-number roster label + persistent **Connect & Enroll** over USB debug.
- Shipped: `atnenroll serve` → `tools/enroll-console.ps1` on 127.0.0.1:8799;
  adb install/push/start with `request_admin`; optional local `atnsign`
  receipt; docs/ENROLL.md. Air-gap = release beta. Knox DO still T-0400.

---

## 2026-09-06 — Narrow DEC-0041: no pre-join unreachable BOOM

- User: do not BOOM if not yet joined unless fully enrolled.
- Lab: airplane/hub-silence BOOM only after `sawEstablished`.
- Hub restarted for retest; S24 ESTABLISHED; post-join airplane OK.
- README/BN-0039 refreshed; tip waiting on T-0400 Knox jar.

---

## 2026-09-06 — DEC-0040 lab BOOM from real lock-screen fails

- User: must test from locked phone; detect 5 failed **device** unlocks
  (not only in-app codes). Wired `onPasswordFailed` + Device Admin
  prompt; stub skips USB/12-char policy. Lab BOOM at K=5, keys kept.
- Recipe in `docs/LAB.md`. SoT 4.2 still `[ ]` (no Knox enroll).

---

## 2026-09-06 — DEC-0039 lab BOOM (30s silence + code x5)

- User: Faraday/airplane/no-Wi‑Fi proxy; >30s no hub => 
  `BOOM phone is dead now`; wrong unlock code x5 => same (testing only).
- Lab APK: hub DATA probes; silence timer; EditText + 2FA verify fails;
  `log_only` keeps keys. S24: silence BOOM PASS; code #5 LOCKOUT BOOM PASS.
- ISS-0024 closed (lab). Faraday SoT still open.

---

## 2026-09-06 — Lab UI live status + situation matrix (BN-0035)

- Phone showed dead "Start mesh daemon" (no on-screen state). Reworked
  `AtnLabActivity`: live `state=` / MESH UP, Start/reconnect, lab ping.
- `AtnDaemonService.ACTION_RECONNECT` + notification text by tunnel state.
- `atnnode listen` fflush on `recv` so redirected hub logs show pings.
- S24 matrix: M1 ESTABLISHED PASS; M2 ping hub `recv 4` PASS; M3
  reconnect-while-up PASS; M4 hub-kill still ESTABLISHED (gap); M5 wrong
  ek phone CLOSED; M6 restore PASS. Skip D-09/D-10/Knox (release/T-0400).

---

## 2026-09-06 — Lab soak: S24 stub APK ↔ atnnode ESTABLISHED

- Device: SM-S901E (Android 16), Wi‑Fi YOUR_PHONE_LAN_IPV4; hub YOUR_HUB_LAN_IPV4:47000.
- Fixes: compile-time `AtnKnoxBuildFlags.STUB_BUILD` (Samsung system Knox
  made runtime ATN_STUB probe false); link `atn_cfg.c` into `libatn.so`;
  FGS type `dataSync`. Hub log: **ESTABLISHED**. USB policy skipped.

---

## 2026-09-06 — DEC-0038: stub lab APK for phone↔hub (no Knox yet)

- User: single hub + mock/no Knox; USB adb; USB charge-only is release-only.
- Keep Makefile stubs (reject Gradle/Node). `make android-apk` packs
  aapt2/d8/apksigner APK; Activity starts daemon; stub skips USB/password.
- `docs/LAB.md` recipe. SoT 4.x still `[ ]` until T-0400.

---

## 2026-09-06 — DEC-0037: lab online; NIC-down is release-only

- User: do not air-gap the development/testing host; that is counter
  to daily work (git, CI, Partner jar). REQ-6.2 disconnected measure
  stays a **release** gate (ISS-0021 / T-0601 backlog).
- Continuous lab isolation: `test_recipe` + `export-tree` only.
- Agents must not delete the default route on this builder. SoT unchanged.

---

## 2026-09-06 — DEC-0036: form percent-decode (T-0803 / ISS-0011)

- WHATWG URL Standard §5.1 / §1.3 in `atn_http_form_get` (`+`→SP, `%HH`).
- ASCII-only product restriction (NUL / non-ASCII rejected).
- Gate: `test_http` form_get cases. ISS-0011 closed.
- T-0601 still open: default-route remove needs elevated Admin
  (Access denied in non-elevated agent shell). SoT unchanged.

---

## 2026-09-06 — DEC-0035: PQ tunnel rekey (T-0801 / ISS-0008)

- Wire types 6/7 (REKEY_INIT / REKEY_ACK); initiator-only; stage until ACK;
  same ML-KEM-1024 + HKDF floor as handshake (DEC-0033).
- Gate: `test_tun` ESTABLISH → echo → `atn_tun_rekey_send` → pump → echo.
- ISS-0008 closed. SoT unchanged. Next: T-0601 or T-0400.

---

## 2026-09-06 — DEC-0033/0034: crypto floor + E8 + console outage 2FA

- User: Essential Eight or better; bleeding-edge PQ crypto; no compromise.
- DEC-0033: freeze ML-KEM-1024 / ML-DSA-87 / ChaCha20-Poly1305-256;
  forbid weaker algs + third-party crypto deps. Gate in `test_recipe`.
- Docs: `CRYPTO.md`, `ESSENTIAL8.md`; README posture + pipeline rows.
- DEC-0034: console `action=outage` after CSRF+2FA (T-0802).
- Opened T-0801 (PQ rekey). SoT unchanged.

---

## 2026-09-06 — DEC-0032: roster/hub cap 16 + README pipeline board

- Raised `ATN_REPL_MAX_NODES` and `ATN_CFG_MAX_HUBS` to 16 (match hb
  peers). Conf `hub2_*`…`hub16_*`; `ATN_REPL_MAX_REC` replaces 512-byte
  truncating buffers. Java `AtnNodeConfig.MAX_HUBS=16`.
- Gates: test_cfg hub2..10 + hub17 reject; test_repl init 16 / reject 17.
- README Status rebuilt: pipeline table + testing-phase readiness + REQ
  board. T-0702 done. T-0703: `atnnode connect` walks hubs via dmon
  (DEC-0031). SoT unchanged.

---

## 2026-09-06 — DEC-0031: D-08 hub failover wire path (T-0701)

- `atn_dmon_tun_connect_hub` / `atn_dmon_tun_failover` (3 HS attempts
  per hub; wipe clears pin; set_peer pins next hub).
- Gate: `tests/test_hub_failover.c` — dark hub0→hub1 echo; wrong-ek
  AUTH advance; all-dark failover keeps keys.
- ISS-0023 fully closed. T-0701 done. SoT 4–6 unchanged. T-0702 next
  for hub/repl cap >4.

---

## 2026-09-06 — DEC-0030: Knox test path is stubs (reject foreign Gradle/Node guide)

- User pasted a “no jar / USE_REAL_KNOX / Node brick lock” guide.
- Rejected for SoT (AndroidX, npm/Express, wrong jar path, no imports,
  DEVICE_BRICK_LOCK). Affirmed existing Makefile + android/stubs +
  vendor/knox/knoxsdk.jar. Rewrote docs/KNOX.md + vendor/knox/README.
- SoT 4.1 still `[ ]` until real jar + enroll (T-0400).

---

## 2026-09-06 — DEC-0027/0028/0029 + push to origin

- User: track everything; no guessing; prove gates; keep pushing GitHub;
  draft the three DECs and steam-roll.
- DEC-0027 diag profile (log_only/wipe_armed/report diag=).
- DEC-0028 hub2..hub4 list (cap 4 = repl).
- DEC-0029 outage_class (blackout HOLD vs faraday wipe).
- Gates: test_cfg / test_dmon / test_sign ALL PASSED (BN-0025).
- ISS-0022/0023 narrowed closed. T-0700 done. T-0701 wire failover and
  T-0702 repl-cap still open. SoT 4–6 unchanged.

---

## 2026-09-06 — Diag / multi-hub use-case analysis (no code policy)

- User: full use-case + diagnostics for a heavy test/diag first build;
  multi-site hubs like IRC; blackout must not brick phones; feature
  inventory.
- Added `docs/DIAG_USECASES.md` (UC-01..10, D-01..20, gaps, inventory).
- Opened ISS-0022 (blackout vs Faraday), ISS-0023 (single peer blocks
  failover), T-0700/T-0701. No SoT flips. No wipe-timing constants
  changed — those need DECs before code.
- Prior: DEC-0026 operator client / Poly1305 note / export-tree (BN-0024).

---

## 2026-09-06 — Keep moving without knox.jar (DEC-0026)

- User: track everything; no guessing; advance what does not need Knox.
- Verified DEC-0025: `make test` ALL PASSED; completed BN-0023.
- DEC-0026: `atnhttp serve-once` / `get` + demo conf reload (ISS-0009-b).
  Poly1305 wall-clock note (ISS-0003 still open for S24/ARM).
  `make export-tree` (avoids colliding with `export/`). ISS-0021 opened
  for real NIC-down measurement. Cause/effect roll-up Phase 2–3 → [X]
  to match SoT. SoT 4.x/5.x/6.x stay `[ ]`.
- BN-0024: default route up; poly1305 40 ms / 40 ms; export has SoT.

---

## 2026-09-04 — Org failsafe + mesh console (DEC-0025)

- No auto SMS (ISS-0020). Witness node id + WARN; recipients emit H.
- Grace G=3; HOLD vote cancels wipe; daemon flush on DEAD only.
- Console roster ATN-MESH-ROSTER + HOLD form (2FA). atn_lock recursive
  mutex. Still one TCP client at a time (memory-safe default).
- SoT 4.x/5.3 stay `[ ]`.

---

## 2026-09-04 — Non-Knox operator path (DEC-0024)

- HTTP keep-alive (max 8; Connection: close still one-shot). ISS-0010
  closed.
- DNS TCP binds a second port when the UDP port is taken. TCP A query
  gated. ISS-0012 pcap still open.
- `atnnode connect <file>`, `atn_cfg_load_file`.
- `docs/ISOLATION.md`, `tools/export.ps1`. SoT 4.x/5.x/6.x stay `[ ]`.

---

## 2026-09-04 — Headless pipeline gates (DEC-0023)

- User: keep working on what we can (Knox / air-gap / Faraday still blocked).
- `test_fuzz`: 4096 HTTP + 4096 DNS + 1024 cfg parse mutations
  (SHA-256 counter). Not N-hour REQ-6.1.
- `test_recipe` scans `src/` `include/` `android/` + Makefile for
  fetch URLs.
- Wrong-ek handshake: initiator AUTH-closes, does not ESTABLISH.
- `atnnode demo` does conf-driven loopback handshake + echo.
- SoT 5.2 / 6.1 / 6.2 stay `[ ]`.

---

## 2026-09-04 — IPv4 is the required heartbeat path (DEC-0022)

- User: must work on non-IPv6 networks; IPv4 reliably and safely; no
  compromise on heartbeat connectivity.
- Sockets stay `AF_INET`. No IPv4-mapped IPv6. `peer_ipv4` is enough.
- Pin source IPv4+port (stray UDP does not AEAD-close). HS_INIT retry.
  Duplicate INIT ignored. Daemon 1s pump, 15s KA, hb tick still 60s.
  dmon pump on ESTABLISHED ingests hb DATA. hb.tun attaches when the
  tunnel becomes ready.
- ISS-0007 remains open (separate AF_INET6 later).

---

## 2026-09-04 — ISS-0015 three UDP heartbeat pairs

- `test_hb` handshakes AB/AC/BC on loopback, three LIVE rounds, then
  drops AC for N buckets. A↔C UNTRUSTED, B still trusts A, nodes live.
- ISS-0015 closed. SoT 3.3 was already [X]; this is the residual UDP
  mesh gate, not a new requirement.

---

## 2026-09-04 — Lab node file + signed report + bind-any (DEC-0021)

- User: keep rolling what we can (still waiting on PTR / Knox Partner).
- `atn_tun_bind` stays loopback. `atn_tun_bind_any` is INADDR_ANY.
  JNI `tunBind` uses bind-any. Tests keep loopback.
- `atn-node.conf`: `peer_ipv4` / `peer_port` / `peer_ek`. Unknown keys
  fail. Incomplete file means do not connect. Daemon reads
  `filesDir/atn-node.conf` and initiates. INTERNET is for that UDP hop.
- `atnnode listen [port]` is the lab responder; `atnnode demo` gates
  cfg roundtrip + bind-any.
- Report: `ATN-REPORT-1` / `atnsign report` / `make report`, ctx
  `atn-rp-v1`. SoT 4.1 / 5.1 / 5.2 stay `[ ]`.

---

## 2026-09-04 — Daemon tunnel + source manifest list (DEC-0020)

- `atn_dmon` embeds the UDP tunnel. Two-dmon loopback handshake + echo
  in `test_dmon`. Flush closes the socket. JNI tun* for the Android
  daemon. Lab IP is not hardcoded.
- `atnsign manifest tools/src.list MANIFEST` plus `make manifest`.
  `tests/test_recipe` fails if the product Makefile grows a fetch URL.
- SoT 4.1 / 5.1 stay `[ ]`.

---

## 2026-09-04 — Knox stub switch + atnsign (DEC-0019)

- User: keep moving; stub knox.jar until PTR + Partner account; patch
  the real jar in later.
- `make android-java` uses `vendor/knox/knoxsdk.jar` when present,
  otherwise `android/stubs` with `ATN_STUB=true`. Daemon logs
  `knoxStub=`. SoT 4.1 stays `[ ]`.
- `atnsign`: SHA3-256 path-sorted manifest, ML-DSA-87 ctx `atn-mf-v1`.
  Demo + `tests/test_sign` gate sort/sign/verify/tamper. REQ-5.1 still
  needs an air-gapped host.

---

## 2026-09-04 — ML-DSA-87 (FIPS 204)

- DEC-0018: category-5 signatures are ML-DSA-87 only. Pure ML-DSA,
  hedged Sign, internal APIs for CAVP.
- ACVP-Server `ML-DSA-keyGen-FIPS204` first ML-DSA-87 vector: pk+sk match.
  `ML-DSA-sigGen-FIPS204` tgId=12 Sign_internal deterministic: signature
  matches; verify accepts; tamper rejects; hedged roundtrip works.
- Incremental SHAKE256 finalize now zeros the unused rate tail (FIPS 202
  padding). Bug only showed up when μ = H(tr∥M′) spanned >1 rate block;
  short absorbs (keygen ExpandS, ML-KEM SampleNTT) had been zero from init.
- ISS-0005 closed. SoT REQ-5.1 stays `[ ]` — we have the pen, not the
  air-gap signing factory.

---

## 2026-09-04 — Keep going after network hiccup

- Ping 8.8.8.8 works from the builder (18 ms). github.com resolves via
  1.1.1.1 to 4.237.22.38. knoxsdk.jar still Partner-only (ISS-0016).
- DEC-0017: native dmon flushes on hb UNTRUSTED/DEAD and 2FA lockout.
  Java wraps device||cluster||id under AndroidKeyStore AES-GCM; wrap
  file deleted on flush. DPM min letters/numeric + password-fail K=5.
  USB re-assert on POWER_CONNECTED.
- SoT 4.1–4.4 stay `[ ]` — no enrolled device, no knoxsdk.jar.
- ISS-0019: 60s hb bucket is policy, not a Faraday measurement.

---

## 2026-09-04 — Knox toolchain: NDK in, jar still Partner-only

- User asked to download/install so Phase 4 can start.
- Installed Android cmdline-tools 14742923 and NDK r27d (hash verified).
- Native mesh cross-compiles to aarch64 `libatn.so`. Daemon Java compiles
  against documented Knox API names via stubs.
- Cannot obtain knoxsdk.jar without a Knox Partner login (ISS-0016).
  REQ-4.1 SoT checkbox stays open. Next human step: drop the jar at
  `vendor/knox/knoxsdk.jar` and enroll a phone as Device Owner.

---

## 2026-09-04 — REQ-3.1 + REQ-3.3

- DEC-0013 replication: SHA3-256 shard, vector clocks, factor 2,
  cluster AEAD over the UDP tunnel. Catch-up + kill-A gates green.
- DEC-0014 heartbeat: HMAC-SHA-512 tokens, N=3 UNTRUSTED, M=3 self-wipe.
  Forged MAC ignored. ISS-0015: 3-node path is in-process plus one UDP hop.
- SoT Phase 3 [X] for 3.1/3.2/3.3. Next: Knox (blocked, no SDK) or
  ML-DSA-87 (ISS-0005).

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
