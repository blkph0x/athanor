#!/bin/sh
# Portable Athanor builder (DEC-0004). No package fetch.
# Usage:
#   ./tools/build.sh
#   CC=aarch64-linux-gnu-gcc ./tools/build.sh
#   CC=arm-linux-gnueabihf-gcc ./tools/build.sh
#   CC=aarch64-linux-android21-clang ./tools/build.sh
set -e

cd "$(dirname "$0")/.."

CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c99 -Wall -Wextra -Werror -O2 -Iinclude}
MACHINE=$("$CC" -dumpmachine)

EXE=""
LIBS=""
EXTRA=""
case "$MACHINE" in
    *mingw*|*windows*)
        EXE=".exe"
        LIBS="-lbcrypt"
        ;;
    *)
        EXTRA="-D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L"
        ;;
esac

mkdir -p tests
echo "CC=$CC MACHINE=$MACHINE"

"$CC" $CFLAGS $EXTRA -o "tests/test_crypto$EXE" \
    src/crypto/atn_platform.c \
    src/crypto/atn_secure.c \
    src/crypto/atn_sha256.c \
    src/crypto/atn_hmac.c \
    src/crypto/atn_hkdf.c \
    src/crypto/atn_chacha20.c \
    src/crypto/atn_poly1305.c \
    src/crypto/atn_aead.c \
    src/crypto/atn_nonce.c \
    tests/test_crypto.c \
    $LIBS

RUN=1
UNAME=$(uname -s 2>/dev/null || echo unknown)
case "$MACHINE" in
    *mingw*|*windows*)
        case "$UNAME" in
            Linux|Darwin|*BSD) RUN=0 ;;
        esac
        ;;
    *linux*|*darwin*|*bsd*|*android*)
        case "$UNAME" in
            Linux|Darwin|*BSD) RUN=1 ;;
            *) RUN=0 ;;
        esac
        ;;
esac

if [ "$RUN" -eq 1 ]; then
    "tests/test_crypto$EXE"
else
    echo "cross-compiled for $MACHINE — run tests/test_crypto$EXE on the target"
fi
