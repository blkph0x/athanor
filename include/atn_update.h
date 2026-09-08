/*
 * Mesh update announce + chunks (DEC-0048).
 * Hub-authoritative lab/updates/announce.conf + payload.bin.
 * Wire: ATN_TUN_DATA 'U' (+ announce text | 'C' binary chunk | '?')
 * ONLY over the existing PQ/AEAD tunnel — no HTTP/URL/cleartext path.
 */
#ifndef ATN_UPDATE_H
#define ATN_UPDATE_H

#include "atn_crypto.h"

#define ATN_UPD_WIRE         0x55u /* 'U' */
#define ATN_UPD_MAX_TEXT     1024u
#define ATN_UPD_CHUNK_MAX    900u  /* fits ATN_TUN_MAX_PT with binary hdr */
#define ATN_UPD_KIND_APK     1u
#define ATN_UPD_KIND_SITE    2u
#define ATN_UPD_KIND_HUB     3u
#define ATN_UPD_ACT_ANNOUNCE 1u
#define ATN_UPD_ACT_CHUNK    2u
#define ATN_UPD_ACT_REQ     3u /* peer asks for announce / missing chunks */
#define ATN_UPD_SHA_HEX     64u
#define ATN_UPD_VER_MAX      64u
#define ATN_UPD_PATH_MAX    256u

typedef struct {
    uint32_t update_id;
    uint8_t  kind; /* ATN_UPD_KIND_* */
    uint8_t  action; /* wire only */
    char     version[ATN_UPD_VER_MAX];
    char     sha256_hex[ATN_UPD_SHA_HEX + 1u];
    uint32_t size;
    uint32_t chunk_size; /* default ATN_UPD_CHUNK_MAX */
    char     payload_path[ATN_UPD_PATH_MAX]; /* hub-local path */
} atn_update;

void atn_update_init(atn_update *u);
int  atn_update_parse(const char *text, size_t n, atn_update *u);
int  atn_update_load_file(const char *path, atn_update *u);
int  atn_update_save_file(const char *path, const atn_update *u);
int  atn_update_encode(const atn_update *u, char *out, size_t out_cap,
                       size_t *out_n);
int  atn_update_encode_announce_wire(const atn_update *u, uint8_t *out,
                                     size_t out_cap, size_t *out_n);
/*
 * Chunk wire: 'U' 'C' id:4be offset:4be len:2be data[len]
 * Returns ATN_OK and *out_n total bytes.
 */
int atn_update_encode_chunk_wire(uint32_t update_id, uint32_t offset,
                                 const uint8_t *data, uint16_t len,
                                 uint8_t *out, size_t out_cap, size_t *out_n);
int atn_update_parse_wire(const uint8_t *msg, size_t n, atn_update *u,
                          uint32_t *chunk_off, uint16_t *chunk_len,
                          const uint8_t **chunk_data);
int atn_update_file_sha256_hex(const char *path, char out_hex[ATN_UPD_SHA_HEX + 1u],
                               uint32_t *out_size);

#endif
