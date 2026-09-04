/*
 * Module: atn_hkdf.c
 * REQ:    REQ-1.1
 * Spec:   RFC 5869 §§2.2–2.3. KATs: Appendix A.1–A.3 (SHA-256).
 *
 * Extract: PRK = HMAC-Hash(salt, IKM)     ; salt defaults to HashLen zeros
 * Expand:  T(0)=empty; T(i)=HMAC(PRK, T(i-1) | info | i) ; OKM = first L of T
 * L <= 255 * HashLen.
 */

#include "atn_crypto.h"

#include <string.h>

int atn_hkdf_extract(const uint8_t *salt, size_t salt_len,
                     const uint8_t *ikm, size_t ikm_len,
                     uint8_t prk[ATN_SHA256_LEN])
{
    uint8_t zero_salt[ATN_SHA256_LEN];
    const uint8_t *s = salt;
    size_t sl = salt_len;

    if (prk == NULL) {
        return ATN_ERR_PARAM;
    }
    if (ikm == NULL && ikm_len != 0) {
        return ATN_ERR_PARAM;
    }
    if (salt == NULL && salt_len != 0) {
        return ATN_ERR_PARAM;
    }

    if (salt == NULL || salt_len == 0) {
        /* RFC 5869 §2.2: if salt not provided, HashLen zeros. */
        atn_memzero(zero_salt, sizeof(zero_salt));
        s = zero_salt;
        sl = ATN_SHA256_LEN;
    }
    return atn_hmac_sha256(s, sl, ikm, ikm_len, prk);
}

int atn_hkdf_expand(const uint8_t *prk, size_t prk_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *okm, size_t okm_len)
{
    uint8_t t[ATN_HMAC_LEN];
    uint8_t ti[ATN_HMAC_LEN];
    size_t t_len = 0;
    size_t filled = 0;
    uint8_t i;
    unsigned n;
    int rc = ATN_OK;

    if (okm == NULL && okm_len != 0) {
        return ATN_ERR_PARAM;
    }
    if (prk == NULL || prk_len < ATN_SHA256_LEN) {
        return ATN_ERR_PARAM;
    }
    if (info == NULL && info_len != 0) {
        return ATN_ERR_PARAM;
    }
    if (okm_len == 0) {
        return ATN_OK;
    }
    /* RFC 5869 §2.3: L <= 255 * HashLen */
    if (okm_len > 255u * ATN_SHA256_LEN) {
        return ATN_ERR_LEN;
    }

    n = (unsigned)((okm_len + ATN_HMAC_LEN - 1u) / ATN_HMAC_LEN);
    for (i = 1; i <= (uint8_t)n; i++) {
        /* HMAC(PRK, T(i-1) | info | i) streamed; info may be long. */
            uint8_t kpad[ATN_SHA256_BLOCK];
            atn_sha256_ctx ctx;
            size_t k;
            size_t use_key_len = prk_len;
            const uint8_t *use_key = prk;
            uint8_t hashed_key[ATN_SHA256_LEN];

            memset(kpad, 0, sizeof(kpad));
            if (use_key_len > ATN_SHA256_BLOCK) {
                rc = atn_sha256(use_key, use_key_len, hashed_key);
                if (rc != ATN_OK) {
                    break;
                }
                use_key = hashed_key;
                use_key_len = ATN_SHA256_LEN;
            }
            memcpy(kpad, use_key, use_key_len);
            for (k = 0; k < ATN_SHA256_BLOCK; k++) {
                kpad[k] ^= 0x36u;
            }
            atn_sha256_init(&ctx);
            rc = atn_sha256_update(&ctx, kpad, ATN_SHA256_BLOCK);
            if (rc == ATN_OK && t_len > 0) {
                rc = atn_sha256_update(&ctx, t, t_len);
            }
            if (rc == ATN_OK && info_len > 0) {
                rc = atn_sha256_update(&ctx, info, info_len);
            }
            if (rc == ATN_OK) {
                rc = atn_sha256_update(&ctx, &i, 1);
            }
            if (rc == ATN_OK) {
                rc = atn_sha256_final(&ctx, ti);
            }
            if (rc != ATN_OK) {
                atn_memzero(kpad, sizeof(kpad));
                atn_memzero(&ctx, sizeof(ctx));
                atn_memzero(hashed_key, sizeof(hashed_key));
                break;
            }
            for (k = 0; k < ATN_SHA256_BLOCK; k++) {
                kpad[k] ^= (uint8_t)(0x36u ^ 0x5cu);
            }
            atn_sha256_init(&ctx);
            rc = atn_sha256_update(&ctx, kpad, ATN_SHA256_BLOCK);
            if (rc == ATN_OK) {
                rc = atn_sha256_update(&ctx, ti, ATN_HMAC_LEN);
            }
            if (rc == ATN_OK) {
                rc = atn_sha256_final(&ctx, ti);
            }
            atn_memzero(kpad, sizeof(kpad));
            atn_memzero(&ctx, sizeof(ctx));
            atn_memzero(hashed_key, sizeof(hashed_key));
            if (rc != ATN_OK) {
                break;
            }

        memcpy(t, ti, ATN_HMAC_LEN);
        t_len = ATN_HMAC_LEN;
        {
            size_t take = okm_len - filled;
            if (take > ATN_HMAC_LEN) {
                take = ATN_HMAC_LEN;
            }
            memcpy(okm + filled, ti, take);
            filled += take;
        }
    }

    atn_memzero(t, sizeof(t));
    atn_memzero(ti, sizeof(ti));
    if (rc != ATN_OK) {
        atn_memzero(okm, okm_len);
    }
    return rc;
}

int atn_hkdf(const uint8_t *salt, size_t salt_len,
             const uint8_t *ikm, size_t ikm_len,
             const uint8_t *info, size_t info_len,
             uint8_t *okm, size_t okm_len)
{
    uint8_t prk[ATN_SHA256_LEN];
    int rc = atn_hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
    if (rc == ATN_OK) {
        rc = atn_hkdf_expand(prk, ATN_SHA256_LEN, info, info_len, okm, okm_len);
    }
    atn_memzero(prk, sizeof(prk));
    return rc;
}
