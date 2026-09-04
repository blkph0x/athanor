/*
 * Athanor HTTP listener (REQ-2.1). Spec: docs/HTTP.md, DEC-0009.
 *
 * Purpose:  Loopback TCP listener. DEC-0007 records, then HTTP/1.1.
 * Spec:     RFC 9112 (HTTP/1.1 framing), RFC 9110 (methods, Host),
 *           docs/TUNNEL.md (handshake and AEAD records).
 * Policy:   No OpenSSL/libtls/nghttp2. Bind 127.0.0.1 only. Pages are
 *           static bytes in this binary. Not RFC 8446 TLS (ISS-0009).
 */
#ifndef ATN_HTTP_H
#define ATN_HTTP_H

#include "atn_crypto.h"

#define ATN_HTTP_MAX_HDR     8192u
#define ATN_HTTP_MAX_PT      8192u
#define ATN_HTTP_PATH_MAX    128u
#define ATN_HTTP_HOST_MAX    128u
#define ATN_HTTP_BACKLOG     8
#define ATN_HTTP_IDLE_MS     5000
#define ATN_HTTP_CLI_PORT    2401u

#define ATN_HTTP_M_GET       1
#define ATN_HTTP_M_HEAD      2

typedef struct {
    int      method;                         /* ATN_HTTP_M_GET or _HEAD */
    char     path[ATN_HTTP_PATH_MAX];
    char     host[ATN_HTTP_HOST_MAX];
} atn_http_req;

typedef struct {
    intptr_t listen_sock;
    uint16_t port;
    uint8_t  ek[ATN_MLKEM1024_EK_LEN];
    uint8_t  dk[ATN_MLKEM1024_DK_LEN];
    uint8_t  last_wire[16u + ATN_HTTP_MAX_PT + 16u];
    size_t   last_wire_len;
    int      last_rc;
} atn_http_srv;

typedef struct {
    int      state;
    int      initiator;
    intptr_t sock;
    uint8_t  k_ack[32];
    uint8_t  k_send[32];
    uint8_t  k_recv[32];
    uint64_t send_seq;
    uint64_t recv_max;
    uint64_t recv_win;
    uint8_t  peer_ek[ATN_MLKEM1024_EK_LEN];
    uint8_t  kem_ct[ATN_MLKEM1024_CT_LEN];
    uint8_t  confirm[32];
    uint8_t  last_wire[16u + ATN_HTTP_MAX_PT + 16u];
    size_t   last_wire_len;
} atn_http_cli;

/*
 * Purpose:  Parse one HTTP/1.1 request from a complete header block.
 * Spec:     RFC 9112 §§2–3, docs/HTTP.md.
 * Params:   buf[0..n) is the DATA plaintext. out is written only on ATN_OK.
 * Returns:  ATN_OK, ATN_ERR_PARAM (malformed), ATN_ERR_STATE (method not
 *           GET/HEAD), ATN_ERR_LEN (header block exceeds 8192).
 */
int atn_http_parse_request(const uint8_t *buf, size_t n, atn_http_req *out);

/*
 * Purpose:  Exact memory-resident page bodies (no NUL in the count).
 * Spec:     REQ-2.1 static-from-memory gate; docs/HTTP.md marker strings.
 */
const uint8_t *atn_http_page_root(size_t *n);
const uint8_t *atn_http_page_admin(size_t *n);

/*
 * Purpose:  Bind 127.0.0.1:port (0 = ephemeral) and listen.
 * Spec:     DEC-0009 loopback-only. Identity = ek/dk pair from the caller.
 * Returns:  ATN_OK or ATN_ERR_SOCK / ATN_ERR_PARAM.
 */
int atn_http_listen(atn_http_srv *s, uint16_t port,
                    const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                    const uint8_t dk[ATN_MLKEM1024_DK_LEN]);

uint16_t atn_http_port(const atn_http_srv *s);

/*
 * Purpose:  Accept one TCP client, handshake, one HTTP request, close.
 * Spec:     docs/HTTP.md process model. timeout_ms applies to accept/recv.
 * Returns:  ATN_OK if a response was sent; ATN_ERR_STATE if the peer was
 *           unauthenticated (no page bytes written).
 */
int atn_http_serve_one(atn_http_srv *s, int timeout_ms);

void atn_http_close(atn_http_srv *s);

/*
 * Purpose:  Client for tests and atnhttp demo. Not a browser.
 * Spec:     Same records as the server. Sequence for in-process tests:
 *           open → send_init → send_http → (server serve_one) → finish.
 */
int atn_http_cli_open(atn_http_cli *c, uint16_t port,
                      const uint8_t ek[ATN_MLKEM1024_EK_LEN]);
int atn_http_cli_send_init(atn_http_cli *c);
int atn_http_cli_send_http(atn_http_cli *c, const char *method, const char *path);
int atn_http_cli_finish(atn_http_cli *c, uint8_t *resp, size_t *n, size_t max,
                        int timeout_ms);
void atn_http_cli_wipe(atn_http_cli *c);

#endif
