# Athanor replication (DEC-0013)

REQ-3.1. This file is the block/shard/clock spec. Code must match it.

Blocks travel **only** as tunnel DATA (DEC-0007). There is no plaintext
TCP for bodies. A second AEAD (cluster key, RFC 8439) protects the
value at rest in the REQ-3.2 tree so a flipped stored tag is rejected
even after a good tunnel MAC.

## Constants

| name | value | why |
|---|---:|---|
| `ATN_REPL_MAX_NODES` | 4 | roster cap for this DEC |
| `ATN_REPL_FACTOR` | 2 | minimum replica count (compile-time) |
| `ATN_REPL_SHARDS` | 8 | shard table size |
| `ATN_REPL_ID_LEN` | 8 | node id = first 8 bytes of SHA3-256(ek) or a test id |
| `ATN_REPL_MAX_KEY` | 64 | must fit a tunnel DATA with clocks + AEAD |
| `ATN_REPL_MAX_VAL` | 256 | same |

Changing shards or the hash is a migration, not a silent tweak.

## Shard function

```
h     = SHA3-256(key)           (FIPS 202)
shard = big-endian u64(h[0..7]) % ATN_REPL_SHARDS
```

Responsible nodes, given roster `[0..n)` in **roster order**:

```
owners[i] = roster[(shard + i) % n]   for i in 0 .. min(FACTOR,n)-1
```

The map is a pure function of `(key, roster)`. Same key, same roster ⇒
same owners. Tests assert that.

## Vector clock

Per key, a list of `(node_id[8], seq u64le)` pairs, at most
`ATN_REPL_MAX_NODES`. Missing id is seq 0.

Compare (standard dominance, not LWW):

- A dominates B iff every id in the union has `seqA >= seqB` and at
  least one `>`.
- Equal ⇒ identical version; ignore the duplicate.
- Incomparable ⇒ **conflict**. Do not last-writer-wins. Keep the local
  value, set the record’s conflict flag, return `ATN_ERR_CONFLICT`.

A put on node X increments X’s component, then stores and replicates.

## Block AEAD

```
nonce = 12 random bytes
ct||tag = ChaCha20-Poly1305(cluster_key, nonce, aad=key, pt=value)
```

`cluster_key` is 32 bytes, distributed out of band (same model as a
static ML-KEM ek). AAD binds ciphertext to the user key.

## Tree record (value bytes)

```
nclock     u8
nclock × { id[8], seq u64le }
nonce      12
clen       u16le
ct         clen
tag        16
conflict   u8
```

## Tunnel DATA plaintext

```
type u8
  1 PUT          klen u8, key[klen], tree-record
  2 CATCHUP_REQ  empty — peer replies with a PUT per key it holds
```

A node with `online=0` stores local puts but does not send (offline
simulation). Catch-up: the returning node sends `CATCHUP_REQ`; the
peer emits PUTs.

## Kill / tamper gates

- Wipe node A’s tree (`atn_repl_wipe`). Gets on B still succeed.
- Apply a PUT with a flipped tag → `ATN_ERR_AUTH`; tree unchanged.
