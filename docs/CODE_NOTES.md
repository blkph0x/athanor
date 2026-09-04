# Code notes (tree map)

If a source file is not in this table, the map is wrong — fix the map in the
same commit.

| Path | REQ | Spec | Notes |
|---|---|---|---|
| `include/atn_crypto.h` | REQ-1.1 | (API) | Public crypto header. Includes `atn_platform.h`. |
| `include/atn_platform.h` | REQ-1.1 | DEC-0004 | OS/arch macros from the compiler. Unknown OS → `#error`. |
| `src/crypto/atn_platform.c` | REQ-1.1 | DEC-0004 | `atn_platform_id()` string |
| `src/crypto/atn_secure.c` | REQ-1.1 | RFC 8439 §4; OS CSPRNG | `atn_memzero`, `atn_ct_equal`, `atn_random_bytes` (BCrypt / arc4random / getrandom / urandom) |
| `src/crypto/atn_sha256.c` | REQ-1.1 | RFC 6234 §§4.1,5.1,6.1,6.2 | SHA-256 only. Big-endian words. |
| `src/crypto/atn_sha512.c` | REQ-1.1-PQ | RFC 6234 SHA-512 | SHA-512, HMAC-SHA-512, HKDF-SHA-512 |
| `src/crypto/atn_fips202.c` | REQ-1.1-PQ | FIPS 202 | Keccak-f[1600], SHA3-256/512, SHAKE128/256 |
| `src/crypto/atn_mlkem.c` | REQ-1.1-PQ | FIPS 203 | ML-KEM-1024 only |
| `tests/kat_mlkem1024.h` | REQ-1.1-PQ | FIPS 203 KAT | First official-style ML-KEM-1024 vector |
| `docs/TUNNEL.md` | REQ-1.2 | DEC-0007 | Wire format. Code must match. |
| `include/atn_tun.h` / `src/tun/atn_tun.c` | REQ-1.2 | DEC-0007 | IPv4 UDP tunnel |
| `include/atn_2fa.h` / `src/auth/atn_2fa.c` | REQ-1.3 | DEC-0008 | Challenge-response 2FA |
| `src/auth/atn_2fa_cli.c` | REQ-1.3 | DEC-0008 | `atn2fa` standalone binary |
| `tests/test_tun.c` | REQ-1.2 | gates | Loopback handshake, echo, replay, bad MAC |
| `tests/test_2fa.c` | REQ-1.3 | gates | Enroll, wrong key, replay, lockout |
| `docs/HTTP.md` | REQ-2.1 | DEC-0009 | Listener spec. Not TLS 1.3. |
| `include/atn_http.h` / `src/http/atn_http.c` | REQ-2.1 | DEC-0009 | Loopback TCP + DEC-0007 records + HTTP/1.1. Reuses `atn_net_init` in `atn_tun.c`. |
| `src/http/atn_http_cli.c` | REQ-2.1 | DEC-0009 | `atnhttp` standalone binary |
| `tests/test_http.c` | REQ-2.1 | gates | Parse rejects, GET exact bytes, unauth close, ciphertext hides pages |
| `src/crypto/atn_hmac.c` | REQ-1.1 | RFC 2104 | HMAC-SHA-256 |
| `src/crypto/atn_hkdf.c` | REQ-1.1 | RFC 5869 §§2.2–2.3 | Extract then expand, SHA-256 |
| `src/crypto/atn_chacha20.c` | REQ-1.1 | RFC 8439 §§2.1–2.4 | IETF 32-bit counter, 96-bit nonce |
| `src/crypto/atn_poly1305.c` | REQ-1.1 | RFC 8439 §2.5 | 5×26-bit limbs, clamp r |
| `src/crypto/atn_aead.c` | REQ-1.1 | RFC 8439 §§2.6,2.8 | AEAD_CHACHA20_POLY1305 |
| `src/crypto/atn_nonce.c` | REQ-1.1 | RFC 8439 §2.3 | Sender id + counter; reject reuse |
| `tests/test_crypto.c` | REQ-1.1 | KATs from SPEC_INDEX | Fail-closed driver; prints which vector died |
| `Makefile` | REQ-1.1 | DEC-0004 | Target from `$(CC) -dumpmachine`; `-lbcrypt` only for Windows targets |
| `tools/build.sh` | REQ-1.1 | DEC-0004 | POSIX/ARM/Android builder |
| `tools/build.bat` | REQ-1.1 | DEC-0004 | Windows native builder |
| `docs/BUILD.md` | REQ-1.1 | DEC-0004 | How to build every listed target |
| `.github/workflows/ci.yml` | — | DEC-0006 | Public `make test` on four OS/arch jobs |
| `.githooks/pre-push` | — | DEC-0006 | Refuses push if `make test` fails |
| `tools/ci_local.ps1` | — | DEC-0006 | Local replay of the Actions command set |

Module-level commentary lives in the file headers. Do not duplicate the spec
here — point at it.
