/*
 * Module: atn_hmac.c
 * REQ:    REQ-1.1
 * Spec:   RFC 2104 (HMAC). KATs: RFC 4231 §§4.2–4.8 for SHA-256.
 *
 *   HMAC(K, m) = H( (K' XOR opad) || H( (K' XOR ipad) || m ) )
 *   ipad = 0x36 repeated B times, opad = 0x5c repeated B times.
 *   B = 64 for SHA-256. If K is longer than B, K' = H(K); else K' is K padded
 *   with zeros to B bytes.
 */

#include "atn_crypto.h"

#include <string.h>

int atn_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[ATN_HMAC_LEN])
{
    uint8_t kpad[ATN_SHA256_BLOCK];
    uint8_t inner[ATN_SHA256_LEN];
    atn_sha256_ctx ctx;
    size_t i;
    int rc;

    if (out == NULL) {
        return ATN_ERR_PARAM;
    }
    if (key == NULL && key_len != 0) {
        return ATN_ERR_PARAM;
    }
    if (msg == NULL && msg_len != 0) {
        return ATN_ERR_PARAM;
    }

    memset(kpad, 0, sizeof(kpad));
    if (key_len > ATN_SHA256_BLOCK) {
        rc = atn_sha256(key, key_len, kpad);
        if (rc != ATN_OK) {
            atn_memzero(kpad, sizeof(kpad));
            return rc;
        }
    } else if (key_len > 0) {
        memcpy(kpad, key, key_len);
    }

    /* Inner: (K' XOR ipad) || msg */
    for (i = 0; i < ATN_SHA256_BLOCK; i++) {
        kpad[i] ^= 0x36u;
    }
    atn_sha256_init(&ctx);
    rc = atn_sha256_update(&ctx, kpad, ATN_SHA256_BLOCK);
    if (rc == ATN_OK) {
        rc = atn_sha256_update(&ctx, msg, msg_len);
    }
    if (rc == ATN_OK) {
        rc = atn_sha256_final(&ctx, inner);
    }
    if (rc != ATN_OK) {
        atn_memzero(&ctx, sizeof(ctx));
        atn_memzero(kpad, sizeof(kpad));
        atn_memzero(inner, sizeof(inner));
        return rc;
    }

    /* Restore K' then apply opad: undo 0x36, apply 0x5c => XOR 0x36^0x5c. */
    for (i = 0; i < ATN_SHA256_BLOCK; i++) {
        kpad[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    }

    atn_sha256_init(&ctx);
    rc = atn_sha256_update(&ctx, kpad, ATN_SHA256_BLOCK);
    if (rc == ATN_OK) {
        rc = atn_sha256_update(&ctx, inner, ATN_SHA256_LEN);
    }
    if (rc == ATN_OK) {
        rc = atn_sha256_final(&ctx, out);
    }

    atn_memzero(&ctx, sizeof(ctx));
    atn_memzero(kpad, sizeof(kpad));
    atn_memzero(inner, sizeof(inner));
    return rc;
}
