/*
 * Module: atn_cfg.c
 * REQ:    REQ-4.1
 * Spec:   DEC-0021. peer_ipv4 / peer_port / peer_ek only.
 */

#include "atn_cfg.h"

#include <stdio.h>
#include <string.h>

void atn_cfg_init(atn_cfg *c)
{
    if (c != NULL) {
        memset(c, 0, sizeof(*c));
    }
}

static int hexnib(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int parse_ipv4(const char *s, size_t n, uint32_t *out)
{
    unsigned oct[4], k = 0, v = 0, i, saw = 0;
    if (s == NULL || n == 0 || out == NULL) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            v = v * 10u + (unsigned)(s[i] - '0');
            if (v > 255u) {
                return ATN_ERR_PARAM;
            }
            saw = 1;
        } else if (s[i] == '.') {
            if (!saw || k >= 4u) {
                return ATN_ERR_PARAM;
            }
            oct[k++] = v;
            v = 0;
            saw = 0;
        } else {
            return ATN_ERR_PARAM;
        }
    }
    if (!saw || k != 3u) {
        return ATN_ERR_PARAM;
    }
    oct[3] = v;
    *out = (oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3];
    return ATN_OK;
}

static int parse_port(const char *s, size_t n, uint16_t *out)
{
    unsigned i, v = 0;
    if (s == NULL || n == 0 || n > 5u) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return ATN_ERR_PARAM;
        }
        v = v * 10u + (unsigned)(s[i] - '0');
        if (v > 65535u) {
            return ATN_ERR_PARAM;
        }
    }
    if (v < 1u) {
        return ATN_ERR_PARAM;
    }
    *out = (uint16_t)v;
    return ATN_OK;
}

static int parse_ek(const char *s, size_t n, uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    size_t i;
    if (n != (size_t)ATN_MLKEM1024_EK_LEN * 2u) {
        return ATN_ERR_LEN;
    }
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        int a = hexnib(s[2u * i]);
        int b = hexnib(s[2u * i + 1u]);
        if (a < 0 || b < 0) {
            return ATN_ERR_PARAM;
        }
        ek[i] = (uint8_t)((a << 4) | b);
    }
    return ATN_OK;
}

int atn_cfg_parse(const char *text, size_t n, atn_cfg *c)
{
    size_t i, line0;
    if (text == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_cfg_init(c);
    i = 0;
    while (i < n) {
        const char *eq;
        size_t ls, klen, vlen;
        line0 = i;
        while (i < n && text[i] != '\n') {
            i++;
        }
        ls = i - line0;
        if (i < n && text[i] == '\n') {
            i++;
        }
        if (ls > 0 && text[line0 + ls - 1u] == '\r') {
            ls--;
        }
        if (ls == 0 || text[line0] == '#') {
            continue;
        }
        eq = NULL;
        {
            size_t j;
            for (j = 0; j < ls; j++) {
                if (text[line0 + j] == '=') {
                    eq = text + line0 + j;
                    break;
                }
            }
        }
        if (eq == NULL) {
            return ATN_ERR_PARAM;
        }
        klen = (size_t)(eq - (text + line0));
        vlen = ls - klen - 1u;
        if (klen == 9 && memcmp(text + line0, "peer_ipv4", 9) == 0) {
            if (parse_ipv4(eq + 1, vlen, &c->ipv4_host) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            c->have_ipv4 = 1;
        } else if (klen == 9 && memcmp(text + line0, "peer_port", 9) == 0) {
            if (parse_port(eq + 1, vlen, &c->port) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            c->have_port = 1;
        } else if (klen == 7 && memcmp(text + line0, "peer_ek", 7) == 0) {
            if (parse_ek(eq + 1, vlen, c->ek) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            c->have_ek = 1;
        } else {
            return ATN_ERR_PARAM;
        }
    }
    return ATN_OK;
}

int atn_cfg_load_file(const char *path, atn_cfg *c)
{
    FILE *f;
    char buf[8192];
    size_t n;
    int rc;

    if (path == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_ERR_PARAM;
    }
    n = fread(buf, 1, sizeof(buf), f);
    if (ferror(f) || n == sizeof(buf)) {
        fclose(f);
        return ATN_ERR_LEN;
    }
    fclose(f);
    rc = atn_cfg_parse(buf, n, c);
    atn_memzero(buf, n);
    return rc;
}

int atn_cfg_ready(const atn_cfg *c)
{
    if (c == NULL) {
        return 0;
    }
    return c->have_ipv4 && c->have_port && c->have_ek;
}
