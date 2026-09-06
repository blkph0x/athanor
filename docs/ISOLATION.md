# Isolation scan (REQ-6.2 scaffolding / DEC-0024 / DEC-0026)

Measured on this builder. Not a disconnected-NIC runtime proof.

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

Record the default-route presence in the matching BUILD_NOTES entry.
A build with the route **up** that still performs zero product fetches
is evidence for the recipe scan; it is **not** the REQ-6.2
disconnected-build gate.

## Not yet measured

- Build with the NIC down / default route removed (REQ-6.2)
- Runtime of server + console with default-deny netns
- Faraday / enrolled device (REQ-5.3)
