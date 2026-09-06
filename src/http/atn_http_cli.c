/*
 * Standalone HTTP listener / operator client (REQ-2.1, DEC-0009 / DEC-0026).
 * Usage:
 *   atnhttp demo
 *   atnhttp serve-once [port]
 *   atnhttp get <conf> <path>
 *
 * Spec: docs/HTTP.md. Not RFC 8446 (ISS-0009). Conf keys: DEC-0021.
 */
#include "atn_cfg.h"
#include "atn_http.h"

#include <stdio.h>
#include <stdlib.h>
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

static void print_ek_hex(const uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    size_t i;
    static const char *hx = "0123456789abcdef";
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        putchar(hx[(ek[i] >> 4) & 0xf]);
        putchar(hx[ek[i] & 0xf]);
    }
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

static int write_temp_conf(const char *path, uint16_t port,
                           const uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    FILE *f = fopen(path, "wb");
    size_t i;
    static const char *hx = "0123456789abcdef";
    if (f == NULL) {
        return 1;
    }
    fprintf(f, "peer_ipv4=127.0.0.1\npeer_port=%u\npeer_ek=", (unsigned)port);
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        fputc(hx[(ek[i] >> 4) & 0xf], f);
        fputc(hx[ek[i] & 0xf], f);
    }
    fputc('\n', f);
    fclose(f);
    return 0;
}

static int cmd_demo(void)
{
    atn_http_srv s;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    const uint8_t *root, *admin;
    size_t rn = 0, an = 0;
    int rc;
    atn_cfg cfg;
    const char *conf_path = "atn-http-demo.conf";

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
    /* DEC-0026: conf path the operator client will load. */
    if (write_temp_conf(conf_path, atn_http_port(&s), ek) != 0) {
        atn_http_close(&s);
        fprintf(stderr, "write conf failed\n");
        return 1;
    }
    atn_cfg_init(&cfg);
    if (atn_cfg_load_file(conf_path, &cfg) != ATN_OK || !atn_cfg_ready(&cfg) ||
        cfg.port != atn_http_port(&s) ||
        memcmp(cfg.ek, ek, ATN_MLKEM1024_EK_LEN) != 0 ||
        cfg.ipv4_host != 0x7f000001u) {
        atn_http_close(&s);
        remove(conf_path);
        fprintf(stderr, "conf reload mismatch\n");
        return 1;
    }
    atn_http_close(&s);
    remove(conf_path);
    printf("atnhttp demo: GET / and GET /admin + conf reload OK (DEC-0009/0026)\n");
    return 0;
}

static int cmd_serve_once(uint16_t port)
{
    atn_http_srv s;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    int rc;

    rc = atn_mlkem1024_keygen(ek, dk);
    if (rc != ATN_OK) {
        fprintf(stderr, "keygen %d\n", rc);
        return 1;
    }
    rc = atn_http_listen(&s, port, ek, dk);
    if (rc != ATN_OK) {
        fprintf(stderr, "listen %d\n", rc);
        return 1;
    }
    printf("peer_ipv4=127.0.0.1\n");
    printf("peer_port=%u\n", (unsigned)atn_http_port(&s));
    printf("peer_ek=");
    print_ek_hex(ek);
    printf("\n");
    fflush(stdout);
    rc = atn_http_serve_one(&s, ATN_HTTP_IDLE_MS);
    atn_http_close(&s);
    atn_memzero(dk, sizeof(dk));
    atn_memzero(ek, sizeof(ek));
    if (rc != ATN_OK) {
        fprintf(stderr, "serve-once rc=%d\n", rc);
        return 1;
    }
    return 0;
}

static int cmd_get(const char *conf_path, const char *path)
{
    atn_cfg cfg;
    atn_http_cli c;
    uint8_t resp[ATN_HTTP_MAX_PT];
    size_t n = 0;
    int rc;

    atn_cfg_init(&cfg);
    rc = atn_cfg_load_file(conf_path, &cfg);
    if (rc != ATN_OK || !atn_cfg_ready(&cfg)) {
        fprintf(stderr, "conf load/ready failed rc=%d\n", rc);
        return 1;
    }
    /* DEC-0009 / DEC-0026: listener is loopback-only. */
    if (cfg.ipv4_host != 0x7f000001u) {
        fprintf(stderr, "peer_ipv4 must be 127.0.0.1 for atnhttp get\n");
        return 1;
    }
    if (path == NULL || path[0] != '/') {
        fprintf(stderr, "path must start with /\n");
        return 1;
    }
    rc = atn_http_cli_open(&c, cfg.port, cfg.ek);
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
    rc = atn_http_cli_finish(&c, resp, &n, sizeof(resp), ATN_HTTP_IDLE_MS);
    atn_http_cli_wipe(&c);
    if (rc != ATN_OK) {
        fprintf(stderr, "finish %d\n", rc);
        return 1;
    }
    if (fwrite(resp, 1, n, stdout) != n) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (argc >= 2 && strcmp(argv[1], "serve-once") == 0) {
        uint16_t port = 0;
        if (argc == 3) {
            unsigned long p = strtoul(argv[2], NULL, 10);
            if (p > 65535ul) {
                fprintf(stderr, "bad port\n");
                return 1;
            }
            port = (uint16_t)p;
        } else if (argc != 2) {
            fprintf(stderr, "usage: atnhttp serve-once [port]\n");
            return 1;
        }
        return cmd_serve_once(port);
    }
    if (argc == 4 && strcmp(argv[1], "get") == 0) {
        return cmd_get(argv[2], argv[3]);
    }
    fprintf(stderr,
            "usage: atnhttp demo | serve-once [port] | get <conf> <path>\n");
    return 1;
}
