# Isolation scan (REQ-6.2 scaffolding / DEC-0024)

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

## Not yet measured

- Build with the NIC down (REQ-6.2 disconnected build)
- Runtime of server + console with default-deny netns
- Faraday / enrolled device (REQ-5.3)
