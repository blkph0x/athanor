/*
 * Mesh update announce + chunks (DEC-0048).
 * Tunnel DATA only — no HTTP/URL/cleartext binary path.
 */
#include "atn_update.h"
#include "atn_crypto.h"
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

void atn_update_init(atn_update *u)
{
    if (u == NULL) {
        return;
    }
    memset(u, 0, sizeof(*u));
    u->chunk_size = ATN_UPD_CHUNK_MAX;
    memcpy(u->payload_path, "lab/updates/payload.bin", 23);
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int parse_u32(const char *s, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    size_t i;
    if (n == 0 || n > 10) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return ATN_ERR_PARAM;
        }
        v = v * 10u + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return ATN_OK;
}

static int kind_parse(const char *s, size_t n, uint8_t *out)
{
    if (n == 3 && memcmp(s, "apk", 3) == 0) {
        *out = ATN_UPD_KIND_APK;
        return ATN_OK;
    }
    if (n == 4 && memcmp(s, "site", 4) == 0) {
        *out = ATN_UPD_KIND_SITE;
        return ATN_OK;
    }
    if (n == 3 && memcmp(s, "hub", 3) == 0) {
        *out = ATN_UPD_KIND_HUB;
        return ATN_OK;
    }
    return ATN_ERR_PARAM;
}

static const char *kind_name(uint8_t k)
{
    switch (k) {
    case ATN_UPD_KIND_SITE:
        return "site";
    case ATN_UPD_KIND_HUB:
        return "hub";
    default:
        return "apk";
    }
}

static int hex_ok(const char *s, size_t n)
{
    size_t i;
    if (n != ATN_UPD_SHA_HEX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

int atn_update_parse(const char *text, size_t n, atn_update *u)
{
    size_t i = 0;
    if (u == NULL || (text == NULL && n > 0)) {
        return ATN_ERR_PARAM;
    }
    atn_update_init(u);
    while (i < n) {
        const char *line;
        size_t ln, eq, klen, vstart, vlen;
        while (i < n && (text[i] == '\r' || text[i] == '\n')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        line = text + i;
        ln = 0;
        while (i + ln < n && text[i + ln] != '\n' && text[i + ln] != '\r') {
            ln++;
        }
        i += ln;
        while (ln > 0 && is_ws(line[ln - 1])) {
            ln--;
        }
        if (ln == 0 || line[0] == '#') {
            continue;
        }
        eq = 0;
        while (eq < ln && line[eq] != '=') {
            eq++;
        }
        if (eq == 0 || eq >= ln) {
            return ATN_ERR_PARAM;
        }
        klen = eq;
        while (klen > 0 && is_ws(line[klen - 1])) {
            klen--;
        }
        vstart = eq + 1;
        while (vstart < ln && is_ws(line[vstart])) {
            vstart++;
        }
        vlen = ln - vstart;
        if (klen == 9 && memcmp(line, "update_id", 9) == 0) {
            if (parse_u32(line + vstart, vlen, &u->update_id) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 4 && memcmp(line, "kind", 4) == 0) {
            if (kind_parse(line + vstart, vlen, &u->kind) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 7 && memcmp(line, "version", 7) == 0) {
            if (vlen == 0 || vlen >= ATN_UPD_VER_MAX) {
                return ATN_ERR_PARAM;
            }
            memcpy(u->version, line + vstart, vlen);
            u->version[vlen] = '\0';
        } else if (klen == 6 && memcmp(line, "sha256", 6) == 0) {
            if (!hex_ok(line + vstart, vlen)) {
                return ATN_ERR_PARAM;
            }
            memcpy(u->sha256_hex, line + vstart, vlen);
            u->sha256_hex[vlen] = '\0';
        } else if (klen == 4 && memcmp(line, "size", 4) == 0) {
            if (parse_u32(line + vstart, vlen, &u->size) != ATN_OK) {
                return ATN_ERR_PARAM;
            }
        } else if (klen == 10 && memcmp(line, "chunk_size", 10) == 0) {
            uint32_t cs;
            if (parse_u32(line + vstart, vlen, &cs) != ATN_OK ||
                cs < 64u || cs > ATN_UPD_CHUNK_MAX) {
                return ATN_ERR_PARAM;
            }
            u->chunk_size = cs;
        } else if (klen == 12 && memcmp(line, "payload_path", 12) == 0) {
            if (vlen == 0 || vlen >= ATN_UPD_PATH_MAX) {
                return ATN_ERR_PARAM;
            }
            memcpy(u->payload_path, line + vstart, vlen);
            u->payload_path[vlen] = '\0';
        } else if (klen == 6 && memcmp(line, "action", 6) == 0) {
            if (vlen == 8 && memcmp(line + vstart, "announce", 8) == 0) {
                u->action = ATN_UPD_ACT_ANNOUNCE;
            } else if (vlen == 5 && memcmp(line + vstart, "chunk", 5) == 0) {
                u->action = ATN_UPD_ACT_CHUNK;
            } else if (vlen == 3 && memcmp(line + vstart, "req", 3) == 0) {
                u->action = ATN_UPD_ACT_REQ;
            } else {
                return ATN_ERR_PARAM;
            }
        } else {
            return ATN_ERR_PARAM;
        }
    }
    return ATN_OK;
}

int atn_update_load_file(const char *path, atn_update *u)
{
    FILE *f;
    char buf[ATN_UPD_MAX_TEXT];
    size_t n;
    if (u == NULL || path == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_update_init(u);
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_OK;
    }
    n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    return atn_update_parse(buf, n, u);
}

int atn_update_save_file(const char *path, const atn_update *u)
{
    char text[ATN_UPD_MAX_TEXT];
    size_t n = 0;
    FILE *f;
    if (path == NULL || u == NULL) {
        return ATN_ERR_PARAM;
    }
    if (atn_update_encode(u, text, sizeof(text), &n) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return ATN_ERR_SOCK;
    }
    if (fwrite(text, 1, n, f) != n) {
        fclose(f);
        return ATN_ERR_SOCK;
    }
    fclose(f);
    return ATN_OK;
}

int atn_update_encode(const atn_update *u, char *out, size_t out_cap,
                      size_t *out_n)
{
    int nw;
    if (u == NULL || out == NULL || out_cap < 64u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    nw = snprintf(out, out_cap,
                  "update_id=%u\n"
                  "kind=%s\n"
                  "version=%s\n"
                  "sha256=%s\n"
                  "size=%u\n"
                  "chunk_size=%u\n"
                  "payload_path=%s\n",
                  (unsigned)u->update_id, kind_name(u->kind), u->version,
                  u->sha256_hex, (unsigned)u->size, (unsigned)u->chunk_size,
                  u->payload_path);
    if (nw < 0 || (size_t)nw >= out_cap) {
        return ATN_ERR_PARAM;
    }
    *out_n = (size_t)nw;
    return ATN_OK;
}

int atn_update_encode_announce_wire(const atn_update *u, uint8_t *out,
                                    size_t out_cap, size_t *out_n)
{
    char text[ATN_UPD_MAX_TEXT];
    char body[ATN_UPD_MAX_TEXT];
    size_t tn = 0;
    int nw;
    if (u == NULL || out == NULL || out_cap < 2u || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    if (atn_update_encode(u, text, sizeof(text), &tn) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    nw = snprintf(body, sizeof(body), "action=announce\n%.*s", (int)tn, text);
    if (nw < 0 || (size_t)nw >= sizeof(body) || 1u + (size_t)nw > out_cap) {
        return ATN_ERR_PARAM;
    }
    out[0] = (uint8_t)ATN_UPD_WIRE;
    memcpy(out + 1, body, (size_t)nw);
    *out_n = 1u + (size_t)nw;
    return ATN_OK;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int atn_update_encode_chunk_wire(uint32_t update_id, uint32_t offset,
                                 const uint8_t *data, uint16_t len,
                                 uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t need;
    if (data == NULL || out == NULL || out_n == NULL || len == 0 ||
        len > ATN_UPD_CHUNK_MAX) {
        return ATN_ERR_PARAM;
    }
    need = 1u + 1u + 4u + 4u + 2u + (size_t)len;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_PARAM;
    }
    out[0] = (uint8_t)ATN_UPD_WIRE;
    out[1] = (uint8_t)'C';
    put_be32(out + 2, update_id);
    put_be32(out + 6, offset);
    put_be16(out + 10, len);
    memcpy(out + 12, data, len);
    *out_n = need;
    return ATN_OK;
}

int atn_update_parse_wire(const uint8_t *msg, size_t n, atn_update *u,
                          uint32_t *chunk_off, uint16_t *chunk_len,
                          const uint8_t **chunk_data)
{
    if (msg == NULL || u == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_UPD_WIRE) {
        return ATN_ERR_PARAM;
    }
    if (n >= 2u && msg[1] == (uint8_t)'C') {
        uint16_t ln;
        if (n < 12u || chunk_off == NULL || chunk_len == NULL ||
            chunk_data == NULL) {
            return ATN_ERR_PARAM;
        }
        atn_update_init(u);
        u->action = ATN_UPD_ACT_CHUNK;
        u->update_id = get_be32(msg + 2);
        *chunk_off = get_be32(msg + 6);
        ln = get_be16(msg + 10);
        if ((size_t)12u + (size_t)ln != n || ln > ATN_UPD_CHUNK_MAX) {
            return ATN_ERR_PARAM;
        }
        *chunk_len = ln;
        *chunk_data = msg + 12;
        return ATN_OK;
    }
    if (n == 1u || (n == 2u && msg[1] == (uint8_t)'?')) {
        atn_update_init(u);
        u->action = ATN_UPD_ACT_REQ;
        return ATN_ERR_STATE;
    }
    if (atn_update_parse((const char *)(msg + 1), n - 1u, u) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    if (u->action == 0) {
        u->action = ATN_UPD_ACT_ANNOUNCE;
    }
    return ATN_OK;
}

int atn_update_file_sha256_hex(const char *path, char out_hex[ATN_UPD_SHA_HEX + 1u],
                               uint32_t *out_size)
{
    FILE *f;
    uint8_t buf[4096];
    uint8_t dig[32];
    atn_sha256_ctx ctx;
    size_t n;
    uint32_t total = 0;
    static const char hx[] = "0123456789abcdef";
    unsigned i;
    if (path == NULL || out_hex == NULL) {
        return ATN_ERR_PARAM;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_ERR_SOCK;
    }
    atn_sha256_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (total + (uint32_t)n < total) {
            fclose(f);
            return ATN_ERR_PARAM;
        }
        total += (uint32_t)n;
        atn_sha256_update(&ctx, buf, n);
    }
    fclose(f);
    atn_sha256_final(&ctx, dig);
    for (i = 0; i < 32u; i++) {
        out_hex[i * 2u] = hx[dig[i] >> 4];
        out_hex[i * 2u + 1u] = hx[dig[i] & 15u];
    }
    out_hex[ATN_UPD_SHA_HEX] = '\0';
    if (out_size != NULL) {
        *out_size = total;
    }
    atn_memzero(dig, sizeof(dig));
    atn_memzero(buf, sizeof(buf));
    return ATN_OK;
}
