# Development pipeline — local and GitHub must match (DEC-0006)

Architecture law stays `SOURCE_OF_TRUTH.md`.
**Build law** is the Makefile. GitHub Actions is a public replay of that
Makefile on machines we do not own, so anyone can see the tests pass.

This is **not** the production air-gapped signer (REQ-5.1). It is development
transparency while the tree is public.

## The contract (do not drift)

GitHub Actions and this laptop both run:

```
make info
make test
make lib
```

Linux x86_64 CI also runs `make test-unsigned-char` (ARM AAPCS `char` is
unsigned). That target exists in the Makefile so it is not a hidden CI flag.

If you add a test, add it to `make test`. Do not put extra compile commands
only in YAML.

## Local (source of truth on this machine)

Already installed here (BN-0001 / this session):

| Tool | Where | Version |
|---|---|---|
| gcc | operator MinGW `bin` on PATH | 11.3.0 |
| make | same | GNU Make 4.3 |
| git | PATH | 2.41.0.windows.1 |
| gh | PATH | 2.94.0 |

One-time (done in DEC-0006 setup):

```
git config core.hooksPath .githooks
```

Before every push:

```
powershell -File tools\ci_local.ps1
```

or just `git push` — the **pre-push hook** runs `make test` and aborts the
push on failure.

## GitHub (public mirror)

Workflow: `.github/workflows/ci.yml`

| Job | Runner | What it proves |
|---|---|---|
| linux-x86_64 | ubuntu-latest | `make test` + unsigned-char + lib |
| linux-aarch64 | ubuntu-24.04-arm | real ARM64 Linux execution (ISS-0004) |
| darwin | macos-latest | Apple clang/Make (often aarch64) |
| windows-x86_64 | windows-latest + MinGW | same Makefile, `-lbcrypt` |

Runs on every push to `main`, every pull request, and manual
“Run workflow”. Logs are public: https://github.com/blkph0x/athanor/actions

**ISS-0006 closed:** billing unlocked. Run 33851841144 executed jobs.
linux-x86_64 and linux-aarch64 passed. Windows/Darwin compiler fixes
ship in the REQ-2.3 commit.

## How the two trees stay the same

1. You change files locally.
2. `make test` (hook or `ci_local.ps1`) must pass.
3. `git push` sends **that exact commit** to GitHub.
4. Actions checks out **that commit** and runs the same Makefile.
5. `git status` after a successful push should read `main...origin/main` with
   nothing ahead/behind.

There is no second copy of the tests. There is no npm/pip CI. The product
still has zero external libraries.

## What this does not replace

REQ-5.1 still requires an air-gapped signer for *release* binaries. GitHub
runners are other people’s computers. Treat a green badge as “this commit
built in public,” not as “this binary is the one we flash.”
