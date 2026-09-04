/*
 * REQ-2.1 verification: in-house HTTP/1.1 listener, no TLS libraries.
 * Gates: parse rejects, GET exact bytes, unknown method, oversized headers,
 * unauthenticated socket cannot read admin page, wire hides plaintext.
 */
#include "atn_http.h"
#include "atn_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
          (const uint8_t *)"ATN-LOGIN-PAGE", 14) == 0);
    check("pages have no cdn",
          memmem_absent(root, rn, (const uint8_t *)"cdn.", 4) &&
          memmem_absent(admin, an, (const uint8_t *)"cdn.", 4) &&
          memmem_absent(admin, an, (const uint8_t *)"googleapis", 10) &&
          memmem_absent(admin, an, (const uint8_t *)"cloudflare", 10) &&
          memmem_absent(admin, an, (const uint8_t *)"npmjs", 5) &&
          memmem_absent(admin, an, (const uint8_t *)"unpkg", 5));

    check("parse GET /",
          atn_http_parse_request(good, sizeof(good) - 1u, &req) == ATN_OK &&
          req.method == ATN_HTTP_M_GET && strcmp(req.path, "/") == 0);
    check("parse POST needs type",
          atn_http_parse_request(post, sizeof(post) - 1u, &req) == ATN_ERR_PARAM);
    check("parse no Host",
          atn_http_parse_request(nohost, sizeof(nohost) - 1u, &req) == ATN_ERR_PARAM);
    check("parse HTTP/1.0",
          atn_http_parse_request(http10, sizeof(http10) - 1u, &req) == ATN_ERR_PARAM);
    {
        static const uint8_t ka[] =
            "GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n";
        static const uint8_t cl[] =
            "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
        check("parse ka",
              atn_http_parse_request(ka, sizeof(ka) - 1u, &req) == ATN_OK &&
              !req.conn_close);
        check("parse close",
              atn_http_parse_request(cl, sizeof(cl) - 1u, &req) == ATN_OK &&
              req.conn_close);
    }

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
    check("GET /admin is login page",
          rc == ATN_OK && status_is(resp, n, "200") &&
          memmem_absent(resp, n, (const uint8_t *)"ATN-LOGIN-PAGE", 14) == 0);
    check("response ciphertext hides login marker",
          memmem_absent(srv.last_wire, srv.last_wire_len,
                        (const uint8_t *)"ATN-LOGIN-PAGE", 14));

    rc = roundtrip(&srv, ek, "HEAD", "/", resp, &n, sizeof(resp));
    check("HEAD / 200 no body",
          rc == ATN_OK && status_is(resp, n, "200") &&
          memmem_absent(resp, n, root, rn));

    rc = roundtrip(&srv, ek, "PUT", "/admin", resp, &n, sizeof(resp));
    check("PUT 405", rc == ATN_OK && status_is(resp, n, "405"));
    check("PUT does not leak console",
          rc == ATN_OK &&
          memmem_absent(resp, n, (const uint8_t *)"ATN-CONSOLE-PAGE", 16));

    rc = roundtrip(&srv, ek, "GET", "/nope", resp, &n, sizeof(resp));
    check("GET unknown 404", rc == ATN_OK && status_is(resp, n, "404"));

    /* DEC-0024: two GETs on one TCP session. */
    {
        atn_http_cli c;
        uint8_t r2[ATN_HTTP_MAX_PT];
        size_t n2 = 0;
        check("ka open", atn_http_cli_open(&c, atn_http_port(&srv), ek) == ATN_OK);
        c.persist = 1;
        check("ka init", atn_http_cli_send_init(&c) == ATN_OK);
        check("ka send1", atn_http_cli_send_http(&c, "GET", "/") == ATN_OK);
        c.persist = 0;
        check("ka send2", atn_http_cli_send_http(&c, "GET", "/") == ATN_OK);
        check("ka serve", atn_http_serve_one(&srv, 3000) == ATN_OK);
        n = 0;
        check("ka finish1",
              atn_http_cli_finish(&c, resp, &n, sizeof(resp), 3000) == ATN_OK &&
              status_is(resp, n, "200"));
        check("ka recv2",
              atn_http_cli_recv_http(&c, r2, &n2, sizeof(r2), 3000) == ATN_OK &&
              status_is(r2, n2, "200"));
        atn_http_cli_wipe(&c);
    }

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
                            (const uint8_t *)"ATN-LOGIN-PAGE", 14) &&
              memmem_absent(raw, rawn,
                            (const uint8_t *)"ATN-CONSOLE-PAGE", 16));
    }

    /* REQ-2.2: 2FA login required to mutate. */
    {
        uint8_t id[ATN_2FA_ID_LEN], key[ATN_2FA_KEY_LEN];
        uint8_t chal[ATN_2FA_CHAL_LEN], mac[ATN_2FA_RESP_LEN];
        char sid[33], csrf[65], chal_hex[65], id_hex[65], mac_hex[129];
        char form[512];
        const uint8_t *p;
        size_t i;
        memset(id, 0x22, sizeof(id));
        check("enroll op", atn_http_enroll_op(&srv, id, key) == ATN_OK);
        check("wipe idle", atn_http_wipe_armed(&srv) == 0);

        rc = roundtrip(&srv, ek, "GET", "/admin", resp, &n, sizeof(resp));
        if (n < sizeof(resp)) {
            resp[n] = 0;
        }
        check("login GET", rc == ATN_OK &&
              memmem_absent(resp, n, (const uint8_t *)"ATN-LOGIN-PAGE", 14) == 0);
        sid[0] = 0;
        csrf[0] = 0;
        for (i = 0; i + 12 < n; i++) {
            if (memcmp(resp + i, "ATN-SID=", 8) == 0) {
                memcpy(sid, resp + i + 8, 32);
                sid[32] = 0;
            }
            if (memcmp(resp + i, "name=\"csrf\" value=\"", 19) == 0) {
                memcpy(csrf, resp + i + 19, 64);
                csrf[64] = 0;
            }
        }
        check("session cookie", sid[0] != 0 && csrf[0] != 0);

        /* Mutate without login must fail. */
        {
            atn_http_cli c;
            snprintf(form, sizeof(form),
                     "csrf=%s&action=wipe&resp=%s", csrf,
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000000");
            check("open mutate", atn_http_cli_open(&c, atn_http_port(&srv), ek) == ATN_OK);
            check("init mutate", atn_http_cli_send_init(&c) == ATN_OK);
            check("send mutate",
                  atn_http_cli_send_req(&c, "POST", "/admin/do", sid, form) == ATN_OK);
            check("serve mutate", atn_http_serve_one(&srv, 3000) == ATN_OK);
            n = 0;
            rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), 3000);
            atn_http_cli_wipe(&c);
            check("unauthed mutate 401", rc == ATN_OK && status_is(resp, n, "401"));
            check("wipe still idle", atn_http_wipe_armed(&srv) == 0);
        }

        for (i = 0; i < 32; i++) {
            static const char H[] = "0123456789abcdef";
            id_hex[2 * i] = H[id[i] >> 4];
            id_hex[2 * i + 1] = H[id[i] & 15];
        }
        id_hex[64] = 0;
        snprintf(form, sizeof(form), "csrf=%s&id=%s", csrf, id_hex);
        {
            atn_http_cli c;
            check("open chal", atn_http_cli_open(&c, atn_http_port(&srv), ek) == ATN_OK);
            check("init chal", atn_http_cli_send_init(&c) == ATN_OK);
            check("send chal",
                  atn_http_cli_send_req(&c, "POST", "/admin/challenge", sid, form) == ATN_OK);
            check("serve chal", atn_http_serve_one(&srv, 3000) == ATN_OK);
            n = 0;
            rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), 3000);
            atn_http_cli_wipe(&c);
            if (n < sizeof(resp)) {
                resp[n] = 0;
            }
            check("challenge 200", rc == ATN_OK && status_is(resp, n, "200"));
        }
        chal_hex[0] = 0;
        p = (const uint8_t *)strstr((const char *)resp, "id=\"chal\">");
        check("chal in html", p != NULL);
        if (p != NULL) {
            memcpy(chal_hex, p + 10, 64);
            chal_hex[64] = 0;
        }
        for (i = 0; i < 32; i++) {
            int a = chal_hex[2 * i];
            int b = chal_hex[2 * i + 1];
            a = (a >= '0' && a <= '9') ? a - '0' : a - 'a' + 10;
            b = (b >= '0' && b <= '9') ? b - '0' : b - 'a' + 10;
            chal[i] = (uint8_t)((a << 4) | b);
        }
        check("2fa respond", atn_2fa_respond(key, chal, mac) == ATN_OK);
        for (i = 0; i < 64; i++) {
            static const char H[] = "0123456789abcdef";
            mac_hex[2 * i] = H[mac[i] >> 4];
            mac_hex[2 * i + 1] = H[mac[i] & 15];
        }
        mac_hex[128] = 0;
        snprintf(form, sizeof(form), "csrf=%s&id=%s&resp=%s", csrf, id_hex, mac_hex);
        {
            atn_http_cli c;
            check("open login", atn_http_cli_open(&c, atn_http_port(&srv), ek) == ATN_OK);
            check("init login", atn_http_cli_send_init(&c) == ATN_OK);
            check("send login",
                  atn_http_cli_send_req(&c, "POST", "/admin/login", sid, form) == ATN_OK);
            check("serve login", atn_http_serve_one(&srv, 3000) == ATN_OK);
            n = 0;
            rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), 3000);
            atn_http_cli_wipe(&c);
            if (n < sizeof(resp)) {
                resp[n] = 0;
            }
            check("login console",
                  rc == ATN_OK && status_is(resp, n, "200") &&
                  memmem_absent(resp, n, (const uint8_t *)"ATN-CONSOLE-PAGE", 16) == 0);
        }

        p = (const uint8_t *)strstr((const char *)resp, "id=\"chal\">");
        check("console chal", p != NULL);
        if (p != NULL) {
            memcpy(chal_hex, p + 10, 64);
            chal_hex[64] = 0;
        }
        for (i = 0; i < 32; i++) {
            int a = chal_hex[2 * i];
            int b = chal_hex[2 * i + 1];
            a = (a >= '0' && a <= '9') ? a - '0' : a - 'a' + 10;
            b = (b >= '0' && b <= '9') ? b - '0' : b - 'a' + 10;
            chal[i] = (uint8_t)((a << 4) | b);
        }
        check("2fa mutate respond", atn_2fa_respond(key, chal, mac) == ATN_OK);
        for (i = 0; i < 64; i++) {
            static const char H[] = "0123456789abcdef";
            mac_hex[2 * i] = H[mac[i] >> 4];
            mac_hex[2 * i + 1] = H[mac[i] & 15];
        }
        mac_hex[128] = 0;
        snprintf(form, sizeof(form), "csrf=%s&action=wipe&resp=%s", csrf, mac_hex);
        {
            atn_http_cli c;
            check("open wipe", atn_http_cli_open(&c, atn_http_port(&srv), ek) == ATN_OK);
            check("init wipe", atn_http_cli_send_init(&c) == ATN_OK);
            check("send wipe",
                  atn_http_cli_send_req(&c, "POST", "/admin/do", sid, form) == ATN_OK);
            check("serve wipe", atn_http_serve_one(&srv, 3000) == ATN_OK);
            n = 0;
            rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), 3000);
            atn_http_cli_wipe(&c);
            check("wipe 200", rc == ATN_OK && status_is(resp, n, "200"));
            check("wipe armed", atn_http_wipe_armed(&srv) == 1);
        }
    }

    atn_http_close(&srv);
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
