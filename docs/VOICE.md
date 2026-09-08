# Secure voice (DEC-0050; supersedes DEC-0049 SoT)

## Trust model

| Path | Confidentiality |
|---|---|
| **P2P (primary)** | Direct PQ/AEAD tunnel peer↔peer. `'A''F'` inside that tunnel is E2E — hub not on path. |
| **Hub relay (fallback)** | Outer tunnel is hop-by-hop to hub; media is **`'A''S'` nested AEAD** from ML-KEM session to contact `peer_ek`. Hub sees ciphertext only — **not** PCM. |
| Clear `'A''F'` via hub | **Forbidden** for product calls. |

No plaintext / classical / WebRTC fallback. Family `'A'` only (not `'V'`).

## Dial order

1. Latency-probe hubs; pick lowest RTT for signaling / fallback.
2. Exchange dial_info; attempt direct `atn_tun` HS to peer.
3. On P2P ESTABLISHED → media on that tunnel (`'A''F'`).
4. Else encaps to peer_ek, chunk CT over hub, seal media as `'A''S'`, hub forwards opaque frames.
5. If peer becomes reachable, hand off to P2P.

## Wire (family `'A'`)

- `'A''C'` CONTROL — offer/accept/reject/hangup/busy/keepalive/codec/dial_info/e2e_ct/probe
- `'A''F'` AUDIO — clear codec payload (**P2P tunnel only**)
- `'A''S'` SEALED — call_id/seq/ts + AEAD(ct‖tag) of codec‖payload (**hub path**)

## Contacts

Label + peer_ek + ipv4:port + optional hubs. Android filesDir lab store.
