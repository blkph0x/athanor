#!/bin/sh
# Pack lab stub/real APK without Gradle (DEC-0030 / 0038). POSIX companion to android-apk.ps1.
# Prerequisites: make android-so + make android-java already run.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

SDK_ROOT=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-${HOME}/Android/Sdk}}
BUILD_TOOLS=${ATN_ANDROID_BUILD_TOOLS:-34.0.0}
PLATFORM=${ATN_ANDROID_PLATFORM:-android-31}

BT="$SDK_ROOT/build-tools/$BUILD_TOOLS"
JAR="$SDK_ROOT/platforms/$PLATFORM/android.jar"
AAPT2="$BT/aapt2"
D8="$BT/d8"
ZIPALIGN="$BT/zipalign"
APKSIGNER="$BT/apksigner"

for p in "$AAPT2" "$D8" "$ZIPALIGN" "$APKSIGNER" "$JAR"; do
  if [ ! -f "$p" ] && [ ! -x "$p" ]; then
    echo "missing tool: $p" >&2
    echo "Set ANDROID_SDK_ROOT / ANDROID_HOME, or install platform $PLATFORM + build-tools $BUILD_TOOLS" >&2
    exit 1
  fi
done
[ -f android/libatn.so ] || { echo "android/libatn.so missing - run: make android-so" >&2; exit 1; }
[ -f android/out/com/athanor/daemon/AtnDaemonService.class ] || {
  echo "android/out classes missing - run: make android-java" >&2
  exit 1
}

WORK="$ROOT/android/apk-work"
rm -rf "$WORK"
mkdir -p "$WORK/compiled" "$WORK/dex" "$WORK/lib/arm64-v8a"
cp android/libatn.so "$WORK/lib/arm64-v8a/libatn.so"

"$AAPT2" compile --dir android/res -o "$WORK/compiled/res.zip"
LINKED="$WORK/linked.apk"
"$AAPT2" link -o "$LINKED" -I "$JAR" --manifest android/AndroidManifest.xml \
  "$WORK/compiled/res.zip" --auto-add-overlay

# Collect classes for d8
CLASSLIST=$(find android/out -name '*.class' -print)
# shellcheck disable=SC2086
"$D8" --lib "$JAR" --min-api 26 --output "$WORK/dex" $CLASSLIST

UNSIGNED="$WORK/unsigned.apk"
cp "$LINKED" "$UNSIGNED"
# Inject classes.dex + native lib into the APK zip (paths must match APK layout).
python3 - "$UNSIGNED" "$WORK/dex/classes.dex" "$WORK/lib/arm64-v8a/libatn.so" <<'PY'
import sys, zipfile
apk, dex, so = sys.argv[1], sys.argv[2], sys.argv[3]
with zipfile.ZipFile(apk, "a", compression=zipfile.ZIP_DEFLATED) as z:
    z.write(dex, "classes.dex")
    z.write(so, "lib/arm64-v8a/libatn.so")
PY

ALIGNED="$WORK/aligned.apk"
rm -f "$ALIGNED"
"$ZIPALIGN" -f 4 "$UNSIGNED" "$ALIGNED"

KS="$ROOT/android/debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -v -keystore "$KS" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Athanor Lab,O=Athanor,C=AU"
fi

OUT="$ROOT/android/athanor-lab.apk"
rm -f "$OUT"
"$APKSIGNER" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out "$OUT" "$ALIGNED"

echo "APK_OK $OUT"
