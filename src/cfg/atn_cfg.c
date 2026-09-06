/*
 * Module: atn_cfg.c
 * REQ:    REQ-4.1
 * Spec:   DEC-0021 / 0027 / 0028 / 0029.
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

static int parse_01(const char *s, size_t n, uint8_t *out)
{
    if (n != 1u || (s[0] != '0' && s[0] != '1')) {
        return ATN_ERR_PARAM;
    }
    *out = (uint8_t)(s[0] - '0');
    return ATN_OK;
}

static int hub_index_from_key(const char *k, size_t klen, unsigned *idx,
                              char *field, size_t field_max)
{
    /* hub2_ipv4 … hub16_ek (DEC-0032). Decimal after "hub", then '_'. */
    unsigned num = 0, di = 3, fi = 0;
    if (klen < 7u || memcmp(k, "hub", 3) != 0) {
        return ATN_ERR_PARAM;
    }
    if (k[3] < '0' || k[3] > '9') {
        return ATN_ERR_PARAM;
    }
    while (di < klen && k[di] >= '0' && k[di] <= '9') {
        num = num * 10u + (unsigned)(k[di] - '0');
        if (num > ATN_CFG_MAX_HUBS) {
            return ATN_ERR_PARAM;
        }
        di++;
    }
    if (di >= klen || k[di] != '_' || num < 2u || num > ATN_CFG_MAX_HUBS) {
        return ATN_ERR_PARAM;
    }
    *idx = num - 1u; /* hub2 → 1, hub16 → 15 */
    di++; /* skip '_' */
    klen -= di;
    k += di;
    if (klen + 1u > field_max) {
        return ATN_ERR_PARAM;
    }
    while (fi < klen) {
        field[fi] = k[fi];
        fi++;
    }
    field[fi] = 0;
    return ATN_OK;
}

int atn_cfg_parse(const char *text, size_t n, atn_cfg *c)
{
    size_t i, line0;
    unsigned h;
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
        } else if (klen == 10 && memcmp(text + line0, "witness_id", 10) == 0) {
            if (vlen != 16u) {
                return ATN_ERR_LEN;
            }
            {
                size_t wi;
                for (wi = 0; wi < 8u; wi++) {
                    int a = hexnib(eq[1 + 2u * wi]);
                    int b = hexnib(eq[1 + 2u * wi + 1u]);
                    if (a < 0 || b < 0) {
                        return ATN_ERR_PARAM;
                    }
                    c->witness[wi] = (uint8_t)((a << 4) | b);
                }
            }
            c->have_witness = 1;
        } else if (klen == 4 && memcmp(text + line0, "diag", 4) == 0) {
            if (parse_01(eq + 1, vlen, &c->diag) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            c->have_diag = 1;
        } else if (klen == 10 && memcmp(text + line0, "flush_mode", 10) == 0) {
            if (vlen == 7 && memcmp(eq + 1, "zeroize", 7) == 0) {
                c->flush_mode = ATN_CFG_FLUSH_ZEROIZE;
            } else if (vlen == 8 && memcmp(eq + 1, "log_only", 8) == 0) {
                c->flush_mode = ATN_CFG_FLUSH_LOG_ONLY;
            } else {
                return ATN_ERR_PARAM;
            }
            c->have_flush_mode = 1;
        } else if (klen == 10 && memcmp(text + line0, "wipe_armed", 10) == 0) {
            if (parse_01(eq + 1, vlen, &c->wipe_armed) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            c->have_wipe_armed = 1;
        } else if (klen == 12 && memcmp(text + line0, "outage_class", 12) == 0) {
            if (vlen == 6 && memcmp(eq + 1, "normal", 6) == 0) {
                c->outage_class = ATN_CFG_OUTAGE_NORMAL;
            } else if (vlen == 11 && memcmp(eq + 1, "maintenance", 11) == 0) {
                c->outage_class = ATN_CFG_OUTAGE_MAINTENANCE;
            } else if (vlen == 8 && memcmp(eq + 1, "blackout", 8) == 0) {
                c->outage_class = ATN_CFG_OUTAGE_BLACKOUT;
            } else if (vlen == 7 && memcmp(eq + 1, "faraday", 7) == 0) {
                c->outage_class = ATN_CFG_OUTAGE_FARADAY;
            } else if (vlen == 7 && memcmp(eq + 1, "capture", 7) == 0) {
                c->outage_class = ATN_CFG_OUTAGE_CAPTURE;
            } else {
                return ATN_ERR_PARAM;
            }
            c->have_outage = 1;
        } else {
            unsigned idx = 0;
            char field[16];
            if (hub_index_from_key(text + line0, klen, &idx, field,
                                   sizeof(field)) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
            if (strcmp(field, "ipv4") == 0) {
                if (parse_ipv4(eq + 1, vlen, &c->hub[idx].ipv4_host) != ATN_OK) {
                    return ATN_ERR_PARAM;
                }
                c->hub[idx].have_ipv4 = 1;
            } else if (strcmp(field, "port") == 0) {
                if (parse_port(eq + 1, vlen, &c->hub[idx].port) != ATN_OK) {
                    return ATN_ERR_PARAM;
                }
                c->hub[idx].have_port = 1;
            } else if (strcmp(field, "ek") == 0) {
                if (parse_ek(eq + 1, vlen, c->hub[idx].ek) != ATN_OK) {
                    return ATN_ERR_PARAM;
                }
                c->hub[idx].have_ek = 1;
            } else {
                return ATN_ERR_PARAM;
            }
        }
    }
    /* Defaults (DEC-0027). */
    if (!c->have_diag) {
        c->diag = 0;
    }
    if (!c->have_wipe_armed) {
        c->wipe_armed = 0;
    }
    if (!c->have_flush_mode) {
        c->flush_mode = c->diag ? ATN_CFG_FLUSH_LOG_ONLY : ATN_CFG_FLUSH_ZEROIZE;
    }
    if (!c->have_outage) {
        c->outage_class = ATN_CFG_OUTAGE_NORMAL;
    }
    if (c->flush_mode == ATN_CFG_FLUSH_LOG_ONLY && !c->diag) {
        return ATN_ERR_PARAM;
    }
    /* Mirror primary into hub[0]; reject partial optional hubs. */
    c->hub[0].ipv4_host = c->ipv4_host;
    c->hub[0].port = c->port;
    if (c->have_ek) {
        memcpy(c->hub[0].ek, c->ek, ATN_MLKEM1024_EK_LEN);
    }
    c->hub[0].have_ipv4 = c->have_ipv4;
    c->hub[0].have_port = c->have_port;
    c->hub[0].have_ek = c->have_ek;
    for (h = 1; h < ATN_CFG_MAX_HUBS; h++) {
        unsigned bits = (unsigned)c->hub[h].have_ipv4 + c->hub[h].have_port +
                        c->hub[h].have_ek;
        if (bits != 0u && bits != 3u) {
            return ATN_ERR_PARAM;
        }
    }
    {
        int gap = 0;
        for (h = 1; h < ATN_CFG_MAX_HUBS; h++) {
            int full = c->hub[h].have_ipv4 && c->hub[h].have_port &&
                       c->hub[h].have_ek;
            if (!full) {
                gap = 1;
            } else if (gap) {
                return ATN_ERR_PARAM; /* hubN without hub(N-1) */
            }
        }
    }
    return ATN_OK;
}

int atn_cfg_load_file(const char *path, atn_cfg *c)
{
    FILE *f;
    char buf[65536];
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

unsigned atn_cfg_hub_count(const atn_cfg *c)
{
    unsigned n = 0, h;
    if (c == NULL || !atn_cfg_ready(c)) {
        return 0;
    }
    n = 1;
    for (h = 1; h < ATN_CFG_MAX_HUBS; h++) {
        if (c->hub[h].have_ipv4 && c->hub[h].have_port && c->hub[h].have_ek) {
            n++;
        } else {
            break; /* contiguous from 2 upward */
        }
    }
    return n;
}

int atn_cfg_hub_get(const atn_cfg *c, unsigned i, uint32_t *ipv4_host,
                    uint16_t *port, uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    if (c == NULL || ipv4_host == NULL || port == NULL || ek == NULL) {
        return ATN_ERR_PARAM;
    }
    if (i >= atn_cfg_hub_count(c)) {
        return ATN_ERR_PARAM;
    }
    *ipv4_host = c->hub[i].ipv4_host;
    *port = c->hub[i].port;
    memcpy(ek, c->hub[i].ek, ATN_MLKEM1024_EK_LEN);
    return ATN_OK;
}
