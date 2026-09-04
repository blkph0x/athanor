/*
 * Lab node responder (REQ-4.1 / DEC-0021).
 *   atnnode demo
 *   atnnode listen [port]
 *
 * Prints peer_port + peer_ek. Operator fills peer_ipv4. No LAN guess.
 */
#include "atn_cfg.h"
#include "atn_crypto.h"
#include "atn_platform.h"
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

static void print_hex(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
}

static int cmd_demo(void)
{
    atn_tun t;
    atn_cfg c;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    char text[80 + ATN_MLKEM1024_EK_LEN * 2u];
    size_t n, i;
    uint16_t port;

    if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
        return 1;
    }
    {
        static const char hdr[] = "peer_ipv4=127.0.0.1\npeer_port=2402\npeer_ek=";
        memcpy(text, hdr, sizeof(hdr) - 1u);
        n = sizeof(hdr) - 1u;
    }
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        static const char hex[] = "0123456789abcdef";
        text[n++] = hex[ek[i] >> 4];
        text[n++] = hex[ek[i] & 15u];
    }
    if (atn_cfg_parse(text, n, &c) != ATN_OK || !atn_cfg_ready(&c) ||
        c.port != 2402 || c.ipv4_host != 0x7f000001u ||
        memcmp(c.ek, ek, ATN_MLKEM1024_EK_LEN) != 0) {
        fprintf(stderr, "cfg roundtrip failed\n");
        return 1;
    }
    if (atn_tun_init_responder(&t, dk) != ATN_OK) {
        return 1;
    }
    if (atn_tun_bind_any(&t, 0) != ATN_OK) {
        atn_tun_wipe(&t);
        return 1;
    }
    port = t.local_port;
    atn_tun_wipe(&t);
    atn_memzero(dk, sizeof(dk));
    if (port == 0) {
        return 1;
    }
    printf("atnnode demo: cfg + bind_any port=%u OK (DEC-0021)\n",
           (unsigned)port);
    return 0;
}

static int cmd_listen(uint16_t port)
{
    atn_tun t;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t pt[ATN_TUN_MAX_PT];
    int rc;

    if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
        return 1;
    }
    if (atn_tun_init_responder(&t, dk) != ATN_OK) {
        atn_memzero(dk, sizeof(dk));
        return 1;
    }
    atn_memzero(dk, sizeof(dk));
    if (atn_tun_bind_any(&t, port) != ATN_OK) {
        fprintf(stderr, "bind_any failed\n");
        atn_tun_wipe(&t);
        return 1;
    }
    printf("# atn-node.conf (set peer_ipv4 to this machine)\n");
    printf("peer_port=%u\n", (unsigned)t.local_port);
    printf("peer_ek=");
    print_hex(ek, ATN_MLKEM1024_EK_LEN);
    printf("\n");
    fflush(stdout);
    for (;;) {
        rc = atn_tun_pump(&t, 1000);
        if (t.state == ATN_TUN_ESTABLISHED) {
            printf("ESTABLISHED\n");
            fflush(stdout);
            break;
        }
        if (rc != ATN_OK && rc != ATN_ERR_STATE) {
            fprintf(stderr, "pump failed %d\n", rc);
            atn_tun_wipe(&t);
            return 1;
        }
    }
    for (;;) {
        size_t n = 0;
        rc = atn_tun_recv_data(&t, pt, &n, sizeof(pt), 1000);
        if (rc == ATN_OK && n > 0) {
            printf("recv %u\n", (unsigned)n);
            (void)atn_tun_send(&t, pt, n);
            atn_memzero(pt, n);
        } else if (rc != ATN_OK && rc != ATN_ERR_STATE) {
            fprintf(stderr, "recv failed %d\n", rc);
            atn_tun_wipe(&t);
            return 1;
        }
        if (t.state == ATN_TUN_CLOSED) {
            printf("CLOSED\n");
            atn_tun_wipe(&t);
            return 0;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: atnnode demo|listen [port]\n");
        return 1;
    }
    if (strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (strcmp(argv[1], "listen") == 0) {
        unsigned port = 2402;
        if (argc == 3) {
            unsigned i, v = 0;
            if (argv[2][0] == 0) {
                return 1;
            }
            for (i = 0; argv[2][i] != 0; i++) {
                if (argv[2][i] < '0' || argv[2][i] > '9') {
                    return 1;
                }
                v = v * 10u + (unsigned)(argv[2][i] - '0');
                if (v > 65535u) {
                    return 1;
                }
            }
            if (v < 1u) {
                return 1;
            }
            port = v;
        } else if (argc != 2) {
            return 1;
        }
        return cmd_listen((uint16_t)port);
    }
    fprintf(stderr, "usage: atnnode demo|listen [port]\n");
    return 1;
}
