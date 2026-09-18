/*
 * Mesh messaging + file share encode/parse (DEC-0055).
 */
#include "atn_mesh.h"

#include "atn_tun.h"

#include <string.h>

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xffu);
    p[1] = (uint8_t)(v & 0xffu);
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int atn_mesh_encode_text(const char *from, const uint8_t *body, uint16_t body_len,
                         uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t from_len, need;
    if (from == NULL || body == NULL || out == NULL || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    if (body_len == 0u || body_len > ATN_MESH_BODY_MAX) {
        return ATN_ERR_LEN;
    }
    from_len = strlen(from);
    if (from_len == 0u || from_len >= ATN_MESH_FROM_MAX) {
        return ATN_ERR_LEN;
    }
    need = 2u + 1u + from_len + 2u + (size_t)body_len;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    out[0] = ATN_MESH_WIRE;
    out[1] = ATN_MESH_TEXT;
    out[2] = (uint8_t)from_len;
    memcpy(out + 3, from, from_len);
    put_be16(out + 3u + from_len, body_len);
    memcpy(out + 3u + from_len + 2u, body, body_len);
    *out_n = need;
    return ATN_OK;
}

int atn_mesh_parse_text(const uint8_t *msg, size_t n, atn_mesh_text *t)
{
    size_t from_len, off;
    uint16_t blen;
    if (msg == NULL || t == NULL || n < 5u) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_MESH_WIRE || msg[1] != ATN_MESH_TEXT) {
        return ATN_ERR_STATE;
    }
    atn_memzero(t, sizeof(*t));
    from_len = msg[2];
    if (from_len == 0u || from_len >= ATN_MESH_FROM_MAX ||
        n < 3u + from_len + 2u) {
        return ATN_ERR_LEN;
    }
    memcpy(t->from, msg + 3, from_len);
    t->from[from_len] = '\0';
    off = 3u + from_len;
    blen = get_be16(msg + off);
    off += 2u;
    if (blen == 0u || blen > ATN_MESH_BODY_MAX || off + blen != n) {
        return ATN_ERR_LEN;
    }
    memcpy(t->body, msg + off, blen);
    t->body_len = blen;
    return ATN_OK;
}

int atn_mesh_encode_file(const atn_mesh_file *f, uint8_t *out, size_t out_cap,
                         size_t *out_n)
{
    size_t name_len, need;
    if (f == NULL || out == NULL || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    name_len = strlen(f->name);
    if (name_len == 0u || name_len >= ATN_MESH_NAME_MAX || f->size == 0u) {
        return ATN_ERR_LEN;
    }
    need = 2u + 4u + 4u + 1u + name_len + ATN_MESH_SHA_LEN;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    out[0] = ATN_MESH_WIRE;
    out[1] = ATN_MESH_FILE;
    put_be32(out + 2, f->file_id);
    put_be32(out + 6, f->size);
    out[10] = (uint8_t)name_len;
    memcpy(out + 11, f->name, name_len);
    memcpy(out + 11u + name_len, f->sha256, ATN_MESH_SHA_LEN);
    *out_n = need;
    return ATN_OK;
}

int atn_mesh_parse_file(const uint8_t *msg, size_t n, atn_mesh_file *f)
{
    size_t name_len, need;
    if (msg == NULL || f == NULL || n < 2u + 4u + 4u + 1u + ATN_MESH_SHA_LEN) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_MESH_WIRE || msg[1] != ATN_MESH_FILE) {
        return ATN_ERR_STATE;
    }
    atn_memzero(f, sizeof(*f));
    f->file_id = get_be32(msg + 2);
    f->size = get_be32(msg + 6);
    name_len = msg[10];
    if (name_len == 0u || name_len >= ATN_MESH_NAME_MAX) {
        return ATN_ERR_LEN;
    }
    need = 11u + name_len + ATN_MESH_SHA_LEN;
    if (n != need || f->size == 0u) {
        return ATN_ERR_LEN;
    }
    memcpy(f->name, msg + 11, name_len);
    f->name[name_len] = '\0';
    memcpy(f->sha256, msg + 11u + name_len, ATN_MESH_SHA_LEN);
    return ATN_OK;
}

int atn_mesh_encode_chunk(uint32_t file_id, uint32_t offset,
                          const uint8_t *data, uint16_t len,
                          uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t need;
    if (data == NULL || out == NULL || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    if (len == 0u || len > ATN_MESH_CHUNK_MAX) {
        return ATN_ERR_LEN;
    }
    need = 2u + 4u + 4u + 2u + (size_t)len;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    out[0] = ATN_MESH_WIRE;
    out[1] = ATN_MESH_CHUNK;
    put_be32(out + 2, file_id);
    put_be32(out + 6, offset);
    put_be16(out + 10, len);
    memcpy(out + 12, data, len);
    *out_n = need;
    return ATN_OK;
}

int atn_mesh_parse_chunk(const uint8_t *msg, size_t n, uint32_t *file_id,
                         uint32_t *offset, uint16_t *len,
                         const uint8_t **data)
{
    uint16_t L;
    if (msg == NULL || file_id == NULL || offset == NULL || len == NULL ||
        data == NULL || n < 12u) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] != ATN_MESH_WIRE || msg[1] != ATN_MESH_CHUNK) {
        return ATN_ERR_STATE;
    }
    *file_id = get_be32(msg + 2);
    *offset = get_be32(msg + 6);
    L = get_be16(msg + 10);
    if (L == 0u || L > ATN_MESH_CHUNK_MAX || 12u + (size_t)L != n) {
        return ATN_ERR_LEN;
    }
    *len = L;
    *data = msg + 12;
    return ATN_OK;
}
