# Athanor tunnel wire format (DEC-0007)

REQ-1.2. This file is the packet spec. Code must match it. Do not change
a field without a new DEC.

Identity is a static **ML-KEM-1024 encapsulation key** distributed out of
band (the same model as a WireGuard public key). ML-DSA-87 is not used
yet (ISS-0005). A peer that encapsulates to the wrong ek gets garbage.

## UDP datagram

Little-endian fields. One tunnel message = one UDP datagram.

```
offset  bytes  field
0       1      version     must be 1
1       1      type        see below
2       2      reserved    must be 0
4       4      length      payload bytes after this 16-byte header
8       8      seq         per-direction counter (DATA/KA/CLOSE)
16      length payload
```

Maximum payload we will emit: 1024 bytes (DATA). HS_INIT payload is
exactly `ATN_MLKEM1024_CT_LEN` (1568).

## Types

| type | name    | payload |
|---:|---|---|
| 1 | HS_INIT | ML-KEM-1024 ciphertext (initiator → responder) |
| 2 | HS_ACK  | ChaCha20-Poly1305(ct \|\| tag) of a 32-byte confirm |
| 3 | DATA    | ChaCha20-Poly1305(ct \|\| tag) of application bytes |
| 4 | KA      | ChaCha20-Poly1305 of empty plaintext (keepalive) |
| 5 | CLOSE   | ChaCha20-Poly1305 of empty plaintext, then wipe |

AEAD AAD is the 16-byte header. Tag is 16 bytes, appended after ciphertext
(RFC 8439). `length` includes ciphertext and tag.

## Handshake (one-way KEM, HPKE-style)

1. Initiator: `(ss, kem_ct) ← ML-KEM.Encaps(responder_ek)`
2. Both sides (responder after Decaps) derive 96 bytes:
   ```
   salt = SHA3-256(responder_ek)
   ikm  = ss                          (32 bytes)
   info = "atn-tun-v1" ‖ kem_ct
   okm  = HKDF-SHA-512(salt, ikm, info, 96)
   k_ack = okm[0:32]
   k_i2r = okm[32:64]                 initiator → responder DATA
   k_r2i = okm[64:96]                 responder → initiator DATA
   ```
3. Initiator sends HS_INIT with payload = kem_ct (plaintext KEM ct).
4. Responder sends HS_ACK: AEAD under `k_ack`, nonce sender=0 seq=0,
   plaintext = SHA3-256(kem_ct). `k_ack` is used once.
5. Initiator opens HS_ACK; confirm must match SHA3-256(kem_ct).
6. DATA seq starts at 1 on each data key. Nonce = RFC 8439 partition
   (DEC-0002): sender 1 = initiator, sender 2 = responder; counter = seq.

## Anti-replay

64-packet sliding window on `seq` per data key. Drop if seq is older than
`highest-63` or already seen. MAC failure → CLOSED, keys wiped, no decrypt
of a second try with the same packet.

## State

`CLOSED → HANDSHAKE → ESTABLISHED → CLOSED`

REKEY is not implemented. If `seq` would exceed 2^64−2 the session closes
(ISS-0008). A new handshake is a new tunnel object.

## Sockets

IPv4 UDP only, OS sockets (`sendto`/`recvfrom`). IPv6 is ISS-0007.
Windows links `ws2_32`; POSIX uses BSD sockets. No libuv, no boost.asio.
