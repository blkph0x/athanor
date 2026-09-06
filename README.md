# Athanor

[![ci](https://github.com/blkph0x/athanor/actions/workflows/ci.yml/badge.svg)](https://github.com/blkph0x/athanor/actions/workflows/ci.yml)

**Sovereign infrastructure, smelted from first principles.**

Athanor is a closed-loop communications, storage, and endpoint stack that you compile yourself, run yourself, and own yourself. No cloud tenant. No vendor VPN. No package mirror. No CDN. No “trust this binary we downloaded.”

Internal architecture name: **SovereignFoundry (SF-ARCH) v2.0.0**  
Rule of the foundry: **zero external libraries, zero third-party runtimes, zero hosted dependencies.**

**Build (Windows, Linux, ARM, Android NDK):** see [`docs/BUILD.md`](docs/BUILD.md). Short version: `make test` on the target, or `make CC=aarch64-linux-gnu-gcc` to cross-compile.

**Crypto floor (no downgrade):** ML-KEM-1024 + ML-DSA-87 + ChaCha20-Poly1305 (256-bit) — [`docs/CRYPTO.md`](docs/CRYPTO.md), DEC-0033.  
**Essential Eight map:** [`docs/ESSENTIAL8.md`](docs/ESSENTIAL8.md) (org IT baseline → Athanor analogues; we aim **above** that floor on the wire).

**Pipeline:** local `make test` and GitHub Actions run the **same Makefile**. See [`docs/CI.md`](docs/CI.md). Pre-push hook refuses a push that fails tests. After `git push`, this laptop and `origin/main` are the same commit.

> An *athanor* is the alchemist’s furnace built to hold a constant fire without feeding it from the outside. That is the point of this project. The heat has to come from ore we smelted.

---

## Why this name

Most “secure” products today are still someone else’s furnace. You rent the fire: their VPN, their identity provider, their object store, their compiler pipeline, their phone-home SDK. When the vendor changes terms, ships a supply-chain bomb, or is compelled to open the flue, your heat is gone.

Athanor is named for a furnace that **does not need outside fuel**. Every packet cipher, every socket, every HTML page, every replica, every heartbeat token, every signed update is meant to be code we wrote and a binary we compiled.

---

## The problem it answers

In the last decade, ordinary life moved onto a handful of networks we do not own:

- Mail, files, chat, and “the office” live in three or four clouds.
- “Encryption” often means a library pulled from a mirror at build time, or a TLS box we never read.
- “Private VPN” often means WireGuard/OpenVPN plus an identity layer plus an app store plus crash analytics.
- Phones — the device that holds a person’s whole civic life — still leak over USB, over vendor backup, over a four-digit PIN, and over whatever SDK was pasted in to ship faster.
- A single poisoned package on npm, PyPI, crates.io, or a Linux mirror can enter a “hardened” shop because the build machine was allowed to fetch.

That is not only a security failure. It is a **civil-rights** failure. If your papers live in a landlord’s filing cabinet, your right to private correspondence, association, and political thought depends on that landlord’s policy, their breach history, and whoever can serve them.

Athanor exists so that a person, a newsroom, a clinic, a union, a campaign, or a family office can run **their own** lock, **their own** corridor, and **their own** archive — and prove that the lock was not borrowed.

---

## What Athanor is

Athanor is not a wrapper around the existing internet stack. It is a specification and, as the work lands, a set of **in-house binaries** that replace the usual rented pieces:

| What people usually rent | What Athanor builds instead |
|---|---|
| WireGuard / OpenVPN | A UDP tunnel with our packet headers, our state machine, our sockets |
| OpenSSL / libsodium / language crypto providers | Low-level cryptographic primitives compiled into our binaries |
| Nginx / Apache / Node | A minimal HTTP/TLS listener that serves handwritten pages from memory |
| SaaS 2FA / authenticator ecosystems | Challenge-response 2FA whose MAC/hash math is our code |
| S3 / hosted Postgres / “the cloud” | Encrypted blocks in our memory trees, replicated over our pipe |
| Public DNS + CDNs | An authoritative DNS responder that does not forward to anyone |
| Play services / vendor MDM-as-a-service | A Knox-bonded Android daemon on S24 / S25 / S26 for people who choose that hardware |
| GitHub Actions / apt / npm CI | An air-gapped compiler and signing chain |

Target endpoints in the current spec: **Samsung Galaxy S24, S25, S26** using Knox hardware-backed policy and attestation — not as a fashion choice, but because those devices expose an enterprise lock, a hardware keystore, and USB restriction that a normal app sandbox does not.

---

## What it will do

When the six phases in the source of truth are complete, Athanor will do six concrete jobs:

### 1. Keep secrets in math we can read
A cryptographic core (hash, MAC, AEAD, key derivation, nonce discipline, constant-time compare, zeroization) compiled from our source. Every later feature — tunnel, 2FA, heartbeat, signed updates — calls this core. If we cannot explain a byte of ciphertext, we do not ship.

### 2. Move bytes through a pipe we specified
A UDP tunnel from raw OS sockets. Handshake, replay window, rekey, keepalive. No WireGuard. No OpenVPN. Packet captures of the payload should be useless without our keys.

### 3. Give operators a door that is still ours
A tiny web listener and a handwritten admin console: enroll a node, challenge 2FA, watch the mesh, revoke a device, order a wipe. No React, no npm, no webfonts, no “just this one CDN.” Names of nodes come from our DNS, not from 8.8.8.8.

### 4. Store and copy data without a landlord
A binary memory tree for local state. A replication/sharding protocol so one seized or burned machine is not the archive. A cryptographic heartbeat mesh so “this node is still ours” is a MAC, not a vibe. If a node goes silent past policy, **that node’s own memory and key slots** are flushed. We do not reach into anyone else’s machine.

### 5. Bond the human endpoint to hardware policy
On enrolled Samsung Knox devices:

- a background daemon that is a mesh member, not a bookmark
- biometric plus a **12+ character alphanumeric** password — stolen glass is not enough
- USB forced to **charge-only** so a cable is not a dump path
- memory flush tied to attestation failure, heartbeat miss, lockout, or RF isolation (the Faraday-bag case)

Knox/TIMA here means: we **attach** wipe and policy to Samsung’s supported attestation and restriction APIs. We do not pretend we can patch Samsung’s firmware on a stock phone.

### 6. Change the system without reopening the supply chain
An air-gapped build and sign chain. Tests that emulate an update before anything is flashed. Fuzzing and leak checks on our own parsers. An isolation audit that treats a single package-mirror fetch as a failed exam. An export of the whole tree so a client can compile the same bytes without us.

---

## End goal

The end goal is not “a cooler VPN.”

The end goal is a **closed loop**:

1. A person or organization can stand up nodes they physically control.
2. Those nodes speak only protocols whose source is in this tree.
3. Endpoints that hold keys are locked by policy the owner set, not by an app-store default.
4. Updates are signed on a machine that does not fetch from the internet.
5. A stranger with this export and a compiler can rebuild the binaries and match the hashes.
6. When we say **0% external libraries**, that is a measured fact (disconnected build, disconnected run, strings/link audit), not a banner.

When that loop closes, Athanor is finished as specified. Everything after that is operations, not a new landlord.

---

## Why it is useful — and needed — now

### Security, as it actually fails

Modern incidents rarely begin with a novel cipher break. They begin with **someone else’s code in your trust boundary**:

- **Supply chain.** A maintainer account, a typo-squatted package, a compromised CI runner, a helpful “security scanner” that phones home. If your build is allowed to download, your policy document is fan fiction.
- **Concentration.** One cloud, one identity provider, one MDM, one push network. The blast radius of a breach or an outage is civilizational, not departmental.
- **Telemetry as architecture.** Free SDKs are not free. Crash reporters, fonts, maps, and “analytics” are extra observers sitting in the room where you thought you were alone.
- **Endpoints are the archive.** The phone is the filing cabinet. USB, weak PINs, cloud backup, and always-on radios make “encrypted in transit” a story that ends at a table in an airport.
- **Parsers are the breach.** The first hostile HTTP request against a weekend web framework is still how consoles die. If we write the listener, we owe it fuzzing and leak tests before we owe it a screenshot.

Athanor’s usefulness is brutally practical: **shrink the number of people who can betray you, including people you never hired.**

### Civil rights, as they are actually exercised

Rights that only exist inside a terms-of-service checkbox are not rights. They are permissions.

- **Private correspondence.** The ability to speak to a doctor, a lawyer, a journalist, a spouse, or a fellow citizen without a platform’s side channel is older than this industry. It is the modern form of a sealed letter. You cannot seal a letter you stored in someone else’s sorting office.
- **Freedom of association.** Groups that are unpopular — sometimes wrongly, sometimes because they are holding power to account — get their tools shut off. If your mesh dies when a vendor decides you are inconvenient, you did not have a network. You had a lease.
- **Freedom of the press and of defense.** Newsrooms, investigators, and counsel need notes that are not sitting in a bucket whose keys the host can be compelled to turn. “Just use a big company’s encrypted chat” still leaves metadata, billing, device backups, and the app’s own SDK graph.
- **Due process vs. bulk observation.** Lawful process against a person is one thing. Architecture that copies everyone’s life to a convenient third party — because that was cheaper than running a node — is how a society sleepwalks into general search. Owning the lock does not place anyone above the law. It puts the papers back in the house that can be served, instead of in a warehouse that can be trawled.
- **The right to compute.** If you cannot compile the tool that guards you, you are a tenant of whoever compiled it. Independent compilation (our last milestone) is the civil-rights clause of this repo: *do not take our word. Take the tree.*
- **Bodily and domestic privacy in a pocket-sized form.** A phone that yields to a USB cable, a guessable PIN, or a bag that merely pauses the radio is not a personal effect. It is an unlocked diary. Charge-only USB, real password policy, attested keys, and a wipe when the mesh can no longer be heard are how you treat that diary as yours.

This is why Athanor is not a hobby aesthetic. **Security without ownership is branding. Ownership without a compile path is faith.** The project is the attempt to have both.

---

## What Athanor is not

- **Not a product you install and forget.** If you cannot operate a node, this is not for you yet.
- **Not a crime kit.** Memory flush, charge-only USB, and Faraday-triggered cache wipe are defenses of **our** process memory and **our** devices. They are not a warrant to touch anyone else’s.
- **Not a claim that homemade crypto is automatically stronger than a well-reviewed library.** The SoT forbids outside libraries *because of control*, then demands tests, known-answer vectors, fuzzing, and isolation audits *because of humility*. Shipping without those gates is a SoT violation, not a shortcut.
- **Not a Samsung firmware fork.** Knox is used as a hardware bond on devices we enroll, through supported policy and attestation.
- **Not “the cloud, but we say sovereign.”** If a public resolver, a CDN, a crash reporter, or a package mirror is still in the path, the exam is failed.

---

## Architecture at a glance

```
crypto primitives
        ├─ UDP tunnel ── HTTP/TLS listener ── handwritten admin console
        │              └─ authoritative DNS
        ├─ 2FA challenge-response
        ├─ memory trees ── replication / sharding ── heartbeat mesh
        └─ air-gapped signer ── emulate-before-flash ── isolation audit ── export

heartbeat miss / attestation fail / lockout / RF isolation
        └─ flush our RAM and key slots (server node and/or enrolled phone)

enrolled Galaxy S24/S25/S26
        └─ Knox daemon, 12+ char password + biometric, USB charge-only
```

Law of sequencing: do not start a requirement whose dependencies are still open. Do not mark a box because files were typed. Mark it when the verification gate compiled, ran, and proved the effect.

---

## Documents in this repository

| File | Role |
|---|---|
| [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md) | Canonical specification. Non-negotiable. |
| [`CAUSE_EFFECT_MAP.md`](CAUSE_EFFECT_MAP.md) | Why each requirement exists, how to finish it, what it unlocks, what breaks if skipped, and the verification gate. |
| [`DEVELOPMENT_RULES.md`](DEVELOPMENT_RULES.md) | Never guess. Comment and document everything. Keep the task list current. |
| [`docs/`](docs/README.md) | Living TASKS, ISSUES, DECISIONS, CODE_NOTES, BUILD_NOTES, LOG, SPEC_INDEX, **CRYPTO**, **ESSENTIAL8**. |

The README is orientation. The source of truth is law. The cause/effect map is how work is sequenced and proven. The development rules are how we type. The `docs/` desk is how we remember.

**Never guess.** If a constant, API, or algorithm is not in `docs/SPEC_INDEX.md` or `docs/DECISIONS.md`, it does not go in the tree. Open `docs/ISSUES.md` instead.

---

## Rules of the foundry

1. Every asset is compiled from source in this tree using language and OS primitives only.
2. No third-party modules, frameworks, package mirrors, or CDNs.
3. Checkboxes in the SoT and the cause/effect map flip from `[ ]` to `[X]` only after the gate for that requirement is met.
4. Wipe, lockout, and USB policy apply to **our** nodes and **our** enrolled devices.
5. A disconnected build and a disconnected run are features, not inconveniences.

---

## Status

**Specification frozen at SF-ARCH v2.0.0.**  
**Public tip:** `origin/main` — local `make test` and GitHub Actions run the **same Makefile** ([`docs/CI.md`](docs/CI.md)). Pre-push refuses a red push.

### Pipeline (how we prove)

| Stage | Command / artifact | Status |
|---|---|---|
| Local gate | `make test` (Windows MinGW gcc 11.3.0 on builder) | Green on tip |
| CI | `.github/workflows/ci.yml` → same `make test` | Badge above |
| Stub Knox compile | `make android-java` / `make android-apk` | Green stub lab APK |
| Real Knox | `vendor/knox/knoxsdk.jar` drop-in | **Blocked** Partner / T-0400 |
| Export | `make export-tree` | Scaffolding OK; NIC-down = **release** (ISS-0021 / DEC-0037) |
| Diag soak | `diag=1` + `flush_mode=log_only` (DEC-0027) | Ready for lab |
| Phone↔hub lab | S24 stub APK + `atnnode listen` ([`docs/LAB.md`](docs/LAB.md)) | **Proven** ESTABLISHED + BOOM soaks |
| Lab USB enroll UI | `atnenroll serve` loopback ([`docs/ENROLL.md`](docs/ENROLL.md)) | **Ready** DEC-0042 (air-gap later) |
| Hub failover | `tests/test_hub_failover` (DEC-0031 / D-08) | Green |
| Multi-hub roster | cap **16** hubs/repl (DEC-0032) | Green |
| PQ tunnel rekey | `test_tun` REKEY_INIT/ACK (DEC-0035) | Green |
| Crypto floor gate | `test_recipe` forbids weak/third-party tokens (DEC-0033) | Green |
| Essential Eight map | `docs/ESSENTIAL8.md` | Documented; not an assessed ML3 claim |

### Crypto & security posture (DEC-0033)

| Control | Bar |
|---|---|
| Tunnel KEM | **ML-KEM-1024 only** (no 512/768, no classical-only KEX) |
| Signatures | **ML-DSA-87 only** |
| Wire/store AEAD | **ChaCha20-Poly1305**, 256-bit keys |
| Admin mutate | Session + CSRF + **HMAC-SHA-512 2FA** |
| Supply chain | Zero product package mirrors; recipe URL + crypto-floor scan |
| Essential Eight | Mapped in `docs/ESSENTIAL8.md` — we meet or exceed on wire crypto; device/OS gaps tracked |

### Testing-phase readiness

| Track | Ready? | Notes |
|---|---|---|
| PC crypto / tunnel / 2FA / HTTP / DNS / tree / repl / hb | **Yes** | Phases 1–3 SoT `[X]` |
| Multi-hub conf + wire failover (no phone) | **Yes** | DEC-0028/0031/0032 |
| Diag / no-brick wipe path | **Yes** | DEC-0027; use before any device flash |
| Outage class (blackout ≠ Faraday) | **Yes** | DEC-0029; console 2FA set (DEC-0034) |
| Lab hub binary | **Yes** | `atnnode listen\|connect\|demo`; connect walks hubs (DEC-0031) |
| Enrolled Knox S24–S26 | **No** | Waiting `knoxsdk.jar` + Device Owner (**T-0400**) |
| Stub lab APK ↔ hub (USB adb) | **Yes** | DEC-0038–0041; join→silence/airplane BOOM; lock-screen K=5 |
| Lab enroll console (USB) | **Yes** | DEC-0042 `atnenroll serve`; local sign OK; air-gap later |
| Air-gap sign host / Faraday bag | **No** | REQ-5.x / 5.3 open (release) |

### Requirement board

| REQ | Status |
|---|---|
| REQ-1.1 crypto primitives | Done — RFC 6234 / 4231 / 5869 / 8439 KATs. Residual: ISS-0003. |
| REQ-1.1-PQ | Done — FIPS 203 **ML-KEM-1024** + FIPS 202 SHAKE/SHA3 + SHA-512. |
| REQ-1.2 UDP tunnel | Done — ML-KEM-1024 handshake + AEAD + PQ rekey (DEC-0035). IPv4 required (DEC-0022). |
| REQ-1.3 2FA | Done — HMAC-SHA-512. `tests/test_2fa` + `atn2fa demo`. |
| REQ-2.1 HTTP listener | Done — HTTP/1.1 loopback + DEC-0007 records. Operator: `atnhttp`. |
| REQ-2.2 admin console | Done — embedded HTML/CSS, POST+CSRF, 2FA on mutate; WHATWG form decode (DEC-0036). |
| REQ-2.3 DNS | Done — RFC 1035 authoritative `atn.test`, no recursion. |
| REQ-3.2 memory tree | Done — AVL + AEAD snapshot. |
| REQ-3.1 replication | Done — factor 2, vector clocks; roster cap **16** (DEC-0032). |
| REQ-3.3 heartbeat | Done — WARN/grace/HOLD; hub failover D-08 (DEC-0031). |
| REQ-4.x Knox device | Stub lab soak **proven** (mesh + BOOM). **SoT `[ ]`** until T-0400 jar + enroll. |
| ML-DSA-87 / REQ-5.1 pen | Done KATs + `atnsign`. Air-gap host open. |
| REQ-6.x isolation/export | Partial — lab: URL scan + export-tree online (DEC-0037); NIC-down = release only. |

Desk trackers (always current with the tip): [`docs/TASKS.md`](docs/TASKS.md), [`docs/LOG.md`](docs/LOG.md), [`docs/BUILD_NOTES.md`](docs/BUILD_NOTES.md), [`docs/DIAG_USECASES.md`](docs/DIAG_USECASES.md).

Until a requirement’s gate is green, treat that capability as *intended*, not *done*.

---

## License

See [`LICENSE`](LICENSE). No warranty. You compile it. You run it. You own the consequences of how you use it. Use it to keep your own house in order.
