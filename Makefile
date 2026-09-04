# Athanor REQ-1.1 — GCC recipe. No package fetch. DEC-0001.
# Host survey: gcc.exe (GCC) 11.3.0 on Windows (docs/BUILD_NOTES.md BN-0001).

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS =

ifeq ($(OS),Windows_NT)
  LDFLAGS += -lbcrypt
  EXE := .exe
else
  EXE :=
endif

SRC = \
	src/crypto/atn_secure.c \
	src/crypto/atn_sha256.c \
	src/crypto/atn_hmac.c \
	src/crypto/atn_hkdf.c \
	src/crypto/atn_chacha20.c \
	src/crypto/atn_poly1305.c \
	src/crypto/atn_aead.c \
	src/crypto/atn_nonce.c

.PHONY: all test clean

all: tests/test_crypto$(EXE)

tests/test_crypto$(EXE): $(SRC) tests/test_crypto.c include/atn_crypto.h
	$(CC) $(CFLAGS) -o $@ $(SRC) tests/test_crypto.c $(LDFLAGS)

test: tests/test_crypto$(EXE)
	./tests/test_crypto$(EXE)

clean:
	rm -f tests/test_crypto$(EXE) tests/test_crypto.exe
