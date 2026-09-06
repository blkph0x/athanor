# Isolation scan (REQ-6.2 scaffolding / DEC-0024 / 0026 / 0037)

Measured on this builder. Lab builds stay **online** (DEC-0037).

## Product fetch URLs

`tests/test_recipe` walks `Makefile` and every `tools/src.list` path
under `src/`, `include/`, and `android/`.

Result (BN-0021 / DEC-0024): **no `http://` or `https://` fetch URLs**.
Android XML `xmlns:` namespace URIs are not fetches.

GitHub Actions may use `actions/checkout` (DEC-0020). That is
transparency CI, not the product recipe.

## Android daemon

No Play services, no Firebase, no OkHttp, no CDN. JNI talks to
`libatn.so`. INTERNET permission is DEC-0007 UDP to a configured lab
node.

Knox: in-tree stubs until `vendor/knox/knoxsdk.jar` (gitignored).

## Export tree (DEC-0026)

`make export-tree` → `tools/export.ps1` copies every path in `tools/src.list`
into `export/athanor-src/` and refuses `*.jar`. That list includes
`SOURCE_OF_TRUTH.md`, `CAUSE_EFFECT_MAP.md`, `DEVELOPMENT_RULES.md`,
`docs/`, sources, and KAT headers. Not a signed clean-room hash match
(REQ-6.3 SoT still `[ ]`).

## Network state during measured builds

**Lab / testing (DEC-0037):** default route **up**. Continuous isolation
evidence is the recipe URL scan + export-tree. Record route-up in BN.
Do **not** air-gap the development host as standing practice.

**Release (ISS-0021 / T-0601):** one-shot `make test` + `make export-tree`
with no default route on a **dedicated release host** (or an explicit
elevated release session). That is the only path to SoT REQ-6.2 `[X]`.
Never claim it from a connected lab `make test`.

## Not yet measured (release / device)

- Build with the NIC down / default route removed (REQ-6.2 release gate)
- Runtime of server + console with default-deny netns
- Faraday / enrolled device (REQ-5.3)
- Air-gap compiler host (REQ-5.1)
