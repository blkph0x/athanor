# Athanor tracking desk

In-tree trackers are canonical so work continues when GitHub is unreachable
(air gap). GitHub issues/PRs are a public mirror.

| File | Job |
|---|---|
| [TASKS.md](TASKS.md) | Living task list. Always current. |
| [ISSUES.md](ISSUES.md) | Defects, ambiguities, blockers. Never deleted. |
| [DECISIONS.md](DECISIONS.md) | Choices made with evidence, before code depends on them. |
| [CODE_NOTES.md](CODE_NOTES.md) | Map of the source tree. |
| [BUILD_NOTES.md](BUILD_NOTES.md) | Compiler, flags, pass/fail. Evidence for SoT gates. |
| [LOG.md](LOG.md) | Chronological session log. |
| [SPEC_INDEX.md](SPEC_INDEX.md) | Specifications we implement; the anti-guess list. |
| [BUILD.md](BUILD.md) | How to compile on Windows, Linux, ARM, Android NDK. |
| [CI.md](CI.md) | Local + GitHub Actions contract. Same `make test`. |
| [TUNNEL.md](TUNNEL.md) | REQ-1.2 UDP wire format (DEC-0007). |
| [HTTP.md](HTTP.md) | REQ-2.1 TCP records + HTTP/1.1 (DEC-0009). |
| [DNS.md](DNS.md) | REQ-2.3 RFC 1035 authoritative DNS (DEC-0011). |
| [REPL.md](REPL.md) | REQ-3.1 shard/vector-clock replication (DEC-0013). |
| [KNOX.md](KNOX.md) | REQ-4.x Knox attach + builder toolchain (DEC-0015). |
| [SIGN.md](SIGN.md) | REQ-5.1 source manifest + `atnsign` (DEC-0019). |
| [ISOLATION.md](ISOLATION.md) | REQ-6.2 URL scan notes (DEC-0024). |
| [DIAG_USECASES.md](DIAG_USECASES.md) | Diag build, multi-hub/IRC model, blackout failsafe, feature inventory. |
| [LAB.md](LAB.md) | Phone + hub soak (stub APK, no Knox jar). |
| [ENROLL.md](ENROLL.md) | Lab USB enroll console (`atnenroll serve`, DEC-0042). |
| [EDGE.md](EDGE.md) | Public/cellular edge path (org fills IPs in panel). |
| [CRYPTO.md](CRYPTO.md) | **Crypto floor** — category-5 PQ + AEAD; no downgrade (DEC-0033). |
| [ESSENTIAL8.md](ESSENTIAL8.md) | ASD Essential Eight → Athanor map (honest gaps). |

**Full lab deploy (start → first phone):** [`../DEPLOY`](../DEPLOY) —
`DEPLOY.ps1` on Windows, `DEPLOY.sh` on Linux/macOS (`make all` + APK + hub + enroll UI).

Root [`../README.md`](../README.md) **Status** section holds the live pipeline +
testing-phase board; update it in the same commit as tip changes.

Rules: [`../DEVELOPMENT_RULES.md`](../DEVELOPMENT_RULES.md).
Architecture: [`../SOURCE_OF_TRUTH.md`](../SOURCE_OF_TRUTH.md).
Why/how/effect: [`../CAUSE_EFFECT_MAP.md`](../CAUSE_EFFECT_MAP.md).
