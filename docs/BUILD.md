# Building Athanor on every target (DEC-0004)

The crypto core is **C99**. Loads and stores are byte-wise (endian-safe).
`char` is never used as a numeric type (ARM often defaults to unsigned char).
The OS CSPRNG is chosen by **compiler macros**, not by `uname`.

## One command

| You are on | Target | Command |
|---|---|---|
| Windows x64 | Windows x64 | `make test` or `tools\build.bat` |
| Windows ARM64 | Windows ARM64 | `tools\build.bat` with that box's GCC/Clang |
| Linux x86_64 | Linux x86_64 | `make test` or `sh tools/build.sh` |
| Linux aarch64 (Pi 4/5, Ampere, S24 via termux/NDK) | same | `make test` |
| Linux armv7 | same | `make test` |
| Any builder with a cross compiler | Linux aarch64 | `make CC=aarch64-linux-gnu-gcc` |
| Any builder | Linux armhf | `make CC=arm-linux-gnueabihf-gcc` |
| Android NDK | Android aarch64 | `make CC=aarch64-linux-android21-clang` |
| macOS Apple Silicon | Darwin aarch64 | `make test` |
| macOS Intel | Darwin x86_64 | `make test` |

Cross-compiles **do not execute** the test binary on the builder. Copy it to
the target and run it there.

```
make info          # print CC, dumpmachine, OS, arch, CROSS
make               # tests/test_crypto[.exe]
make test          # build + run (native only)
make lib           # libatn_crypto.a  (link this into later binaries)
make clean
```

## What gets linked (and nothing else)

| Compiler `-dumpmachine` contains | Extra link |
|---|---|
| `mingw` or `windows` | `-lbcrypt` (Windows CNG, including Windows on ARM) |
| `linux`, `android`, `darwin`, `bsd` | none |

No OpenSSL, no libsodium, no libcrypt.

## Entropy source per OS (in `atn_secure.c`)

| OS | Call | Citation |
|---|---|---|
| Windows / Windows ARM | `BCryptGenRandom` | bcrypt.h, `BCRYPT_USE_SYSTEM_PREFERRED_RNG` |
| macOS / iOS / BSD | `arc4random_buf` | arc4random(3) |
| Linux (x86 and ARM) | `getrandom`, then `/dev/urandom` | getrandom(2) |
| Android | `getrandom` if API ≥ 28, else `/dev/urandom` | bionic |
| other unix | `/dev/urandom` | — |
| anything else | compile error | never guess |

## Flags that must stay

`-std=c99 -Wall -Wextra -Werror -O2 -Iinclude`

On POSIX add `-D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L` so `getrandom` /
`open` are declared under strict C99. The Makefile and `tools/build.sh` add
these. `atn_secure.c` also defines them itself so a raw compiler line still
works.

## Raw compiler line (no make)

Windows:

```
gcc -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -o tests/test_crypto.exe src/crypto/*.c tests/test_crypto.c -lbcrypt
```

Linux / ARM / macOS:

```
cc -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -o tests/test_crypto src/crypto/*.c tests/test_crypto.c
```

## This builder's measured results (do not paper over)

Host: Windows, `gcc -dumpmachine` = `x86_64-w64-mingw32` (BN-0001 / BN-0003).

| What | Result |
|---|---|
| `make test` (Windows x86_64) | ALL PASSED, `platform=windows-x86_64` `sizeof(void*)=8` |
| `gcc -funsigned-char` (ARM AAPCS `char` is unsigned) | ALL PASSED |
| `gcc -m32` (32-bit pointers, ARM32-like width) | ALL PASSED, `platform=windows-x86` `sizeof(void*)=4` |
| `make lib` | `libatn_crypto.a` with all nine objects |
| ARM GCC / WSL distro on this host | **none** — ISS-0004. Recipe is `make CC=aarch64-linux-gnu-gcc`; run the binary on ARM. |
