#!/bin/sh
# Builder env for REQ-4.x (DEC-0015). Not a product dependency.
# Source from deploy / shell:  . tools/android_env.sh
# Prints key paths; exports ANDROID_HOME, ANDROID_SDK_ROOT, ANDROID_NDK, JAVA_HOME when found.
# (No set -e: safe to source from DEPLOY.sh.)

guess_sdk() {
  if [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -d "$ANDROID_SDK_ROOT" ]; then
    echo "$ANDROID_SDK_ROOT"
    return
  fi
  if [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME" ]; then
    echo "$ANDROID_HOME"
    return
  fi
  for d in \
    "$HOME/Android/Sdk" \
    "$HOME/Library/Android/sdk" \
    /opt/android-sdk \
    /usr/lib/android-sdk
  do
    if [ -d "$d" ]; then
      echo "$d"
      return
    fi
  done
  echo ""
}

guess_ndk() {
  _sdk=$1
  if [ -n "${ANDROID_NDK:-}" ] && [ -d "$ANDROID_NDK" ]; then
    echo "$ANDROID_NDK"
    return
  fi
  if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
    echo "$ANDROID_NDK_HOME"
    return
  fi
  # Prefer pinned lab version, else newest under ndk/
  if [ -d "$_sdk/ndk/27.3.13750724" ]; then
    echo "$_sdk/ndk/27.3.13750724"
    return
  fi
  if [ -n "$_sdk" ] && [ -d "$_sdk/ndk" ]; then
    ls -1d "$_sdk/ndk"/* 2>/dev/null | sort -V | tail -n1
    return
  fi
  echo ""
}

guess_java() {
  if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/javac" ]; then
    echo "$JAVA_HOME"
    return
  fi
  if command -v /usr/libexec/java_home >/dev/null 2>&1; then
    /usr/libexec/java_home 2>/dev/null && return
  fi
  if command -v javac >/dev/null 2>&1; then
    _j=$(command -v javac)
    # .../bin/javac -> home
    echo "$_j" | sed 's|/bin/javac$||'
    return
  fi
  echo ""
}

host_tag() {
  _os=$(uname -s 2>/dev/null || echo unknown)
  _m=$(uname -m 2>/dev/null || echo unknown)
  case "$_os" in
    Linux*) echo "linux-x86_64" ;;
    Darwin*)
      if [ "$_m" = arm64 ] || [ "$_m" = aarch64 ]; then
        echo "darwin-aarch64"
      else
        echo "darwin-x86_64"
      fi
      ;;
    *) echo "linux-x86_64" ;;
  esac
}

SDK=$(guess_sdk)
NDK=$(guess_ndk "$SDK")
JH=$(guess_java)
TAG=$(host_tag)

if [ -n "$SDK" ]; then
  export ANDROID_HOME=$SDK
  export ANDROID_SDK_ROOT=$SDK
fi
if [ -n "$NDK" ]; then
  export ANDROID_NDK=$NDK
  export ANDROID_NDK_HOME=$NDK
fi
if [ -n "$JH" ]; then
  export JAVA_HOME=$JH
  PATH="$JH/bin:$PATH"
  export PATH
fi

echo "ANDROID_HOME=${ANDROID_HOME:-}"
echo "ANDROID_NDK=${ANDROID_NDK:-}"
echo "JAVA_HOME=${JAVA_HOME:-}"
echo "NDK_HOST_TAG=$TAG"

PRE=""
if [ -n "${ANDROID_NDK:-}" ]; then
  PRE="$ANDROID_NDK/toolchains/llvm/prebuilt/$TAG/bin"
  if [ ! -x "$PRE/clang" ] && [ "$TAG" = darwin-aarch64 ]; then
    TAG=darwin-x86_64
    PRE="$ANDROID_NDK/toolchains/llvm/prebuilt/$TAG/bin"
    echo "NDK_HOST_TAG=$TAG (fallback)"
  fi
fi
export ATN_ANDROID_HOST_TAG=$TAG

if [ -n "$PRE" ] && [ -x "$PRE/clang" ]; then
  echo "NDK clang OK ($PRE/clang)"
elif [ -n "$PRE" ] && [ -x "$PRE/aarch64-linux-android21-clang" ]; then
  echo "NDK clang OK ($PRE/aarch64-linux-android21-clang)"
else
  echo "NDK clang MISSING"
fi

if [ -f vendor/knox/knoxsdk.jar ]; then
  echo "knoxsdk.jar present"
else
  echo "knoxsdk.jar ABSENT — stub build only (Partner Program download)"
fi
