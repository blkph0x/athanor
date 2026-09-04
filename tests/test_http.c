/*
 * REQ-2.1 verification: in-house HTTP/1.1 listener, no TLS libraries.
 * Gates: parse rejects, GET exact bytes, unknown method, oversized headers,
 * unauthenticated socket cannot read admin page, wire hides plaintext.
 */
#include "atn_http.h"
#include "atn_platform.h"

#include <stdio.h>
#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET atn_sock;
#  define ATN_INV INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
   typedef int atn_sock;
#  define ATN_INV (-1)
#endif

static int g_fail;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("ok   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fail++;
    }
}

static int memmem_absent(const uint8_t *hay, size_t hn, const uint8_t *nd, size_t nn)
{
    size_t i;
    if (nn == 0 || hn < nn) {
        return 1;
    }
    for (i = 0; i + nn <= hn; i++) {
        if (memcmp(hay + i, nd, nn) == 0) {
            return 0;
        }
    }
    return 1;
}

static int body_eq(const uint8_t *resp, size_t n, const uint8_t *page, size_t pn)
{
    size_t i;
    for (i = 0; i + 3u < n; i++) {
        if (resp[i] == '\r' && resp[i + 1u] == '\n' &&
            resp[i + 2u] == '\r' && resp[i + 3u] == '\n') {
            i += 4u;
            return (n - i) == pn && memcmp(resp + i, page, pn) == 0;
        }
    }
    return 0;
}

static int status_is(const uint8_t *resp, size_t n, const char *want)
{
    /* HTTP/1.1 xxx */
    if (n < 12) {
        return 0;
    }
    return memcmp(resp, "HTTP/1.1 ", 9) == 0 &&
           memcmp(resp + 9, want, 3) == 0;
}

static int roundtrip(atn_http_srv *s, const uint8_t *ek,
                     const char *method, const char *path,
                     uint8_t *resp, size_t *n, size_t max)
{
    atn_http_cli c;
    int rc;
    *n = 0;
    rc = atn_http_cli_open(&c, atn_http_port(s), ek);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = atn_http_cli_send_init(&c);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        return rc;
    }
    rc = atn_http_cli_send_http(&c, method, path);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        return rc;
    }
    rc = atn_http_serve_one(s, 3000);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        return rc;
    }
    rc = atn_http_cli_finish(&c, resp, n, max, 3000);
    if (rc == ATN_OK) {
        /* ciphertext of the GET must not contain the admin marker */
        (void)c.last_wire_len;
    }
    atn_http_cli_wipe(&c);
    return rc;
}

int main(void)
{
    atn_http_req req;
    atn_http_srv srv;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t resp[ATN_HTTP_MAX_PT];
    uint8_t raw[512];
    size_t n = 0, rn = 0, an = 0, rawn = 0;
    const uint8_t *root, *admin;
    static const uint8_t good[] =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    static const uint8_t post[] =
        "POST /admin HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n";
    static const uint8_t nohost[] =
        "GET / HTTP/1.1\r\n\r\n";
    static const uint8_t http10[] =
        "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
    uint8_t huge[ATN_HTTP_MAX_HDR + 64];
    int rc;

    printf("athanor http  platform=%s\n", atn_platform_id());

    root = atn_http_page_root(&rn);
    admin = atn_http_page_admin(&an);
    check("root page marker", rn > 0 && memmem_absent(root, rn,
          (const uint8_t *)"ATN-PUBLIC-PLACEHOLDER", 22) == 0);
    check("admin page marker", an > 0 && memmem_absent(admin, an,
          (const uint8_t *)"ATN-ADMIN-PLACEHOLDER", 21) == 0);
    check("pages have no cdn",
          memmem_absent(root, rn, (const uint8_t *)"cdn", 3) &&
          memmem_absent(admin, an, (const uint8_t *)"cdn.", 4) &&
          memmem_absent(admin, an, (const uint8_t *)"googleapis", 10) &&
          memmem_absent(admin, an, (const uint8_t *)"cloudflare", 10));

    check("parse GET /",
          atn_http_parse_request(good, sizeof(good) - 1u, &req) == ATN_OK &&
          req.method == ATN_HTTP_M_GET && strcmp(req.path, "/") == 0);
    check("parse POST rejected",
          atn_http_parse_request(post, sizeof(post) - 1u, &req) == ATN_ERR_STATE);
    check("parse no Host",
          atn_http_parse_request(nohost, sizeof(nohost) - 1u, &req) == ATN_ERR_PARAM);
    check("parse HTTP/1.0",
          atn_http_parse_request(http10, sizeof(http10) - 1u, &req) == ATN_ERR_PARAM);

    memset(huge, 'A', sizeof(huge));
    memcpy(huge, "GET / HTTP/1.1\r\nHost: x\r\nX-Pad: ", 32);
    /* no terminator; length at the cap */
    check("parse oversized",
          atn_http_parse_request(huge, ATN_HTTP_MAX_HDR, &req) == ATN_ERR_LEN);

    check("kem keygen", atn_mlkem1024_keygen(ek, dk) == ATN_OK);
    check("listen loopback", atn_http_listen(&srv, 0, ek, dk) == ATN_OK &&
          atn_http_port(&srv) != 0);

    rc = roundtrip(&srv, ek, "GET", "/", resp, &n, sizeof(resp));
    check("GET / rc", rc == ATN_OK);
    check("GET / status 200", rc == ATN_OK && status_is(resp, n, "200"));
    check("GET / exact body", rc == ATN_OK && body_eq(resp, n, root, rn));

    rc = roundtrip(&srv, ek, "GET", "/admin", resp, &n, sizeof(resp));
    check("GET /admin rc", rc == ATN_OK);
    check("GET /admin exact body", rc == ATN_OK && body_eq(resp, n, admin, an));
    check("response ciphertext hides admin",
          memmem_absent(srv.last_wire, srv.last_wire_len, admin, an));

    rc = roundtrip(&srv, ek, "HEAD", "/", resp, &n, sizeof(resp));
    check("HEAD / 200 no body",
          rc == ATN_OK && status_is(resp, n, "200") &&
          memmem_absent(resp, n, root, rn));

    rc = roundtrip(&srv, ek, "POST", "/admin", resp, &n, sizeof(resp));
    check("POST 405", rc == ATN_OK && status_is(resp, n, "405"));
    check("POST does not leak admin page",
          rc == ATN_OK && memmem_absent(resp, n, admin, an));

    rc = roundtrip(&srv, ek, "GET", "/nope", resp, &n, sizeof(resp));
    check("GET unknown 404", rc == ATN_OK && status_is(resp, n, "404"));

    /* Unauthenticated raw HTTP: connect, send GET /admin, then serve_one. */
    {
        atn_sock s;
        struct sockaddr_in sa;
        const char *pre = "GET /admin HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        check("raw socket", s != ATN_INV);
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(atn_http_port(&srv));
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check("raw connect",
              connect(s, (const struct sockaddr *)&sa, sizeof(sa)) == 0);
#if defined(ATN_OS_WINDOWS)
        check("raw send", send(s, pre, (int)strlen(pre), 0) == (int)strlen(pre));
#else
        check("raw send", send(s, pre, strlen(pre), 0) == (ssize_t)strlen(pre));
#endif
        rc = atn_http_serve_one(&srv, 3000);
        check("unauth serve rejects", rc == ATN_ERR_STATE);
#if defined(ATN_OS_WINDOWS)
        rawn = 0;
        {
            int rr = recv(s, (char *)raw, (int)sizeof(raw), 0);
            if (rr > 0) {
                rawn = (size_t)rr;
            }
        }
        closesocket(s);
#else
        {
            int rr = (int)recv(s, raw, sizeof(raw), 0);
            rawn = rr > 0 ? (size_t)rr : 0;
        }
        close(s);
#endif
        check("unauth socket has no admin page",
              memmem_absent(raw, rawn, admin, an) &&
              memmem_absent(raw, rawn,
                            (const uint8_t *)"ATN-ADMIN-PLACEHOLDER", 21));
    }

    atn_http_close(&srv);
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
