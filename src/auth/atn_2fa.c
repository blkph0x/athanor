/*
 * Module: atn_2fa.c
 * REQ:    REQ-1.3
 * Spec:   DEC-0008. MAC = HMAC-SHA-512 (RFC 2104) over challenge ‖ "atn-2fa-v1"
 */

#include "atn_2fa.h"

#include <string.h>

static const uint8_t CTX[10] = {
    'a','t','n','-','2','f','a','-','v','1'
};

static atn_2fa_slot *find_id(atn_2fa_store *st, const uint8_t id[32])
{
    unsigned i;
    for (i = 0; i < ATN_2FA_SLOTS; i++) {
        if (st->slots[i].in_use && atn_ct_equal(st->slots[i].id, id, ATN_2FA_ID_LEN)) {
            return &st->slots[i];
        }
    }
    return NULL;
}

static int used_contains(const atn_2fa_slot *s, const uint8_t h[32])
{
    uint32_t i;
    for (i = 0; i < s->used_n && i < ATN_2FA_REPLAY_CAP; i++) {
        if (atn_ct_equal(s->used[i], h, 32)) {
            return 1;
        }
    }
    return 0;
}

void atn_2fa_store_init(atn_2fa_store *st)
{
    if (st != NULL) {
        atn_memzero(st, sizeof(*st));
    }
}

int atn_2fa_enroll(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                   uint8_t key_out[ATN_2FA_KEY_LEN])
{
    unsigned i;
    if (st == NULL || id == NULL || key_out == NULL) {
        return ATN_ERR_PARAM;
    }
    if (find_id(st, id) != NULL) {
        return ATN_ERR_STATE;
    }
    for (i = 0; i < ATN_2FA_SLOTS; i++) {
        if (!st->slots[i].in_use) {
            if (atn_random_bytes(key_out, ATN_2FA_KEY_LEN) != ATN_OK) {
                return ATN_ERR_ENTROPY;
            }
            memcpy(st->slots[i].id, id, ATN_2FA_ID_LEN);
            memcpy(st->slots[i].key, key_out, ATN_2FA_KEY_LEN);
            st->slots[i].in_use = 1;
            st->slots[i].locked = 0;
            st->slots[i].fails = 0;
            st->slots[i].used_n = 0;
            st->slots[i].have_pending = 0;
            return ATN_OK;
        }
    }
    return ATN_ERR_LEN;
}

int atn_2fa_revoke(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN])
{
    atn_2fa_slot *s;
    if (st == NULL || id == NULL) {
        return ATN_ERR_PARAM;
    }
    s = find_id(st, id);
    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_memzero(s, sizeof(*s));
    return ATN_OK;
}

int atn_2fa_challenge(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                      uint8_t chal[ATN_2FA_CHAL_LEN])
{
    atn_2fa_slot *s;
    if (st == NULL || id == NULL || chal == NULL) {
        return ATN_ERR_PARAM;
    }
    s = find_id(st, id);
    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    if (s->locked) {
        return ATN_ERR_LOCKOUT;
    }
    if (atn_random_bytes(chal, ATN_2FA_CHAL_LEN) != ATN_OK) {
        return ATN_ERR_ENTROPY;
    }
    memcpy(s->pending, chal, ATN_2FA_CHAL_LEN);
    s->have_pending = 1;
    return ATN_OK;
}

int atn_2fa_respond(const uint8_t key[ATN_2FA_KEY_LEN],
                    const uint8_t chal[ATN_2FA_CHAL_LEN],
                    uint8_t resp[ATN_2FA_RESP_LEN])
{
    uint8_t msg[ATN_2FA_CHAL_LEN + 10];
    if (key == NULL || chal == NULL || resp == NULL) {
        return ATN_ERR_PARAM;
    }
    memcpy(msg, chal, ATN_2FA_CHAL_LEN);
    memcpy(msg + ATN_2FA_CHAL_LEN, CTX, 10);
    return atn_hmac_sha512(key, ATN_2FA_KEY_LEN, msg, sizeof(msg), resp);
}

int atn_2fa_verify(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                   const uint8_t chal[ATN_2FA_CHAL_LEN],
                   const uint8_t resp[ATN_2FA_RESP_LEN])
{
    atn_2fa_slot *s;
    uint8_t expect[ATN_2FA_RESP_LEN], h[32];
    int rc;
    if (st == NULL || id == NULL || chal == NULL || resp == NULL) {
        return ATN_ERR_PARAM;
    }
    s = find_id(st, id);
    if (s == NULL) {
        return ATN_ERR_PARAM;
    }
    if (s->locked) {
        return ATN_ERR_LOCKOUT;
    }
    if (!s->have_pending || !atn_ct_equal(chal, s->pending, ATN_2FA_CHAL_LEN)) {
        s->fails++;
        if (s->fails >= ATN_2FA_FAIL_MAX) {
            s->locked = 1;
            return ATN_ERR_LOCKOUT;
        }
        return ATN_ERR_AUTH;
    }
    atn_sha3_256(chal, ATN_2FA_CHAL_LEN, h);
    if (used_contains(s, h)) {
        s->fails++;
        if (s->fails >= ATN_2FA_FAIL_MAX) {
            s->locked = 1;
            return ATN_ERR_LOCKOUT;
        }
        return ATN_ERR_NONCE;
    }
    rc = atn_2fa_respond(s->key, chal, expect);
    if (rc != ATN_OK) {
        return rc;
    }
    if (!atn_ct_equal(expect, resp, ATN_2FA_RESP_LEN)) {
        atn_memzero(expect, sizeof(expect));
        s->fails++;
        if (s->fails >= ATN_2FA_FAIL_MAX) {
            s->locked = 1;
            return ATN_ERR_LOCKOUT;
        }
        return ATN_ERR_AUTH;
    }
    atn_memzero(expect, sizeof(expect));
    if (s->used_n < ATN_2FA_REPLAY_CAP) {
        memcpy(s->used[s->used_n], h, 32);
        s->used_n++;
    } else {
        memmove(s->used[0], s->used[1], (ATN_2FA_REPLAY_CAP - 1u) * 32u);
        memcpy(s->used[ATN_2FA_REPLAY_CAP - 1u], h, 32);
    }
    s->have_pending = 0;
    s->fails = 0;
    atn_memzero(s->pending, sizeof(s->pending));
    return ATN_OK;
}
