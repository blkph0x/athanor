# Athanor portable build (DEC-0004).
#
# Same tree on Windows, Linux, macOS, Android NDK, and ARM.
# The *compiler's* -dumpmachine decides libraries, not `uname` (missing on
# Windows) and not the OS that happens to run make.
#
# Native:              make
#                      make test
# Linux aarch64:       make CC=aarch64-linux-gnu-gcc
# Linux armhf:         make CC=arm-linux-gnueabihf-gcc
# Android 64-bit:      make CC=aarch64-linux-android21-clang
# Windows ARM64 mingw: make CC=aarch64-w64-mingw32-gcc
# Cross without run:   tests are skipped when the target is not this OS.
#
# Override: make CC=clang CFLAGS='-std=c99 -O2 -Iinclude'

# GNU Make defaults CC to `cc`, which does not exist on Windows MinGW.
# Honour an explicit CC=... from the command line or environment; otherwise gcc.
ifeq ($(origin CC),default)
  CC := gcc
endif
ifeq ($(origin AR),default)
  AR := ar
endif
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -Itests
LDFLAGS ?=

MACHINE := $(shell $(CC) -dumpmachine)

ATN_TARGET_OS := unknown
ifneq (,$(findstring mingw,$(MACHINE)))
  ATN_TARGET_OS := windows
endif
ifneq (,$(findstring windows,$(MACHINE)))
  ATN_TARGET_OS := windows
endif
ifneq (,$(findstring android,$(MACHINE)))
  ATN_TARGET_OS := android
endif
ifeq ($(ATN_TARGET_OS),unknown)
  ifneq (,$(findstring linux,$(MACHINE)))
    ATN_TARGET_OS := linux
  endif
endif
ifneq (,$(findstring darwin,$(MACHINE)))
  ATN_TARGET_OS := darwin
endif
ifneq (,$(findstring freebsd,$(MACHINE)))
  ATN_TARGET_OS := bsd
endif
ifneq (,$(findstring openbsd,$(MACHINE)))
  ATN_TARGET_OS := bsd
endif
ifneq (,$(findstring netbsd,$(MACHINE)))
  ATN_TARGET_OS := bsd
endif

ATN_ARCH := unknown
ifneq (,$(findstring aarch64,$(MACHINE)))
  ATN_ARCH := aarch64
endif
ifeq ($(ATN_ARCH),unknown)
  ifneq (,$(findstring arm64,$(MACHINE)))
    ATN_ARCH := aarch64
  endif
endif
ifeq ($(ATN_ARCH),unknown)
  ifneq (,$(findstring arm,$(MACHINE)))
    ATN_ARCH := arm
  endif
endif
ifneq (,$(findstring x86_64,$(MACHINE)))
  ATN_ARCH := x86_64
endif
ifneq (,$(findstring i686,$(MACHINE)))
  ATN_ARCH := x86
endif
ifneq (,$(findstring i386,$(MACHINE)))
  ATN_ARCH := x86
endif

EXE :=
ifeq ($(ATN_TARGET_OS),windows)
  EXE := .exe
  LDFLAGS += -lbcrypt -lws2_32
endif

# POSIX feature macros: needed with -std=c99 so getrandom/open are declared.
# Darwin: _POSIX_C_SOURCE hides arc4random_buf; _DARWIN_C_SOURCE restores it.
ifneq ($(ATN_TARGET_OS),windows)
  CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
endif
ifeq ($(ATN_TARGET_OS),darwin)
  CFLAGS += -D_DARWIN_C_SOURCE
endif

# Skip running a foreign binary. Override with CROSS=0/1.
CROSS ?= auto
ifeq ($(CROSS),auto)
  CROSS := 0
  ifeq ($(OS),Windows_NT)
    ifneq ($(ATN_TARGET_OS),windows)
      CROSS := 1
    endif
  else
    ifeq ($(ATN_TARGET_OS),windows)
      CROSS := 1
    endif
  endif
endif

SRC = \
	src/crypto/atn_platform.c \
	src/crypto/atn_secure.c \
	src/crypto/atn_sha256.c \
	src/crypto/atn_sha512.c \
	src/crypto/atn_hmac.c \
	src/crypto/atn_hkdf.c \
	src/crypto/atn_fips202.c \
	src/crypto/atn_mlkem.c \
	src/crypto/atn_chacha20.c \
	src/crypto/atn_poly1305.c \
	src/crypto/atn_aead.c \
	src/crypto/atn_nonce.c

TUN_SRC = src/tun/atn_tun.c
AUTH_SRC = src/auth/atn_2fa.c
HTTP_SRC = src/http/atn_http.c
DNS_SRC  = src/dns/atn_dns.c
TREE_SRC = src/store/atn_tree.c
REPL_SRC = src/repl/atn_repl.c
HB_SRC   = src/hb/atn_hb.c

TEST_BIN = tests/test_crypto$(EXE)
TEST_TUN = tests/test_tun$(EXE)
TEST_2FA = tests/test_2fa$(EXE)
TEST_HTTP = tests/test_http$(EXE)
TEST_DNS = tests/test_dns$(EXE)
TEST_TREE = tests/test_tree$(EXE)
TEST_REPL = tests/test_repl$(EXE)
TEST_HB   = tests/test_hb$(EXE)
CLI_2FA  = atn2fa$(EXE)
CLI_HTTP = atnhttp$(EXE)
CLI_DNS  = atndns$(EXE)
LIB_BIN  = libatn_crypto.a

.PHONY: all test lib info clean ci test-unsigned-char android-so android-java

all: $(TEST_BIN) $(TEST_TUN) $(TEST_2FA) $(TEST_HTTP) $(TEST_DNS) $(TEST_TREE) $(TEST_REPL) $(TEST_HB) $(CLI_2FA) $(CLI_HTTP) $(CLI_DNS)

info:
	$(info CC=$(CC))
	$(info MACHINE=$(MACHINE))
	$(info ATN_TARGET_OS=$(ATN_TARGET_OS))
	$(info ATN_ARCH=$(ATN_ARCH))
	$(info CROSS=$(CROSS))
	$(info CFLAGS=$(CFLAGS))
	$(info LDFLAGS=$(LDFLAGS))
	@$(CC) -dumpmachine

$(TEST_BIN): $(SRC) tests/test_crypto.c include/atn_crypto.h include/atn_platform.h
	$(CC) $(CFLAGS) -o $@ $(SRC) tests/test_crypto.c $(LDFLAGS)

$(TEST_TUN): $(SRC) $(TUN_SRC) tests/test_tun.c include/atn_tun.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) tests/test_tun.c $(LDFLAGS)

$(TEST_2FA): $(SRC) $(AUTH_SRC) tests/test_2fa.c include/atn_2fa.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(AUTH_SRC) tests/test_2fa.c $(LDFLAGS)

$(CLI_2FA): $(SRC) $(AUTH_SRC) src/auth/atn_2fa_cli.c include/atn_2fa.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(AUTH_SRC) src/auth/atn_2fa_cli.c $(LDFLAGS)

$(TEST_HTTP): $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) tests/test_http.c include/atn_http.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) tests/test_http.c $(LDFLAGS)

$(CLI_HTTP): $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) src/http/atn_http_cli.c include/atn_http.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) src/http/atn_http_cli.c $(LDFLAGS)

$(TEST_DNS): $(SRC) $(TUN_SRC) $(DNS_SRC) tests/test_dns.c include/atn_dns.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(DNS_SRC) tests/test_dns.c $(LDFLAGS)

$(CLI_DNS): $(SRC) $(TUN_SRC) $(DNS_SRC) src/dns/atn_dns_cli.c include/atn_dns.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(DNS_SRC) src/dns/atn_dns_cli.c $(LDFLAGS)

$(TEST_TREE): $(SRC) $(TREE_SRC) tests/test_tree.c include/atn_tree.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TREE_SRC) tests/test_tree.c $(LDFLAGS)

$(TEST_REPL): $(SRC) $(TUN_SRC) $(TREE_SRC) $(REPL_SRC) tests/test_repl.c include/atn_repl.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(TREE_SRC) $(REPL_SRC) tests/test_repl.c $(LDFLAGS)

$(TEST_HB): $(SRC) $(TUN_SRC) $(HB_SRC) tests/test_hb.c include/atn_hb.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(TUN_SRC) $(HB_SRC) tests/test_hb.c $(LDFLAGS)

test: $(TEST_BIN) $(TEST_TUN) $(TEST_2FA) $(TEST_HTTP) $(TEST_DNS) $(TEST_TREE) $(TEST_REPL) $(TEST_HB) $(CLI_2FA) $(CLI_HTTP) $(CLI_DNS)
ifeq ($(CROSS),1)
	@echo "cross-compiled for $(MACHINE) ($(ATN_TARGET_OS)-$(ATN_ARCH))"
	@echo "run binaries on the target; not executing them on the builder"
else
	$(TEST_BIN)
	$(TEST_TUN)
	$(TEST_2FA)
	$(TEST_HTTP)
	$(TEST_DNS)
	$(TEST_TREE)
	$(TEST_REPL)
	$(TEST_HB)
	./$(CLI_2FA) demo
	./$(CLI_HTTP) demo
	./$(CLI_DNS) demo
endif

# Same three commands GitHub Actions runs. Local pre-push runs `make test`.
ci: info test lib

test-unsigned-char:
	$(MAKE) test CFLAGS="-std=c99 -Wall -Wextra -Werror -O2 -Iinclude -Itests -funsigned-char"

lib: $(LIB_BIN)

$(LIB_BIN): $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) $(DNS_SRC) $(TREE_SRC) $(REPL_SRC) $(HB_SRC) include/atn_crypto.h include/atn_platform.h include/atn_tun.h include/atn_2fa.h include/atn_http.h include/atn_dns.h include/atn_tree.h include/atn_repl.h include/atn_hb.h
	$(CC) $(CFLAGS) -c $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HTTP_SRC) $(DNS_SRC) $(TREE_SRC) $(REPL_SRC) $(HB_SRC)
	$(AR) rcs $@ atn_platform.o atn_secure.o atn_sha256.o atn_sha512.o atn_hmac.o atn_hkdf.o atn_fips202.o atn_mlkem.o atn_chacha20.o atn_poly1305.o atn_aead.o atn_nonce.o atn_tun.o atn_2fa.o atn_http.o atn_dns.o atn_tree.o atn_repl.o atn_hb.o

clean:
	-rm -f $(TEST_BIN) $(TEST_TUN) $(TEST_2FA) $(TEST_HTTP) $(TEST_DNS) $(TEST_TREE) $(TEST_REPL) $(TEST_HB) $(CLI_2FA) $(CLI_HTTP) $(CLI_DNS) $(LIB_BIN) atn_*.o android/libatn.so
	-cmd /c "del /Q tests\test_crypto.exe tests\test_tun.exe tests\test_2fa.exe tests\test_http.exe tests\test_dns.exe tests\test_tree.exe tests\test_repl.exe tests\test_hb.exe atn2fa.exe atnhttp.exe atndns.exe libatn_crypto.a atn_*.o 2>NUL"

# Android NDK aarch64 shared lib + javac against platform android.jar (DEC-0015).
# knoxsdk.jar is optional: without it we compile stubs (not a device build).
ANDROID_NDK ?= $(LOCALAPPDATA)/Android/Sdk/ndk/27.3.13750724
ANDROID_CC  ?= $(ANDROID_NDK)/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android21-clang.cmd
ANDROID_AR  ?= $(ANDROID_NDK)/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-ar.exe
ANDROID_JAR ?= $(LOCALAPPDATA)/Android/Sdk/platforms/android-31/android.jar

JNI_SRC = android/jni/atn_jni.c

android-so:
	$(MAKE) CC="$(ANDROID_CC)" AR="$(ANDROID_AR)" lib
	$(ANDROID_CC) -shared -o android/libatn.so $(JNI_SRC) $(SRC) $(TUN_SRC) $(AUTH_SRC) $(HB_SRC) -Iinclude -llog

android-java:
	@echo "javac against $(ANDROID_JAR) + android/stubs (STUB BUILD unless vendor/knox/knoxsdk.jar exists)"
	javac -source 8 -target 8 -bootclasspath "$(ANDROID_JAR)" -d android/out \
		android/stubs/com/samsung/android/knox/EnterpriseDeviceManager.java \
		android/stubs/com/samsung/android/knox/restriction/RestrictionPolicy.java \
		android/stubs/com/samsung/android/knox/devicesecurity/PasswordPolicy.java \
		android/java/com/athanor/daemon/AtnNative.java \
		android/java/com/athanor/daemon/AtnKnoxPolicy.java \
		android/java/com/athanor/daemon/AtnDeviceAdminReceiver.java \
		android/java/com/athanor/daemon/AtnDaemonService.java
