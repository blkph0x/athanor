# Specification index

We do not invent cryptographic algorithms. We transcribe published ones.
If a constant is not in this list, it does not go in the tree.

| ID | Document | What we take from it | Used by |
|---|---|---|---|
| FIPS-180-4 / RFC 6234 | US Secure Hash Algorithms (SHA-256 sections 4.1, 5.1, 6.1, 6.2) | Padding, functions CH/MAJ/BSIG/SSIG, K[0..63], H(0), compression | REQ-1.1 hash |
| RFC 2104 | HMAC | ipad 0x36, opad 0x5c, key hashing when key > block size | REQ-1.1 MAC |
| RFC 4231 | HMAC-SHA-256 test vectors §§4.2–4.8 | Known-answer tests | REQ-1.1 tests |
| RFC 5869 | HKDF extract-and-expand, Appendix A.1–A.3 | PRK/OKM construction and SHA-256 KATs | REQ-1.1 KDF |
| RFC 8439 | ChaCha20, Poly1305, AEAD_CHACHA20_POLY1305 | Quarter round, block function, encrypt, clamp, AEAD layout, all KATs we ship | REQ-1.1 AEAD |
| RFC 8439 §2.3 | Nonce partition | 32-bit sender id + remaining nonce unique per key | REQ-1.1 nonce |
| RFC 8439 §4 | Side channels | Constant-time tag compare; no memcmp on tags | REQ-1.1 ct_equal |
| Win32 BCrypt / POSIX getrandom | OS CSPRNG | Entropy source, not an algorithm we designed | REQ-1.1 random |
| FIPS 202 | SHA-3 / SHAKE (Keccak-p[1600,24]) | SHA3-256, SHA3-512, SHAKE128, SHAKE256, round constants Table 2 | REQ-1.1-PQ |
| FIPS 203 | ML-KEM-1024 | n=256 q=3329 k=4 η1=η2=2 du=11 dv=5; Algorithms 7–21; ζ=17 | REQ-1.1-PQ |
| RFC 6234 SHA-512 | SHA-512 / HMAC-SHA-512 | §§4.2, 5.2, 6.3–6.4; RFC 4231 HMAC-SHA-512 KATs | REQ-1.1-PQ |
| RFC 9112 | HTTP/1.1 | Request line, header block, CRLF, Host required | REQ-2.1 |
| RFC 9110 | HTTP semantics | Method case-sensitivity, Host, 4xx status | REQ-2.1 |
| docs/TUNNEL.md | DEC-0007 records | TCP framing of the same 16-byte header + ML-KEM handshake | REQ-2.1 |

SHA-256 one-block and two-block message digests are the FIPS 180-4 examples
also printed throughout the literature:

- `""` (empty) → `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
- `"abc"` → `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`
- `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"` → `248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1`

These three are treated as FIPS 180-4 Appendix examples / NIST CAVS fixtures
and are stored next to the test driver with that citation. If a future audit
disagrees, open an issue — do not "fix from memory."
