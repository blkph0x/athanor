# Signed source manifest (DEC-0019)

REQ-5.1 pen. Not the air-gap factory.

## Format

UTF-8, LF, no BOM:

```
ATN-MANIFEST-1
<64-char lowercase SHA3-256> <path>
...
```

Paths are `strcmp`-sorted, `/` separators, no spaces.

Signature: ML-DSA-87 over those exact bytes, context `atn-mf-v1`.

## Tool

```
atnsign demo
atnsign keygen keys/mldsa87.pk keys/mldsa87.sk
atnsign manifest tools/src.list MANIFEST
atnsign sign   keys/mldsa87.sk MANIFEST MANIFEST.sig
atnsign verify keys/mldsa87.pk MANIFEST MANIFEST.sig
```

`make manifest` runs the third command. `tools/src.list` is the frozen path list (DEC-0020).

`keys/` is gitignored. Lab keys on this builder are not the production
air-gap key.

## Knox jar switch

`make android-java` uses `vendor/knox/knoxsdk.jar` when that file exists.
Otherwise it compiles `android/stubs` (they set `ATN_STUB=true` and throw).
Drop the Partner jar at that path; no code rewrite. Stub builds are not
device builds.
