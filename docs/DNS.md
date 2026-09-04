# Athanor authoritative DNS (DEC-0011)

REQ-2.3. This file is the protocol spec. Code must match it.

We implement a **cited subset of RFC 1035**. We do not recurse. We do
not forward. We do not link a DNS library.

## Transport

IPv4 only (ISS-0007 covers AAAA/IPv6 dual-stack). OS sockets.

- UDP: one query = one datagram, max **512** bytes (RFC 1035 §4.2.1).
  Larger answers set TC and wait for TCP.
- TCP: 2-byte big-endian length prefix, then the message
  (RFC 1035 §4.2.2). If TCP cannot bind the UDP port (observed on
  Windows ephemeral), bind a second port and publish `tcp_port`
  (DEC-0024). UDP stays up.

Default bind: `127.0.0.1`. Tests use port 0. CLI default **1053**
(unprivileged; 53 needs a DEC for production bind and is not the
compile default). 1053 is an Athanor assignment, not an IANA service.

No `sendto`/`connect` except back to the querier. Addresses `8.8.8.8`
and `1.1.1.1` do not appear in this module.

## Header (RFC 1035 §4.1.1)

Big-endian. 12 bytes.

```
ID      16  copied from query
QR       1  1 in responses
OPCODE   4  0 QUERY only; else RCODE=NOTIMP
AA       1  1 if the QNAME is in our zone
TC       1  1 if UDP truncated
RD       1  copied from query
RA       1  0 always (recursion off)
Z        3  0
RCODE    4  see below
QDCOUNT 16
ANCOUNT 16
NSCOUNT 16
ARCOUNT 16
```

RCODE: `NOERROR=0`, `FORMERR=1`, `NXDOMAIN=3`, `NOTIMP=4`, `REFUSED=5`.

- Malformed message → FORMERR, no answers.
- OPCODE ≠ 0 → NOTIMP.
- QNAME outside `atn.test` → **REFUSED** (not forwarded).
- QNAME in-zone, no matching RR of the requested type, but the name
  exists → NOERROR empty answer.
- QNAME in-zone, name does not exist → NXDOMAIN.
- QDCOUNT must be 1. Else FORMERR.

## Types we answer (CLASS IN = 1)

| TYPE | value | when |
|---|---:|---|
| A    | 1  | IPv4 address RDATA (4 bytes, network order) |
| NS   | 2  | apex NS |
| SOA  | 6  | apex SOA |
| TXT  | 16 | character-string (RFC 1035 §3.3.14) |

AAAA (28) for an in-zone name with no AAAA RR: NOERROR, ANCOUNT=0.
We do not synthesise IPv6.

Unknown QTYPE for an in-zone name: NOERROR empty (not NOTIMP), so a
stub resolver does not treat the zone as broken.

## Embedded zone (until REQ-3.2)

Apex: `atn.test` (RFC 2606 / 6761 `.test`).

| name | type | rdata |
|---|---|---|
| `atn.test` | SOA | MNAME `ns.atn.test`, RNAME `hostmaster.atn.test`, SERIAL 1, REFRESH 3600, RETRY 600, EXPIRE 86400, MINIMUM 60 |
| `atn.test` | NS | `ns.atn.test` |
| `ns.atn.test` | A | 127.0.0.1 |
| `node1.atn.test` | A | 127.0.0.1 |
| `atn.test` | TXT | `athanor` |

TTL 60. Names are compared case-insensitively (RFC 1035 §2.3.3).

`atn_dns_upsert_a` / `atn_dns_delete` mutate the in-memory zone. They
are the API the console will call after 2FA; persistence is REQ-3.2.

## Compression

Queries may use RFC 1035 §4.1.4 pointers. The parser follows at most
10 pointers and rejects loops. Answers may compress a NAME to a
pointer at offset 12 (the QNAME) when it is identical.
