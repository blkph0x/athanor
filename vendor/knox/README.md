# Knox SDK drop-in

Samsung does not allow this jar in our git tree.

1. Sign in at https://partner.samsungknox.com/
2. SDK Tools → SDK Downloads → Knox SDK
3. Place `knoxsdk.jar` in this directory (this path):

   `vendor/knox/knoxsdk.jar`

`make android` uses the jar when present; otherwise it compiles
against `android/stubs` and prints STUB BUILD.
