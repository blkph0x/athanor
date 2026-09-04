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
| `src/crypto/atn_fips202.c` | REQ-1.1-PQ | FIPS 202 | Keccak-f[1600], SHA3-256/512, SHAKE128/256. Incremental finalize zeros unused rate tail. |
| `src/crypto/atn_mlkem.c` | REQ-1.1-PQ | FIPS 203 | ML-KEM-1024 only |
| `src/crypto/atn_mldsa.c` | REQ-5.1 | FIPS 204 / DEC-0018 | ML-DSA-87 only. Hedged Sign; internal APIs for KATs |
| `tests/kat_mldsa87.h` | REQ-5.1 | ACVP FIPS204 | First ML-DSA-87 keyGen + Sign_internal (tgId=12) |
| `tests/test_mldsa.c` | REQ-5.1 | gates | keygen/sign/verify KATs, tamper, hedged roundtrip |
| `tests/kat_mlkem1024.h` | REQ-1.1-PQ | FIPS 203 KAT | First official-style ML-KEM-1024 vector |
| `docs/TUNNEL.md` | REQ-1.2 | DEC-0007 | Wire format. Code must match. |
| `include/atn_tun.h` / `src/tun/atn_tun.c` | REQ-1.2 | DEC-0007 / 0021 | IPv4 UDP tunnel; `bind_any` is INADDR_ANY |
| `include/atn_2fa.h` / `src/auth/atn_2fa.c` | REQ-1.3 | DEC-0008 | Challenge-response 2FA |
| `src/auth/atn_2fa_cli.c` | REQ-1.3 | DEC-0008 | `atn2fa` standalone binary |
| `tests/test_tun.c` | REQ-1.2 | gates | Loopback handshake, echo, replay, bad MAC |
| `tests/test_2fa.c` | REQ-1.3 | gates | Enroll, wrong key, replay, lockout |
| `docs/HTTP.md` | REQ-2.1 | DEC-0009 | Listener spec. Not TLS 1.3. |
| `include/atn_http.h` / `src/http/atn_http.c` | REQ-2.1 | DEC-0009 | Loopback TCP + DEC-0007 records + HTTP/1.1. Reuses `atn_net_init` in `atn_tun.c`. |
| `src/http/atn_http_cli.c` | REQ-2.1 | DEC-0009 | `atnhttp` standalone binary |
| `tests/test_http.c` | REQ-2.1/2.2 | gates | Parse, GET exact `/`, unauth close, login/2FA/wipe mutate |
| `docs/DNS.md` | REQ-2.3 | DEC-0011 | RFC 1035 subset. Recursion off. |
| `include/atn_dns.h` / `src/dns/atn_dns.c` | REQ-2.3 | RFC 1035 | Authoritative zone `atn.test`. Reuses `atn_net_init`. |
| `src/dns/atn_dns_cli.c` | REQ-2.3 | DEC-0011 | `atndns` binary |
| `tests/test_dns.c` | REQ-2.3 | gates | In-zone A, REFUSED, NXDOMAIN, loopback querier |
| `include/atn_tree.h` / `src/store/atn_tree.c` | REQ-3.2 | DEC-0012 | AVL blobs + AEAD snapshot |
| `tests/test_tree.c` | REQ-3.2 | gates | put/get/del/scan, snapshot hides plaintext, restore |
| `docs/REPL.md` | REQ-3.1 | DEC-0013 | Block, shard, vector clock, PUT/CATCHUP |
| `include/atn_repl.h` / `src/repl/atn_repl.c` | REQ-3.1 | DEC-0013 | Replicate AEAD blocks over the tunnel |
| `tests/test_repl.c` | REQ-3.1 | gates | A→B, kill A, tamper AUTH, catch-up, shard |
| `include/atn_hb.h` / `src/hb/atn_hb.c` | REQ-3.3 | DEC-0014 | HMAC-SHA-512 heartbeat |
| `tests/test_hb.c` | REQ-3.3 | gates | 3-node live, forge, silence wipe, one UDP hop, 3-pair mesh + lossy AC |
| `android/` | REQ-4.1 | DEC-0015 | Daemon Java + JNI. Stubs until knoxsdk.jar. |
| `android/jni/atn_jni.c` | REQ-4.1 | DEC-0015 | JNI to libatn.so |
| `vendor/knox/README.md` | REQ-4.1 | DEC-0015 | Drop-in path for knoxsdk.jar |
| `include/atn_dmon.h` / `src/dmon/atn_dmon.c` | REQ-4.1/4.4 | DEC-0016/0017/0020/0021 | Native session; hb/2FA flush; DEC-0007 tunnel; bind_any |
| `tools/src.list` | REQ-5.1 | DEC-0020 | Frozen path list for `atnsign manifest` |
| `tests/test_recipe.c` | REQ-5.1 | DEC-0020 | Makefile must not contain fetch URLs |
| `tests/test_dmon.c` | REQ-4.4 | gates | flush zeros keys; silence UNTRUSTED; 2FA lockout |
| `android/java/.../AtnKeystore.java` | REQ-4.1 | DEC-0016/0017 | AndroidKeyStore AES-256 GCM wrap, StrongBox then TEE |
| `android/java/.../AtnPowerReceiver.java` | REQ-4.3 | DEC-0017 | Re-assert USB on ACTION_POWER_CONNECTED |
| `android/java/.../AtnBootReceiver.java` | REQ-4.1 | DEC-0015 | Start daemon on BOOT_COMPLETED |
| `android/java/.../AtnKnoxBuild.java` | REQ-4.1 | DEC-0019 | `isStub()` via ATN_STUB field |
| `include/atn_sign.h` / `src/sign/atn_sign.c` | REQ-5.1 | DEC-0019 / 0021 | SHA3-256 manifest + ML-DSA-87; ATN-REPORT-1 |
| `src/sign/atn_sign_cli.c` | REQ-5.1 | DEC-0019 / 0021 | `atnsign` CLI (manifest + report) |
| `tests/test_sign.c` | REQ-5.1 | gates | sort, sign, verify, tamper; report |
| `docs/SIGN.md` | REQ-5.1 | DEC-0019 / 0021 | Manifest + report wire format |
| `include/atn_cfg.h` / `src/cfg/atn_cfg.c` | REQ-4.1 | DEC-0021 | Lab `atn-node.conf` parser |
| `tests/test_cfg.c` | REQ-4.1 | gates | parse 127.0.0.1:2402, unknown keys fail |
| `src/node/atn_node_cli.c` | REQ-4.1 | DEC-0021 | `atnnode` lab responder |
| `android/java/.../AtnNodeConfig.java` | REQ-4.1 | DEC-0021 | Java mirror of atn_cfg.c |
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
