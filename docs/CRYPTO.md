# Crypto floor (DEC-0033)

**Law:** `SOURCE_OF_TRUTH.md` Tier 1. **Decision:** DEC-0033.  
**Index:** `docs/SPEC_INDEX.md`. Never invent algorithms.

This is the **minimum** Athanor will ship. Raising the floor needs a DEC.
**Lowering it is forbidden.**

## Allowed (product)

| Role | Algorithm | Spec | Notes |
|---|---|---|---|
| KEM / handshake | **ML-KEM-1024** | FIPS 203 | NIST category 5. Only size. |
| Signatures | **ML-DSA-87** | FIPS 204 / DEC-0018 | Category 5. Hedged Sign. |
| Wire / store AEAD | **ChaCha20-Poly1305** | RFC 8439 | 256-bit keys. |
| Hash (legacy KATs + some AAD) | SHA-256 | FIPS 180-4 / RFC 6234 | Still allowed; not a KEM. |
| MAC / long-term KDF | HMAC-SHA-512 / HKDF-SHA-512 | RFC 4231 / 5869 | |
| PQ hash / XOF | SHA3 / SHAKE | FIPS 202 | Required by FIPS 203/204. |
| CSPRNG | OS only | BCrypt / getrandom / arc4random | |
| Phone key wrap | AES-256-GCM | Android Keystore | Hardware wrap of our 32-byte key; not tunnel AEAD. |

CNSA 2.0 alignment (cited pairing): ML-KEM-1024 + ML-DSA-87.

## Forbidden on Athanor protocols (product `src/` / `include/` / `android/`)

| Token / class | Why |
|---|---|
| ML-KEM-512, ML-KEM-768 | Below SoT / category 5 |
| ML-DSA-44, ML-DSA-65 | Below DEC-0018 |
| Classical-only KEX (RSA, ECDH, X25519) as tunnel handshake | Not PQ |
| AES-128, RC4, 3DES, Blowfish | Below floor |
| MD5, SHA-1 as security MAC/hash | Broken / deprecated |
| OpenSSL, libsodium, BoringSSL, WireGuard, liboqs as deps | Third-party / SoT |

Gate: `make test` → `tests/test_recipe` crypto-floor scan (DEC-0033).

## Open (not a downgrade)

| Item | Status |
|---|---|
| PQ tunnel rekey | ISS-0008 — needs TUNNEL.md + DEC |
| IPv6 second socket | ISS-0007 — must not break IPv4 |
| TLS 1.3 / browser | Out of scope; operators use `atnhttp` (ISS-0009) |

## Operator rule

If a PR, paste, or “temporary lab cipher” asks for anything below this
table: **reject**. Use stubs + diag (`DEC-0027`) instead of weakening crypto.
