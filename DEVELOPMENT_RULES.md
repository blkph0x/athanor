# Athanor development rules

These rules are binding. They sit under `SOURCE_OF_TRUTH.md` (architecture law)
and above day-to-day coding. If a rule here and the SoT conflict, the SoT wins.
If a habit and this file conflict, this file wins.

Companion map: `CAUSE_EFFECT_MAP.md`.
Living trackers: `docs/`.

---

## 1. Never guess

A guess is any value, API, flag, constant, packet field, error path, or
"should work" claim that is not backed by one of:

- a sentence in `SOURCE_OF_TRUTH.md` or `CAUSE_EFFECT_MAP.md`
- a cited specification (RFC, FIPS, Knox SDK doc, OS man page, compiler manual)
- a measurement we took and wrote down (`docs/BUILD_NOTES.md` or `docs/LOG.md`)
- a recorded decision (`docs/DECISIONS.md`) that itself cites evidence

If you do not know:

1. Stop that path.
2. Open an issue in `docs/ISSUES.md` (and a GitHub issue if the network is up).
3. Write what is unknown, what would make it known, and what must not be invented
   in the meantime.
4. Do not commit a placeholder that pretends to be finished.

Forbidden substitutes for knowledge:

- copying a constant "from memory" without a citation next to it
- assuming a compiler flag, library, or OS API exists without checking
- marking a SoT checkbox because the file compiled once on one machine
- `TODO` that hides an unmade decision
- "temporary" vendor libraries

Inventing a new cipher, hash, or handshake "because ours is more sovereign" is
a guess. We implement **published** primitives line-by-line from cited specs.
Sovereignty is *who compiles them*, not *who dreamed the round function*.

---

## 2. Everything is documented

Every change that lands on `main` updates the trackers in the same commit
whenever the change affects them.

| If you… | You also… |
|---|---|
| Start work | Put it on `docs/TASKS.md` as in-progress |
| Make a choice | Record it in `docs/DECISIONS.md` *before* the code that depends on it |
| Write or change a module | Update `docs/CODE_NOTES.md` (and the module note it points at) |
| Compile, fail, or pass a gate | Append `docs/BUILD_NOTES.md` |
| Hit a defect, ambiguity, or blocker | Open/update `docs/ISSUES.md` |
| Finish a session | Append `docs/LOG.md` with what changed, what was proven, what is still open |

Documentation is part of the patch, not a later courtesy.

---

## 3. Everything is commented

Comments explain **why**, **which spec**, and **what must remain true**.
They do not narrate `i++`.

Required on every public function:

```
Purpose:
Spec:     <document, section>
Params:   <invariants, ownership, lengths>
Returns:  <codes, what is written>
Invariants / side channels:
```

Required on magic numbers: a citation, not a vibe.

```c
/* RFC 8439 §2.3: ChaCha20 state words 0-3 are this ASCII constant. */
st[0] = 0x61707865u;
```

Required on security-sensitive code: whether it must be constant-time, whether
buffers are zeroed, whether a nonce may repeat.

If a block of code is not obvious from the spec, the comment points at the spec
step it implements. If it is not in any spec, it does not belong until a
decision record exists.

---

## 4. The task list is always current

`docs/TASKS.md` is the canonical work queue (air-gapped). GitHub issues are a
mirror, not a second brain.

Rules:

- One current "in progress" implementation task per person unless a blocker
  forces a documented switch.
- Do not start a SoT REQ whose `Depends on` items in the cause/effect map are
  still open.
- Sub-tasks under a REQ exist in `TASKS.md`. The SoT checkbox stays `[ ]` until
  the **verification gate** in `CAUSE_EFFECT_MAP.md` is green.
- Closing a task requires: code + comments + tests + tracker updates.

---

## 5. Issues are first-class

An issue is opened for:

- a failed test or build
- a spec ambiguity
- a missing measurement
- a suspected side channel
- anything we were tempted to guess

Issues are never deleted. They are closed with a pointer to the commit, test,
or decision that resolved them.

---

## 6. Build notes are evidence

`docs/BUILD_NOTES.md` records, for each build that matters:

- host OS, compiler, exact version (`gcc --version`, not "gcc")
- flags
- command line
- network state (connected / disconnected)
- pass/fail and the first error if fail
- artifact hashes when we start signing (REQ-5.1)

A verification gate is not met because a developer said so. It is met because
a build note plus a test run say so.

---

## 7. Code notes are the map of the tree

`docs/CODE_NOTES.md` indexes every module: path, REQ, spec, public entry
points, and dangers. If you cannot find a file's purpose in that index, the
index is wrong.

---

## 8. Tooling and dependencies

SoT: **zero external libraries**. Allowed:

- language + OS libc / Win32 / Knox SDK as specified
- the compiler already on the air-gapped builder
- specifications we cite (RFCs, FIPS) as *references*, not as code we link

Not allowed: crates.io, npm, pip, vcpkg ports, git submodules of foreign
crypto, "just OpenSSL until the tests pass."

---

## 9. Git

- `main` is the signed history of the foundry.
- Commit messages say *what* and *why*, and name the REQ (`REQ-1.1`).
- Do not commit keys, nonces used in production, or machine-local secrets.
  `.gitignore` is part of the spec of what must not leak.
- Tracker files land in the same commit as the code they describe.

---

## 10. Definition of done (any REQ)

Copied in spirit from the cause/effect map. A REQ is done only when all are true:

1. Source compiles with the frozen toolchain, no outside libs.
2. Comments and code notes cite the spec actually implemented.
3. Known-answer tests from the cited spec pass.
4. Negative tests for the failure mode the REQ exists to stop (tamper, replay,
   guessable nonce, etc.) exist and pass.
5. `docs/BUILD_NOTES.md` has a passing run.
6. `docs/TASKS.md` and the SoT checkbox are updated in that order: evidence
   first, checkbox second.

---

## Quick checklist before you type code

- [ ] I can name the REQ.
- [ ] Dependencies of that REQ are done, or I am on a leaf of Phase 1.
- [ ] I am not about to invent a constant.
- [ ] The decision (language, algorithm, API) is already in `docs/DECISIONS.md`.
- [ ] I know which tracker files this commit will touch.
