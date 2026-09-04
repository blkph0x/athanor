/*
 * Module: atn_aead.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §2.6 (Poly1305 one-time key from ChaCha20 block 0)
 *         RFC 8439 §2.8 (AEAD_CHACHA20_POLY1305)
 *
 * Encrypt: otk = chacha20_block(key, 0, nonce)[0..31]
 *          ct  = chacha20_xor (key, 1, nonce, pt)
 *          tag = poly1305(otk, aad || pad16(aad) || ct || pad16(ct)
 *                              || le64(aad_len) || le64(ct_len))
 * Decrypt: compute tag over the ciphertext, atn_ct_equal, then xor.
 *          On mismatch, pt is zeroed and ATN_ERR_AUTH is returned.
 */

#include "atn_crypto.h"

#include <string.h>

static void store_le64(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static int poly_pad16(atn_poly1305_ctx *st, size_t len)
{
    static const uint8_t zeros[16] = {0};
    size_t rem = len & 15u;
    if (rem == 0) {
        return ATN_OK;
    }
    return atn_poly1305_update(st, zeros, 16u - rem);
}

static int aead_tag(const uint8_t otk[ATN_POLY1305_KEY_LEN],
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *ct, size_t ct_len,
                    uint8_t tag[ATN_AEAD_TAG_LEN])
{
    atn_poly1305_ctx st;
    uint8_t lens[16];
    int rc;

    rc = atn_poly1305_init(&st, otk);
    if (rc == ATN_OK) {
        rc = atn_poly1305_update(&st, aad, aad_len);
    }
    if (rc == ATN_OK) {
        rc = poly_pad16(&st, aad_len);
    }
    if (rc == ATN_OK) {
        rc = atn_poly1305_update(&st, ct, ct_len);
    }
    if (rc == ATN_OK) {
        rc = poly_pad16(&st, ct_len);
    }
    if (rc == ATN_OK) {
        store_le64(lens + 0, (uint64_t)aad_len);
        store_le64(lens + 8, (uint64_t)ct_len);
        rc = atn_poly1305_update(&st, lens, 16);
    }
    if (rc == ATN_OK) {
        rc = atn_poly1305_final(&st, tag);
    }
    atn_memzero(&st, sizeof(st));
    atn_memzero(lens, sizeof(lens));
    return rc;
}

int atn_aead_encrypt(const uint8_t key[ATN_AEAD_KEY_LEN],
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *pt, size_t pt_len,
                     uint8_t *ct,
                     uint8_t tag[ATN_AEAD_TAG_LEN])
{
    uint8_t block0[ATN_CHACHA20_BLOCK];
    int rc;

    if (key == NULL || nonce == NULL || tag == NULL) {
        return ATN_ERR_PARAM;
    }
    if (pt_len > 0 && (pt == NULL || ct == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (aad_len > 0 && aad == NULL) {
        return ATN_ERR_PARAM;
    }

    /* RFC 8439 §2.6: Poly1305 one-time key is the first 32 bytes of block 0. */
    atn_chacha20_block(key, 0, nonce, block0);

    /* RFC 8439 §2.8: encryption starts at counter 1 so it does not reuse block 0. */
    rc = atn_chacha20_xor(key, 1, nonce, pt, ct, pt_len);
    if (rc == ATN_OK) {
        rc = aead_tag(block0, aad, aad_len, ct, pt_len, tag);
    }
    atn_memzero(block0, sizeof(block0));
    if (rc != ATN_OK && ct != NULL && pt_len > 0) {
        atn_memzero(ct, pt_len);
    }
    return rc;
}

int atn_aead_decrypt(const uint8_t key[ATN_AEAD_KEY_LEN],
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *ct, size_t ct_len,
                     const uint8_t tag[ATN_AEAD_TAG_LEN],
                     uint8_t *pt)
{
    uint8_t block0[ATN_CHACHA20_BLOCK];
    uint8_t expect[ATN_AEAD_TAG_LEN];
    int rc;

    if (key == NULL || nonce == NULL || tag == NULL) {
        return ATN_ERR_PARAM;
    }
    if (ct_len > 0 && (ct == NULL || pt == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (aad_len > 0 && aad == NULL) {
        return ATN_ERR_PARAM;
    }

    atn_chacha20_block(key, 0, nonce, block0);
    rc = aead_tag(block0, aad, aad_len, ct, ct_len, expect);
    if (rc != ATN_OK) {
        atn_memzero(block0, sizeof(block0));
        atn_memzero(expect, sizeof(expect));
        return rc;
    }
    if (!atn_ct_equal(expect, tag, ATN_AEAD_TAG_LEN)) {
        atn_memzero(block0, sizeof(block0));
        atn_memzero(expect, sizeof(expect));
        if (pt != NULL && ct_len > 0) {
            atn_memzero(pt, ct_len);
        }
        return ATN_ERR_AUTH;
    }
    rc = atn_chacha20_xor(key, 1, nonce, ct, pt, ct_len);
    atn_memzero(block0, sizeof(block0));
    atn_memzero(expect, sizeof(expect));
    if (rc != ATN_OK && pt != NULL && ct_len > 0) {
        atn_memzero(pt, ct_len);
    }
    return rc;
}
