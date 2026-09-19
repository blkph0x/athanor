/*
 * Mesh messaging + file share wire (DEC-0055 / DEC-0057).
 * ONLY over existing PQ/AEAD ATN_TUN_DATA — no HTTP/cleartext side channel.
 * Family 'M': text (with from+to), file announce, file chunk.
 */
#ifndef ATN_MESH_H
#define ATN_MESH_H

#include "atn_crypto.h"

#define ATN_MESH_WIRE        0x4Du /* 'M' */
#define ATN_MESH_TEXT        0x54u /* 'T' */
#define ATN_MESH_FILE        0x46u /* 'F' announce */
#define ATN_MESH_CHUNK       0x43u /* 'C' */
#define ATN_MESH_CHUNK_MAX   900u
#define ATN_MESH_NAME_MAX    64u
#define ATN_MESH_FROM_MAX    64u
#define ATN_MESH_TO_MAX      64u
#define ATN_MESH_BODY_MAX    800u
#define ATN_MESH_SHA_LEN     32u
#define ATN_MESH_TO_ANY      "*"

typedef struct {
    char     from[ATN_MESH_FROM_MAX];
    char     to[ATN_MESH_TO_MAX];
    char     body[ATN_MESH_BODY_MAX];
    uint16_t body_len;
} atn_mesh_text;

typedef struct {
    uint32_t file_id;
    uint32_t size;
    char     name[ATN_MESH_NAME_MAX];
    char     from[ATN_MESH_FROM_MAX];
    char     to[ATN_MESH_TO_MAX];
    uint8_t  sha256[ATN_MESH_SHA_LEN];
} atn_mesh_file;

int atn_mesh_encode_text(const char *from, const char *to,
                         const uint8_t *body, uint16_t body_len,
                         uint8_t *out, size_t out_cap, size_t *out_n);
int atn_mesh_parse_text(const uint8_t *msg, size_t n, atn_mesh_text *t);

int atn_mesh_encode_file(const atn_mesh_file *f, uint8_t *out, size_t out_cap,
                         size_t *out_n);
int atn_mesh_parse_file(const uint8_t *msg, size_t n, atn_mesh_file *f);

int atn_mesh_encode_chunk(uint32_t file_id, uint32_t offset,
                          const uint8_t *data, uint16_t len,
                          uint8_t *out, size_t out_cap, size_t *out_n);
int atn_mesh_parse_chunk(const uint8_t *msg, size_t n, uint32_t *file_id,
                         uint32_t *offset, uint16_t *len,
                         const uint8_t **data);

#endif
