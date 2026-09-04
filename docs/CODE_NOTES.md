# Code notes (tree map)

If a source file is not in this table, the map is wrong — fix the map in the
same commit.

| Path | REQ | Spec | Notes |
|---|---|---|---|
| `include/atn_crypto.h` | REQ-1.1 | (API) | Only public crypto header. Sizes and error codes live here. |
| `src/crypto/atn_secure.c` | REQ-1.1 | RFC 8439 §4; OS CSPRNG | `atn_memzero`, `atn_ct_equal`, `atn_random_bytes` |
| `src/crypto/atn_sha256.c` | REQ-1.1 | RFC 6234 §§4.1,5.1,6.1,6.2 | SHA-256 only. Big-endian words. |
| `src/crypto/atn_hmac.c` | REQ-1.1 | RFC 2104 | HMAC-SHA-256 |
| `src/crypto/atn_hkdf.c` | REQ-1.1 | RFC 5869 §§2.2–2.3 | Extract then expand, SHA-256 |
| `src/crypto/atn_chacha20.c` | REQ-1.1 | RFC 8439 §§2.1–2.4 | IETF 32-bit counter, 96-bit nonce |
| `src/crypto/atn_poly1305.c` | REQ-1.1 | RFC 8439 §2.5 | 5×26-bit limbs, clamp r |
| `src/crypto/atn_aead.c` | REQ-1.1 | RFC 8439 §§2.6,2.8 | AEAD_CHACHA20_POLY1305 |
| `src/crypto/atn_nonce.c` | REQ-1.1 | RFC 8439 §2.3 | Sender id + counter; reject reuse |
| `tests/test_crypto.c` | REQ-1.1 | KATs from SPEC_INDEX | Fail-closed driver; prints which vector died |
| `Makefile` | REQ-1.1 | DEC-0001 | GCC recipe, no package fetch |

Module-level commentary lives in the file headers. Do not duplicate the spec
here — point at it.
