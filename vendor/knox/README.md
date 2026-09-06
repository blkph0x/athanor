# Knox SDK drop-in (DEC-0015 / 0019 / 0030)

Samsung does not allow the real jar in our git tree.

## Testing version (no jar) — already wired

| Item | Path / command |
|---|---|
| Stubs (same packages as Samsung) | `android/stubs/com/samsung/android/knox/` |
| Product Java (keeps real imports) | `android/java/com/athanor/daemon/` |
| Build | `make android-java` → prints `STUB BUILD: …` |
| Detector | `AtnKnoxBuild.isStub() == true` (`ATN_STUB` field) |
| Diag conf (PC soak) | `diag=1` / `flush_mode=log_only` (DEC-0027) |

Stubs export `ATN_STUB=true` and throw `UnsupportedOperationException`
if any policy API is invoked. That is a **lab compile**, not a phone
enroll. SoT REQ-4.1 stays `[ ]`.

Do **not** use `app/libs/knox_sdk.jar`, Gradle AndroidX recipes, or a
Node “brick lock” server — those are foreign to this SoT (DEC-0030).

## When the Partner jar arrives

1. Sign in at https://partner.samsungknox.com/
2. SDK Tools → SDK Downloads → Knox SDK
3. Place the real file at **this exact path only**:

   `vendor/knox/knoxsdk.jar`

4. Re-run `make android-java`. Expect:

   `REAL knoxsdk.jar: …/vendor/knox/knoxsdk.jar`

5. No Java rewrite. Stub sources are omitted from the compile line.
   Real classes lack `ATN_STUB`, so `AtnKnoxBuild.isStub()` is false.
6. Enroll Device/Profile Owner on S24–S26, then run REQ-4.x gates
   (still required before SoT `[X]`).

License key (if any) → gitignored local file, never in source.
