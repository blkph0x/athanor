/*
 * Standalone HTTP listener binary (REQ-2.1, DEC-0009).
 * Usage:
 *   atnhttp demo
 */
#include "atn_http.h"

#include <stdio.h>
#include <string.h>

static int body_is(const uint8_t *resp, size_t n, const uint8_t *page, size_t pn)
{
    size_t i;
    if (n < pn) {
        return 0;
    }
    for (i = 0; i + 3u < n; i++) {
        if (resp[i] == '\r' && resp[i + 1u] == '\n' &&
            resp[i + 2u] == '\r' && resp[i + 3u] == '\n') {
            i += 4u;
            return (n - i) == pn && memcmp(resp + i, page, pn) == 0;
        }
    }
    return 0;
}

static int roundtrip(atn_http_srv *s, const uint8_t *ek, const char *path,
                     const uint8_t *want, size_t wantn)
{
    atn_http_cli c;
    uint8_t resp[ATN_HTTP_MAX_PT];
    size_t n = 0;
    int rc;
    rc = atn_http_cli_open(&c, atn_http_port(s), ek);
    if (rc != ATN_OK) {
        fprintf(stderr, "open %d\n", rc);
        return 1;
    }
    rc = atn_http_cli_send_init(&c);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        fprintf(stderr, "init %d\n", rc);
        return 1;
    }
    rc = atn_http_cli_send_http(&c, "GET", path);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        fprintf(stderr, "send %d\n", rc);
        return 1;
    }
    rc = atn_http_serve_one(s, ATN_HTTP_IDLE_MS);
    if (rc != ATN_OK) {
        atn_http_cli_wipe(&c);
        fprintf(stderr, "serve %d\n", rc);
        return 1;
    }
    rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), ATN_HTTP_IDLE_MS);
    atn_http_cli_wipe(&c);
    if (rc != ATN_OK || !body_is(resp, n, want, wantn)) {
        fprintf(stderr, "finish rc=%d n=%u\n", rc, (unsigned)n);
        return 1;
    }
    return 0;
}

static int cmd_demo(void)
{
    atn_http_srv s;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    const uint8_t *root, *admin;
    size_t rn = 0, an = 0;
    int rc;

    root = atn_http_page_root(&rn);
    admin = atn_http_page_admin(&an);
    rc = atn_mlkem1024_keygen(ek, dk);
    if (rc != ATN_OK) {
        fprintf(stderr, "keygen %d\n", rc);
        return 1;
    }
    rc = atn_http_listen(&s, 0, ek, dk);
    if (rc != ATN_OK) {
        fprintf(stderr, "listen %d\n", rc);
        return 1;
    }
    if (roundtrip(&s, ek, "/", root, rn) != 0) {
        atn_http_close(&s);
        return 1;
    }
    {
        atn_http_cli c;
        uint8_t resp[ATN_HTTP_MAX_PT];
        size_t n = 0;
        int rc;
        rc = atn_http_cli_open(&c, atn_http_port(&s), ek);
        if (rc != ATN_OK || atn_http_cli_send_init(&c) != ATN_OK ||
            atn_http_cli_send_http(&c, "GET", "/admin") != ATN_OK ||
            atn_http_serve_one(&s, ATN_HTTP_IDLE_MS) != ATN_OK ||
            atn_http_cli_finish(&c, resp, &n, sizeof(resp), ATN_HTTP_IDLE_MS) != ATN_OK) {
            atn_http_cli_wipe(&c);
            atn_http_close(&s);
            fprintf(stderr, "admin GET failed\n");
            return 1;
        }
        atn_http_cli_wipe(&c);
        if (n < 20 || memcmp(resp, "HTTP/1.1 200", 12) != 0) {
            atn_http_close(&s);
            return 1;
        }
        (void)admin;
        (void)an;
    }
    atn_http_close(&s);
    printf("atnhttp demo: GET / and GET /admin login OK (DEC-0009/0010)\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    fprintf(stderr, "usage: atnhttp demo\n");
    return 1;
}
