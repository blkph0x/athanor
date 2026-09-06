# Athanor HTTP listener (DEC-0009)

REQ-2.1. This file is the protocol spec. Code must match it. Do not change
a field without a new DEC.

This is **not** RFC 8446 TLS. Browsers will not speak it (ISS-0009).
The SoT asks for a custom HTTP/TLS handshake using REQ-1.1 math. We
reuse the already-specified tunnel handshake (DEC-0007 / `docs/TUNNEL.md`)
on IPv4 TCP, then parse HTTP/1.1 (RFC 9112) from decrypted DATA.

## Transport

IPv4 TCP, OS sockets (`socket` / `bind` / `listen` / `accept` / `send` /
`recv`). Windows links `ws2_32`; POSIX uses BSD sockets. No OpenSSL, no
libtls, no nghttp2, no libuv.

Default bind: `127.0.0.1` only. There is no bind-any API in this DEC.
A public bind would be a new DEC plus an explicit flag; it is not a
compile default.

CLI default port if none is given: **2401** on loopback. 2401 is an
Athanor assignment, not an IANA service. Tests bind port 0 (ephemeral).

## Record framing (TCP)

TCP is a byte stream. Each record is concatenated:

```
16-byte header  (identical to TUNNEL.md)
`length` payload bytes
```

Header (little-endian), copied from TUNNEL.md:

```
offset  bytes  field
0       1      version     must be 1
1       1      type        HS_INIT=1 HS_ACK=2 DATA=3 KA=4 CLOSE=5
2       2      reserved    must be 0
4       4      length      payload bytes after the header
8       8      seq         per-direction counter (DATA/KA/CLOSE)
```

Maximum DATA plaintext: **8192** bytes (larger than the UDP tunnel’s
1024 because there is no datagram MTU here). `length` for DATA is
plaintext + 16-byte Poly1305 tag.

Handshake bytes, key schedule (`info = "atn-tun-v1" ‖ kem_ct`), nonces,
replay window, and MAC-fail-close are **exactly** DEC-0007. Identity is
the responder’s static ML-KEM-1024 encapsulation key, distributed out
of band.

## HTTP/1.1 inside DATA (RFC 9112)

After ESTABLISHED, each DATA plaintext is one HTTP message.

We accept:

- Methods `GET`, `HEAD`, and `POST` (RFC 9110 §9: method names are
  case-sensitive). `POST` is DEC-0010 (REQ-2.2).
- Version `HTTP/1.1` only.
- Origin-form target (`/…`), max 127 bytes, charset
  `A–Z a–z 0–9 / . _ -`. Must start with `/`. No `..`, no `//`, no `?`
  (query strings wait for a DEC), no NUL, no bare CR or LF.
- Header block max **8192** bytes including the terminator.
- `Host` header required (RFC 9112 §3.2 / RFC 9110 §7.2). Duplicate
  `Host` is rejected.
- `Transfer-Encoding` is rejected (we do not implement chunked).
- `Content-Length`, if present, must be `0` for GET/HEAD. POST requires
  Content-Length in `1..1024` (or `0` only if we reject the empty mutate).
  POST also requires `Content-Type: application/x-www-form-urlencoded`.
  `%` and `+` in the body are rejected (ISS-0011).
- Strict CRLF. Bare LF is rejected.

We emit:

```
HTTP/1.1 <code> <reason>\r\n
Content-Type: text/html; charset=us-ascii\r\n
Content-Length: <n>\r\n
Connection: close|keep-alive\r\n
Cache-Control: no-store\r\n
\r\n
<body>          (omitted for HEAD; Content-Length still the GET size)
```

| Condition | Status |
|---|---|
| `GET`/`HEAD` `/` | 200, public page `ATN-PUBLIC-PLACEHOLDER` |
| `GET`/`HEAD` `/admin` | 200, login (`ATN-LOGIN-PAGE`) or console (`ATN-CONSOLE-PAGE`) |
| `POST /admin/challenge` | issue 2FA challenge for `id` (not a mesh mutate) |
| `POST /admin/login` | verify 2FA, mark session authed |
| `POST /admin/do` | mutate (`action=wipe` or `hold`); requires authed + fresh 2FA + CSRF |
| unknown path | 404 |
| unknown method | 405 |
| malformed | 400 |
| unauthenticated mutate | 401 |
| bad CSRF / bad 2FA on mutate | 403 |
| header block > 8192 or unterminated at the cap | 431 |

Session cookie: `Set-Cookie: ATN-SID=<32 hex>; Path=/; HttpOnly`.
CSRF: `HMAC-SHA-512(server_secret, sid ‖ "atn-csrf-v1")` first 32 bytes,
hex field `csrf`. Compared with `atn_ct_equal`.

HTTP/1.1 persist (RFC 9112 §9.3 / DEC-0024). `Connection: close` ends
the session after that response (our test client still sends close).
Max 8 requests per TCP session. We do not emit pipelined requests.
ISS-0010 closed.

## Unauthenticated sockets

A TCP peer that has not completed HS_ACK is unauthenticated.

If the first record is not a valid `HS_INIT`, the server **closes
without writing page bytes**. A raw `GET /admin` on the socket must
never contain `ATN-ADMIN-PLACEHOLDER` in anything the server sends.

Completing the handshake proves knowledge of the responder ek (same
identity model as the tunnel). 2FA on mutating console actions is
REQ-2.2, not this REQ.

## Process model

Single-threaded accept → handshake → up to `ATN_HTTP_KA_MAX` (8) HTTP
requests → close (DEC-0024). Listen backlog is 8. Idle recv timeout is
5000 ms. Concurrent TCP clients are not multiplexed (DEC-0025: one
`serve_one` at a time).

## Operator client (DEC-0026 / ISS-0009-b)

Browsers cannot speak DEC-0009 records. Operators use the in-tree CLI:

```
atnhttp serve-once [port]     # prints peer_ipv4 / peer_port / peer_ek; one client
# save those three lines as a DEC-0021 conf (peer_ipv4 must be 127.0.0.1)
atnhttp get <conf> <path>     # handshake + GET; HTTP response on stdout
```

`atnhttp demo` gates GET / and /admin plus conf write/reload in-process.
This is not RFC 8446 TLS.

## Pages

Bodies are `static const` byte arrays compiled into the binary. No
`fopen` of a document root. No CGI. No template engine. No CDN URL
appears in the bytes.
