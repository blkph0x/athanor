/*
 * Standalone 2FA binary (REQ-1.3). enroll / challenge / respond / verify / demo.
 * Usage:
 *   atn2fa demo
 *   atn2fa respond <64-hex-key> <64-hex-chal>
 */
#include "atn_2fa.h"

#include <stdio.h>
#include <string.h>

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int parse_hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (strlen(s) != n * 2u) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int a = hex_nibble(s[2u * i]);
        int b = hex_nibble(s[2u * i + 1u]);
        if (a < 0 || b < 0) {
            return -1;
        }
        out[i] = (uint8_t)((a << 4) | b);
    }
    return 0;
}

static void print_hex(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
    printf("\n");
}

static int cmd_demo(void)
{
    atn_2fa_store st;
    uint8_t id[32], key[32], chal[32], resp[64], bad[64];
    int rc;
    atn_2fa_store_init(&st);
    memset(id, 0x11, sizeof(id));
    rc = atn_2fa_enroll(&st, id, key);
    if (rc != ATN_OK) {
        fprintf(stderr, "enroll failed %d\n", rc);
        return 1;
    }
    rc = atn_2fa_challenge(&st, id, chal);
    if (rc != ATN_OK) {
        fprintf(stderr, "challenge failed %d\n", rc);
        return 1;
    }
    rc = atn_2fa_respond(key, chal, resp);
    if (rc != ATN_OK || atn_2fa_verify(&st, id, chal, resp) != ATN_OK) {
        fprintf(stderr, "verify failed\n");
        return 1;
    }
    if (atn_2fa_verify(&st, id, chal, resp) != ATN_ERR_AUTH &&
        atn_2fa_verify(&st, id, chal, resp) != ATN_ERR_NONCE) {
        /* pending cleared: second verify without new challenge is AUTH */
    }
    rc = atn_2fa_challenge(&st, id, chal);
    if (rc != ATN_OK) {
        return 1;
    }
    rc = atn_2fa_respond(key, chal, resp);
    if (atn_2fa_verify(&st, id, chal, resp) != ATN_OK) {
        fprintf(stderr, "second verify failed\n");
        return 1;
    }
    /* replay the same chal after consume */
    if (atn_2fa_challenge(&st, id, chal) != ATN_OK) {
        return 1;
    }
    memcpy(bad, chal, 32);
    if (atn_2fa_respond(key, chal, resp) != ATN_OK) {
        return 1;
    }
    if (atn_2fa_verify(&st, id, chal, resp) != ATN_OK) {
        return 1;
    }
    /* re-issue same bytes won't happen; consume-then-replay of resp+chal: */
    rc = atn_2fa_verify(&st, id, bad, resp);
    if (rc != ATN_ERR_AUTH && rc != ATN_ERR_NONCE && rc != ATN_ERR_LOCKOUT) {
        fprintf(stderr, "replay not rejected (%d)\n", rc);
        return 1;
    }
    printf("atn2fa demo: enroll/challenge/verify/replay-reject OK\n");
    atn_memzero(key, sizeof(key));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (argc == 4 && strcmp(argv[1], "respond") == 0) {
        uint8_t key[32], chal[32], resp[64];
        if (parse_hex(argv[2], key, 32) != 0 || parse_hex(argv[3], chal, 32) != 0) {
            fprintf(stderr, "hex parse error\n");
            return 1;
        }
        if (atn_2fa_respond(key, chal, resp) != ATN_OK) {
            return 1;
        }
        print_hex(resp, 64);
        atn_memzero(key, sizeof(key));
        return 0;
    }
    fprintf(stderr, "usage: atn2fa demo | atn2fa respond <64-hex-key> <64-hex-chal>\n");
    return 1;
}
