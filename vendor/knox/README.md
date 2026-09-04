# Knox SDK drop-in

Samsung does not allow the real jar in our git tree.

## While Partner / PTR is pending

In-tree stubs live at `android/stubs/`. They export `ATN_STUB=true` and
throw `UnsupportedOperationException` if any policy API is called.
`make android-java` uses them automatically. That is a **lab compile**,
not a phone build. SoT REQ-4.1 stays `[ ]`.

## When the Partner jar arrives

1. Sign in at https://partner.samsungknox.com/
2. SDK Tools → SDK Downloads → Knox SDK
3. Place the real file here (this exact path):

   `vendor/knox/knoxsdk.jar`

4. Re-run `make android-java`. The Makefile switches classpath; no
   Java rewrite. The stub `ATN_STUB` field is absent on the real
   classes, so `AtnKnoxBuild.isStub()` becomes false.

License key (if any) goes in a gitignored local file, never in source.
