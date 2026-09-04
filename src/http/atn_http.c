/*
 * Module: atn_http.c
 * REQ:    REQ-2.1
 * Spec:   docs/HTTP.md, DEC-0009, docs/TUNNEL.md, RFC 9112, RFC 9110
 */

#include "atn_http.h"
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET atn_sock;
#  define ATN_INV  INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int atn_sock;
#  define ATN_INV  (-1)
#endif

/* DEC-0007 info prefix, unchanged on TCP (docs/HTTP.md). */
static const char INFO_PREFIX[10] = {
    'a','t','n','-','t','u','n','-','v','1'
};

static const uint8_t PAGE_ROOT[] =
    "<!DOCTYPE html><html><head><title>Athanor</title></head>"
    "<body><p>ATN-PUBLIC-PLACEHOLDER</p></body></html>";

static const uint8_t PAGE_ADMIN[] =
    "<!DOCTYPE html><html><head><title>Athanor admin</title>"
    "<style>body{font-family:sans-serif;background:#111;color:#ddd;margin:2em}"
    "h1{color:#c9a227}</style></head><body><h1>Athanor</h1>"
    "<p>ATN-ADMIN-PLACEHOLDER</p>"
    "<p>REQ-2.2 console will replace this page. No CDN.</p>"
    "</body></html>";

#define PAGE_ROOT_LEN  (sizeof(PAGE_ROOT) - 1u)
#define PAGE_ADMIN_LEN (sizeof(PAGE_ADMIN) - 1u)

static atn_sock sock_cast(intptr_t v)
{
    return (atn_sock)v;
}

static void sock_close(atn_sock s)
{
    if (s == ATN_INV) {
        return;
    }
#if defined(ATN_OS_WINDOWS)
    closesocket(s);
#else
    close(s);
#endif
}

static void hdr_write(uint8_t h[16], uint8_t type, uint32_t len, uint64_t seq)
{
    h[0] = 1;
    h[1] = type;
    h[2] = 0;
    h[3] = 0;
    h[4] = (uint8_t)len;
    h[5] = (uint8_t)(len >> 8);
    h[6] = (uint8_t)(len >> 16);
    h[7] = (uint8_t)(len >> 24);
    h[8]  = (uint8_t)seq;
    h[9]  = (uint8_t)(seq >> 8);
    h[10] = (uint8_t)(seq >> 16);
    h[11] = (uint8_t)(seq >> 24);
    h[12] = (uint8_t)(seq >> 32);
    h[13] = (uint8_t)(seq >> 40);
    h[14] = (uint8_t)(seq >> 48);
    h[15] = (uint8_t)(seq >> 56);
}

static int hdr_parse(const uint8_t h[16], uint8_t *type, uint32_t *len, uint64_t *seq)
{
    uint32_t L;
    if (h[0] != 1u || h[2] != 0 || h[3] != 0) {
        return ATN_ERR_PARAM;
    }
    L = (uint32_t)h[4] | ((uint32_t)h[5] << 8) |
        ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
    if (L > (ATN_HTTP_MAX_PT + 16u)) {
        return ATN_ERR_LEN;
    }
    *type = h[1];
    *len = L;
    *seq = (uint64_t)h[8] | ((uint64_t)h[9] << 8) |
           ((uint64_t)h[10] << 16) | ((uint64_t)h[11] << 24) |
           ((uint64_t)h[12] << 32) | ((uint64_t)h[13] << 40) |
           ((uint64_t)h[14] << 48) | ((uint64_t)h[15] << 56);
    return ATN_OK;
}

static int tcp_read_n(atn_sock s, uint8_t *buf, size_t n, int timeout_ms)
{
    size_t got = 0;
    while (got < n) {
        fd_set rfds;
        struct timeval tv;
        int r;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
        r = select((int)s + 1, &rfds, NULL, NULL, &tv);
        if (r == 0) {
            return ATN_ERR_STATE;
        }
        if (r < 0) {
            return ATN_ERR_SOCK;
        }
#if defined(ATN_OS_WINDOWS)
        r = recv(s, (char *)buf + (int)got, (int)(n - got), 0);
#else
        r = (int)recv(s, buf + got, n - got, 0);
#endif
        if (r <= 0) {
            return ATN_ERR_SOCK;
        }
        got += (size_t)r;
    }
    return ATN_OK;
}

static int tcp_write_n(atn_sock s, const uint8_t *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int r;
#if defined(ATN_OS_WINDOWS)
        r = send(s, (const char *)buf + (int)sent, (int)(n - sent), 0);
#else
        r = (int)send(s, buf + sent, n - sent, 0);
#endif
        if (r <= 0) {
            return ATN_ERR_SOCK;
        }
        sent += (size_t)r;
    }
    return ATN_OK;
}

static int rec_recv(atn_sock s, uint8_t *dg, size_t *n, size_t max, int timeout_ms)
{
    uint8_t hdr[16];
    uint8_t type;
    uint32_t len;
    uint64_t seq;
    int rc;
    rc = tcp_read_n(s, hdr, 16, timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = hdr_parse(hdr, &type, &len, &seq);
    if (rc != ATN_OK) {
        return rc;
    }
    if (16u + (size_t)len > max) {
        return ATN_ERR_LEN;
    }
    memcpy(dg, hdr, 16);
    if (len > 0u) {
        rc = tcp_read_n(s, dg + 16, (size_t)len, timeout_ms);
        if (rc != ATN_OK) {
            return rc;
        }
    }
    *n = 16u + (size_t)len;
    (void)type;
    (void)seq;
    return ATN_OK;
}

static int derive_keys(uint8_t k_ack[32], uint8_t k_i2r[32], uint8_t k_r2i[32],
                       uint8_t confirm[32],
                       const uint8_t ss[32],
                       const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                       const uint8_t kem_ct[ATN_MLKEM1024_CT_LEN])
{
    uint8_t salt[32], info[10 + ATN_MLKEM1024_CT_LEN], okm[96];
    int rc;
    atn_sha3_256(ek, ATN_MLKEM1024_EK_LEN, salt);
    memcpy(info, INFO_PREFIX, 10);
    memcpy(info + 10, kem_ct, ATN_MLKEM1024_CT_LEN);
    rc = atn_hkdf_sha512(salt, 32, ss, 32, info, sizeof(info), okm, 96);
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(k_ack, okm, 32);
    memcpy(k_i2r, okm + 32, 32);
    memcpy(k_r2i, okm + 64, 32);
    atn_sha3_256(kem_ct, ATN_MLKEM1024_CT_LEN, confirm);
    atn_memzero(salt, sizeof(salt));
    atn_memzero(okm, sizeof(okm));
    atn_memzero(info, sizeof(info));
    return ATN_OK;
}

static int replay_ok(uint64_t *recv_max, uint64_t *recv_win, uint64_t seq)
{
    uint64_t off;
    if (seq == 0) {
        return 0;
    }
    if (seq > *recv_max) {
        uint64_t shift = seq - *recv_max;
        if (shift >= 64u) {
            *recv_win = 0;
        } else {
            *recv_win <<= shift;
        }
        *recv_max = seq;
        *recv_win |= 1ull;
        return 1;
    }
    off = *recv_max - seq;
    if (off >= 64u) {
        return 0;
    }
    if (*recv_win & (1ull << off)) {
        return 0;
    }
    *recv_win |= (1ull << off);
    return 1;
}

static int aead_send(atn_sock s, uint8_t *last, size_t *last_len,
                     const uint8_t key[32], uint32_t sender, uint64_t seq,
                     uint8_t type, const uint8_t *pt, size_t n)
{
    uint8_t hdr[16], nonce[12], tag[16];
    uint8_t pkt[16u + ATN_HTTP_MAX_PT + 16u];
    uint32_t plen;
    int rc;
    if (n > ATN_HTTP_MAX_PT) {
        return ATN_ERR_LEN;
    }
    plen = (uint32_t)(n + 16u);
    hdr_write(hdr, type, plen, seq);
    atn_nonce_format(nonce, sender, seq);
    rc = atn_aead_encrypt(key, nonce, hdr, 16, pt, n, pkt + 16, tag);
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(pkt, hdr, 16);
    memcpy(pkt + 16 + n, tag, 16);
    *last_len = 16u + n + 16u;
    memcpy(last, pkt, *last_len);
    return tcp_write_n(s, pkt, *last_len);
}

static int aead_open(const uint8_t *dg, size_t n, uint32_t len, uint64_t seq,
                     const uint8_t key[32], uint32_t sender,
                     uint8_t *pt, size_t *ptlen, size_t max)
{
    uint8_t hdr[16], nonce[12];
    size_t clen;
    int rc;
    if (len < 16u || n != 16u + (size_t)len) {
        return ATN_ERR_LEN;
    }
    clen = (size_t)len - 16u;
    if (clen > max) {
        return ATN_ERR_LEN;
    }
    memcpy(hdr, dg, 16);
    atn_nonce_format(nonce, sender, seq);
    rc = atn_aead_decrypt(key, nonce, hdr, 16, dg + 16, clen, dg + 16 + clen, pt);
    if (rc != ATN_OK) {
        return ATN_ERR_AUTH;
    }
    *ptlen = clen;
    return ATN_OK;
}

const uint8_t *atn_http_page_root(size_t *n)
{
    if (n != NULL) {
        *n = PAGE_ROOT_LEN;
    }
    return PAGE_ROOT;
}

const uint8_t *atn_http_page_admin(size_t *n)
{
    if (n != NULL) {
        *n = PAGE_ADMIN_LEN;
    }
    return PAGE_ADMIN;
}

static int is_tchar(unsigned char c)
{
    /* RFC 9110 tchar subset we actually allow in methods we emit/accept. */
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    return 0;
}

static int is_path_char(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    if (c >= '0' && c <= '9') {
        return 1;
    }
    return c == '/' || c == '.' || c == '_' || c == '-';
}

static int hdr_name_eq(const uint8_t *p, size_t n, const char *lit)
{
    size_t i;
    for (i = 0; lit[i] != 0; i++) {
        unsigned char a, b;
        if (i >= n) {
            return 0;
        }
        a = p[i];
        b = (unsigned char)lit[i];
        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return i == n;
}

int atn_http_parse_request(const uint8_t *buf, size_t n, atn_http_req *out)
{
    size_t i, line_end, p0, p1, p2, pos, hdr_end;
    size_t meth_n, path_n, ver_n;
    int have_host = 0;
    int have_te = 0;
    unsigned long clen = 0;
    int have_cl = 0;

    if (buf == NULL || out == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));

    hdr_end = (size_t)-1;
    for (i = 0; i + 3u < n; i++) {
        if (buf[i] == '\r' && buf[i + 1u] == '\n' &&
            buf[i + 2u] == '\r' && buf[i + 3u] == '\n') {
            hdr_end = i + 4u;
            break;
        }
        if (buf[i] == 0) {
            return ATN_ERR_PARAM;
        }
    }
    if (hdr_end == (size_t)-1) {
        if (n >= ATN_HTTP_MAX_HDR) {
            return ATN_ERR_LEN;
        }
        return ATN_ERR_PARAM;
    }
    if (hdr_end > ATN_HTTP_MAX_HDR) {
        return ATN_ERR_LEN;
    }

    line_end = (size_t)-1;
    for (i = 0; i + 1u < hdr_end; i++) {
        if (buf[i] == '\r' && buf[i + 1u] == '\n') {
            line_end = i;
            break;
        }
        if (buf[i] == '\n') {
            return ATN_ERR_PARAM;
        }
    }
    if (line_end == (size_t)-1 || line_end == 0) {
        return ATN_ERR_PARAM;
    }

    p0 = 0;
    while (p0 < line_end && buf[p0] != ' ') {
        p0++;
    }
    if (p0 == 0 || p0 >= line_end) {
        return ATN_ERR_PARAM;
    }
    p1 = p0 + 1u;
    p2 = p1;
    while (p2 < line_end && buf[p2] != ' ') {
        p2++;
    }
    if (p2 >= line_end || p2 == p1) {
        return ATN_ERR_PARAM;
    }
    if (p2 + 1u >= line_end) {
        return ATN_ERR_PARAM;
    }
    /* exactly two spaces: no extra token */
    for (i = p2 + 1u; i < line_end; i++) {
        if (buf[i] == ' ') {
            return ATN_ERR_PARAM;
        }
    }

    meth_n = p0;
    path_n = p2 - (p0 + 1u);
    ver_n = line_end - (p2 + 1u);

    if (meth_n == 3u && memcmp(buf, "GET", 3) == 0) {
        out->method = ATN_HTTP_M_GET;
    } else if (meth_n == 4u && memcmp(buf, "HEAD", 4) == 0) {
        out->method = ATN_HTTP_M_HEAD;
    } else {
        for (i = 0; i < meth_n; i++) {
            if (!is_tchar(buf[i])) {
                return ATN_ERR_PARAM;
            }
        }
        return ATN_ERR_STATE;
    }

    if (path_n == 0 || path_n >= ATN_HTTP_PATH_MAX || buf[p0 + 1u] != '/') {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < path_n; i++) {
        unsigned char c = buf[p0 + 1u + i];
        if (!is_path_char(c)) {
            return ATN_ERR_PARAM;
        }
        if (i + 1u < path_n && c == '/' && buf[p0 + 1u + i + 1u] == '/') {
            return ATN_ERR_PARAM;
        }
        if (i + 1u < path_n && c == '.' && buf[p0 + 1u + i + 1u] == '.') {
            return ATN_ERR_PARAM;
        }
    }
    memcpy(out->path, buf + p0 + 1u, path_n);
    out->path[path_n] = 0;

    if (ver_n != 8u || memcmp(buf + p2 + 1u, "HTTP/1.1", 8) != 0) {
        return ATN_ERR_PARAM;
    }

    pos = line_end + 2u;
    while (pos + 1u < hdr_end) {
        size_t he, colon, vs, ve, hn;
        if (buf[pos] == '\r' && buf[pos + 1u] == '\n') {
            break;
        }
        he = pos;
        while (he + 1u < hdr_end && !(buf[he] == '\r' && buf[he + 1u] == '\n')) {
            if (buf[he] == '\n' || buf[he] == 0) {
                return ATN_ERR_PARAM;
            }
            he++;
        }
        if (he + 1u >= hdr_end) {
            return ATN_ERR_PARAM;
        }
        colon = pos;
        while (colon < he && buf[colon] != ':') {
            colon++;
        }
        if (colon == pos || colon >= he) {
            return ATN_ERR_PARAM;
        }
        hn = colon - pos;
        vs = colon + 1u;
        while (vs < he && (buf[vs] == ' ' || buf[vs] == '\t')) {
            vs++;
        }
        ve = he;
        while (ve > vs && (buf[ve - 1u] == ' ' || buf[ve - 1u] == '\t')) {
            ve--;
        }
        if (hdr_name_eq(buf + pos, hn, "host")) {
            size_t hl = ve - vs;
            if (have_host || hl == 0 || hl >= ATN_HTTP_HOST_MAX) {
                return ATN_ERR_PARAM;
            }
            memcpy(out->host, buf + vs, hl);
            out->host[hl] = 0;
            have_host = 1;
        } else if (hdr_name_eq(buf + pos, hn, "transfer-encoding")) {
            have_te = 1;
        } else if (hdr_name_eq(buf + pos, hn, "content-length")) {
            size_t k;
            clen = 0;
            have_cl = 1;
            if (ve == vs) {
                return ATN_ERR_PARAM;
            }
            for (k = vs; k < ve; k++) {
                if (buf[k] < '0' || buf[k] > '9') {
                    return ATN_ERR_PARAM;
                }
                clen = clen * 10u + (unsigned long)(buf[k] - '0');
                if (clen > 0u) {
                    return ATN_ERR_PARAM; /* GET/HEAD body forbidden */
                }
            }
        }
        pos = he + 2u;
    }
    if (!have_host || have_te || (have_cl && clen != 0u)) {
        return ATN_ERR_PARAM;
    }
    (void)clen;
    return ATN_OK;
}

static size_t build_resp(uint8_t *out, size_t max, int status, const char *reason,
                         const uint8_t *body, size_t body_len, int head_only)
{
    char hdr[256];
    int hn;
    size_t total;
    hn = snprintf(hdr, sizeof(hdr),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: text/html; charset=us-ascii\r\n"
                  "Content-Length: %u\r\n"
                  "Connection: close\r\n"
                  "Cache-Control: no-store\r\n"
                  "\r\n",
                  status, reason, (unsigned)body_len);
    if (hn < 0 || (size_t)hn >= sizeof(hdr)) {
        return 0;
    }
    total = (size_t)hn + (head_only ? 0u : body_len);
    if (total > max) {
        return 0;
    }
    memcpy(out, hdr, (size_t)hn);
    if (!head_only && body_len > 0u) {
        memcpy(out + (size_t)hn, body, body_len);
    }
    return total;
}

int atn_http_listen(atn_http_srv *s, uint16_t port,
                    const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                    const uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    atn_sock ls;
    struct sockaddr_in sa;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    int rc;
    if (s == NULL || ek == NULL || dk == NULL) {
        return ATN_ERR_PARAM;
    }
    memset(s, 0, sizeof(*s));
    s->listen_sock = (intptr_t)ATN_INV;
    rc = atn_net_init();
    if (rc != ATN_OK) {
        return rc;
    }
    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        sock_close(ls);
        return ATN_ERR_SOCK;
    }
    if (listen(ls, ATN_HTTP_BACKLOG) != 0) {
        sock_close(ls);
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    if (getsockname(ls, (struct sockaddr *)&sa, &slen) != 0) {
        sock_close(ls);
        return ATN_ERR_SOCK;
    }
    s->listen_sock = (intptr_t)ls;
    s->port = ntohs(sa.sin_port);
    memcpy(s->ek, ek, ATN_MLKEM1024_EK_LEN);
    memcpy(s->dk, dk, ATN_MLKEM1024_DK_LEN);
    return ATN_OK;
}

uint16_t atn_http_port(const atn_http_srv *s)
{
    return s == NULL ? 0 : s->port;
}

void atn_http_close(atn_http_srv *s)
{
    if (s == NULL) {
        return;
    }
    sock_close(sock_cast(s->listen_sock));
    atn_memzero(s->dk, sizeof(s->dk));
    s->listen_sock = (intptr_t)ATN_INV;
}

static int route_and_respond(atn_sock cs, uint8_t *last, size_t *last_len,
                             const uint8_t k_send[32], uint64_t *send_seq,
                             const uint8_t *req, size_t reqn)
{
    atn_http_req r;
    const uint8_t *body = (const uint8_t *)"";
    size_t blen = 0;
    int status = 200;
    const char *reason = "OK";
    uint8_t resp[ATN_HTTP_MAX_PT];
    size_t rn;
    int rc, head_only;
    int pr = atn_http_parse_request(req, reqn, &r);

    if (pr == ATN_ERR_LEN) {
        status = 431;
        reason = "Request Header Fields Too Large";
        body = (const uint8_t *)"<p>431</p>";
        blen = 10;
        head_only = 0;
    } else if (pr == ATN_ERR_STATE) {
        status = 405;
        reason = "Method Not Allowed";
        body = (const uint8_t *)"<p>405</p>";
        blen = 10;
        head_only = 0;
    } else if (pr != ATN_OK) {
        status = 400;
        reason = "Bad Request";
        body = (const uint8_t *)"<p>400</p>";
        blen = 10;
        head_only = 0;
    } else {
        head_only = (r.method == ATN_HTTP_M_HEAD);
        if (strcmp(r.path, "/") == 0) {
            body = PAGE_ROOT;
            blen = PAGE_ROOT_LEN;
        } else if (strcmp(r.path, "/admin") == 0) {
            body = PAGE_ADMIN;
            blen = PAGE_ADMIN_LEN;
        } else {
            status = 404;
            reason = "Not Found";
            body = (const uint8_t *)"<p>404</p>";
            blen = 10;
        }
    }
    rn = build_resp(resp, sizeof(resp), status, reason, body, blen, head_only);
    if (rn == 0) {
        return ATN_ERR_LEN;
    }
    rc = aead_send(cs, last, last_len, k_send, 2u, *send_seq, ATN_TUN_DATA, resp, rn);
    if (rc == ATN_OK) {
        (*send_seq)++;
    }
    atn_memzero(resp, sizeof(resp));
    return rc;
}

int atn_http_serve_one(atn_http_srv *s, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    atn_sock ls, cs;
    uint8_t dg[16u + ATN_HTTP_MAX_PT + 16u];
    uint8_t pt[ATN_HTTP_MAX_PT];
    size_t n = 0, ptn = 0;
    uint8_t type;
    uint32_t len;
    uint64_t seq;
    uint8_t k_ack[32], k_i2r[32], k_r2i[32], confirm[32], ss[32];
    uint8_t nonce[12];
    uint8_t hdr[16];
    uint64_t send_seq = 1;
    uint64_t recv_max = 0, recv_win = 0;
    int r, rc;

    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    ls = sock_cast(s->listen_sock);
    if (ls == ATN_INV) {
        return ATN_ERR_STATE;
    }
    FD_ZERO(&rfds);
    FD_SET(ls, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
    r = select((int)ls + 1, &rfds, NULL, NULL, &tv);
    if (r == 0) {
        s->last_rc = ATN_ERR_STATE;
        return ATN_ERR_STATE;
    }
    if (r < 0) {
        s->last_rc = ATN_ERR_SOCK;
        return ATN_ERR_SOCK;
    }
    cs = accept(ls, NULL, NULL);
    if (cs == ATN_INV) {
        s->last_rc = ATN_ERR_SOCK;
        return ATN_ERR_SOCK;
    }

    rc = rec_recv(cs, dg, &n, sizeof(dg), timeout_ms);
    if (rc != ATN_OK) {
        /* First bytes were not a framed record (e.g. raw HTTP). */
        sock_close(cs);
        s->last_rc = ATN_ERR_STATE;
        return ATN_ERR_STATE;
    }
    rc = hdr_parse(dg, &type, &len, &seq);
    if (rc != ATN_OK || type != ATN_TUN_HS_INIT || len != ATN_MLKEM1024_CT_LEN) {
        /* Unauthenticated: close with no page bytes. */
        sock_close(cs);
        s->last_rc = ATN_ERR_STATE;
        return ATN_ERR_STATE;
    }
    memcpy(s->last_wire, dg, n);
    s->last_wire_len = n;

    rc = atn_mlkem1024_decaps(s->dk, dg + 16, ss);
    if (rc != ATN_OK) {
        sock_close(cs);
        s->last_rc = rc;
        return rc;
    }
    rc = derive_keys(k_ack, k_i2r, k_r2i, confirm, ss, s->ek, dg + 16);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        sock_close(cs);
        s->last_rc = rc;
        return rc;
    }

    hdr_write(hdr, ATN_TUN_HS_ACK, 48, 0);
    atn_nonce_format(nonce, 0, 0);
    rc = atn_aead_encrypt(k_ack, nonce, hdr, 16, confirm, 32, dg + 16, dg + 48);
    if (rc != ATN_OK) {
        sock_close(cs);
        s->last_rc = rc;
        return rc;
    }
    memcpy(dg, hdr, 16);
    rc = tcp_write_n(cs, dg, 16u + 48u);
    if (rc != ATN_OK) {
        sock_close(cs);
        s->last_rc = rc;
        return rc;
    }

    rc = rec_recv(cs, dg, &n, sizeof(dg), timeout_ms);
    if (rc != ATN_OK) {
        sock_close(cs);
        s->last_rc = rc;
        return rc;
    }
    rc = hdr_parse(dg, &type, &len, &seq);
    if (rc != ATN_OK || type != ATN_TUN_DATA) {
        sock_close(cs);
        s->last_rc = ATN_ERR_STATE;
        return ATN_ERR_STATE;
    }
    if (!replay_ok(&recv_max, &recv_win, seq)) {
        sock_close(cs);
        s->last_rc = ATN_ERR_NONCE;
        return ATN_ERR_NONCE;
    }
    rc = aead_open(dg, n, len, seq, k_i2r, 1u, pt, &ptn, sizeof(pt));
    if (rc != ATN_OK) {
        sock_close(cs);
        atn_memzero(k_ack, 32);
        atn_memzero(k_i2r, 32);
        atn_memzero(k_r2i, 32);
        s->last_rc = ATN_ERR_AUTH;
        return ATN_ERR_AUTH;
    }
    rc = route_and_respond(cs, s->last_wire, &s->last_wire_len,
                           k_r2i, &send_seq, pt, ptn);
    atn_memzero(pt, sizeof(pt));
    atn_memzero(k_ack, 32);
    atn_memzero(k_i2r, 32);
    atn_memzero(k_r2i, 32);
    atn_memzero(confirm, 32);
    sock_close(cs);
    s->last_rc = rc;
    return rc;
}

int atn_http_cli_open(atn_http_cli *c, uint16_t port,
                      const uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    atn_sock s;
    struct sockaddr_in sa;
    int rc;
    if (c == NULL || ek == NULL || port == 0) {
        return ATN_ERR_PARAM;
    }
    memset(c, 0, sizeof(*c));
    c->sock = (intptr_t)ATN_INV;
    c->initiator = 1;
    c->send_seq = 1;
    memcpy(c->peer_ek, ek, ATN_MLKEM1024_EK_LEN);
    rc = atn_net_init();
    if (rc != ATN_OK) {
        return rc;
    }
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (const struct sockaddr *)&sa, sizeof(sa)) != 0) {
        sock_close(s);
        return ATN_ERR_SOCK;
    }
    c->sock = (intptr_t)s;
    c->state = ATN_TUN_CLOSED;
    return ATN_OK;
}

int atn_http_cli_send_init(atn_http_cli *c)
{
    uint8_t ss[32], pkt[16 + ATN_MLKEM1024_CT_LEN];
    uint8_t k_i2r[32], k_r2i[32];
    int rc;
    if (c == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_mlkem1024_encaps(c->peer_ek, ss, c->kem_ct);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = derive_keys(c->k_ack, k_i2r, k_r2i, c->confirm, ss, c->peer_ek, c->kem_ct);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(c->k_send, k_i2r, 32);
    memcpy(c->k_recv, k_r2i, 32);
    atn_memzero(k_i2r, 32);
    atn_memzero(k_r2i, 32);
    hdr_write(pkt, ATN_TUN_HS_INIT, ATN_MLKEM1024_CT_LEN, 0);
    memcpy(pkt + 16, c->kem_ct, ATN_MLKEM1024_CT_LEN);
    rc = tcp_write_n(sock_cast(c->sock), pkt, sizeof(pkt));
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(c->last_wire, pkt, sizeof(pkt));
    c->last_wire_len = sizeof(pkt);
    c->state = ATN_TUN_HANDSHAKE;
    return ATN_OK;
}

int atn_http_cli_send_http(atn_http_cli *c, const char *method, const char *path)
{
    char req[512];
    int n;
    int rc;
    if (c == NULL || method == NULL || path == NULL) {
        return ATN_ERR_PARAM;
    }
    if (c->state != ATN_TUN_HANDSHAKE && c->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    n = snprintf(req, sizeof(req),
                 "%s %s HTTP/1.1\r\n"
                 "Host: 127.0.0.1\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 method, path);
    if (n < 0 || (size_t)n >= sizeof(req)) {
        return ATN_ERR_LEN;
    }
    rc = aead_send(sock_cast(c->sock), c->last_wire, &c->last_wire_len,
                   c->k_send, 1u, c->send_seq, ATN_TUN_DATA,
                   (const uint8_t *)req, (size_t)n);
    if (rc == ATN_OK) {
        c->send_seq++;
    }
    return rc;
}

int atn_http_cli_finish(atn_http_cli *c, uint8_t *resp, size_t *n, size_t max,
                        int timeout_ms)
{
    uint8_t dg[16u + ATN_HTTP_MAX_PT + 16u];
    uint8_t pt[ATN_HTTP_MAX_PT];
    size_t got = 0, ptn = 0;
    uint8_t type;
    uint32_t len;
    uint64_t seq;
    uint8_t nonce[12];
    int rc;

    if (c == NULL || resp == NULL || n == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = rec_recv(sock_cast(c->sock), dg, &got, sizeof(dg), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = hdr_parse(dg, &type, &len, &seq);
    if (rc != ATN_OK) {
        return rc;
    }
    if (type != ATN_TUN_HS_ACK || len != 48u || seq != 0) {
        return ATN_ERR_STATE;
    }
    atn_nonce_format(nonce, 0, 0);
    rc = atn_aead_decrypt(c->k_ack, nonce, dg, 16, dg + 16, 32, dg + 48, pt);
    if (rc != ATN_OK || !atn_ct_equal(pt, c->confirm, 32)) {
        atn_memzero(pt, 32);
        c->state = ATN_TUN_CLOSED;
        return ATN_ERR_AUTH;
    }
    atn_memzero(pt, 32);
    c->state = ATN_TUN_ESTABLISHED;

    rc = rec_recv(sock_cast(c->sock), dg, &got, sizeof(dg), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = hdr_parse(dg, &type, &len, &seq);
    if (rc != ATN_OK || type != ATN_TUN_DATA) {
        return ATN_ERR_STATE;
    }
    if (!replay_ok(&c->recv_max, &c->recv_win, seq)) {
        return ATN_ERR_NONCE;
    }
    rc = aead_open(dg, got, len, seq, c->k_recv, 2u, pt, &ptn, sizeof(pt));
    if (rc != ATN_OK) {
        c->state = ATN_TUN_CLOSED;
        return ATN_ERR_AUTH;
    }
    if (ptn > max) {
        atn_memzero(pt, sizeof(pt));
        return ATN_ERR_LEN;
    }
    memcpy(resp, pt, ptn);
    *n = ptn;
    atn_memzero(pt, sizeof(pt));
    return ATN_OK;
}

void atn_http_cli_wipe(atn_http_cli *c)
{
    if (c == NULL) {
        return;
    }
    sock_close(sock_cast(c->sock));
    atn_memzero(c->k_ack, 32);
    atn_memzero(c->k_send, 32);
    atn_memzero(c->k_recv, 32);
    atn_memzero(c, sizeof(*c));
    c->sock = (intptr_t)ATN_INV;
}
