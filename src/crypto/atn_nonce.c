/*
 * Module: atn_nonce.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §2.3 — partition the 96-bit nonce so the first 32 bits
 *         are unique per sender and the rest come from a counter.
 *
 * Effect: repeating a (key, nonce) pair under ChaCha20-Poly1305 leaks the
 * XOR of plaintexts (RFC 8439 §4). This sequencer makes reuse a returned
 * error instead of a silent bug. It does not replace protocol-level tracking
 * of which key this state belongs to — the caller binds one state per key.
 */

#include "atn_crypto.h"

#include <string.h>

static void store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le64(uint8_t *p, uint64_t v)
{
    store_le32(p + 0, (uint32_t)v);
    store_le32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t load_le64(const uint8_t *p)
{
    return (uint64_t)load_le32(p) | ((uint64_t)load_le32(p + 4) << 32);
}

int atn_nonce_format(uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     uint32_t sender, uint64_t counter)
{
    if (nonce == NULL) {
        return ATN_ERR_PARAM;
    }
    store_le32(nonce + 0, sender);
    store_le64(nonce + 4, counter);
    return ATN_OK;
}

int atn_nonce_parse(const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                    uint32_t *sender, uint64_t *counter)
{
    if (nonce == NULL || sender == NULL || counter == NULL) {
        return ATN_ERR_PARAM;
    }
    *sender = load_le32(nonce + 0);
    *counter = load_le64(nonce + 4);
    return ATN_OK;
}

int atn_nonce_next(atn_nonce_state *st, uint32_t sender,
                   uint8_t nonce[ATN_AEAD_NONCE_LEN])
{
    uint64_t c;

    if (st == NULL || nonce == NULL) {
        return ATN_ERR_PARAM;
    }
    if (!st->have_sender) {
        st->sender = sender;
        st->have_sender = 1;
        st->next_counter = 0;
    } else if (st->sender != sender) {
        /* A state object is bound to one sender. Mixing senders is a guess
         * that two 32-bit ids will not collide under the same key. Refuse. */
        return ATN_ERR_NONCE;
    }
    c = st->next_counter;
    if (c == UINT64_MAX) {
        return ATN_ERR_NONCE;
    }
    st->next_counter = c + 1u;
    return atn_nonce_format(nonce, sender, c);
}

int atn_nonce_accept(atn_nonce_state *st,
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN])
{
    uint32_t sender;
    uint64_t counter;
    int rc;

    if (st == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_nonce_parse(nonce, &sender, &counter);
    if (rc != ATN_OK) {
        return rc;
    }
    if (!st->have_sender) {
        st->sender = sender;
        st->have_sender = 1;
        st->last_seen = counter;
        st->have_seen = 1;
        return ATN_OK;
    }
    if (st->sender != sender) {
        return ATN_ERR_NONCE;
    }
    if (!st->have_seen) {
        st->last_seen = counter;
        st->have_seen = 1;
        return ATN_OK;
    }
    /* Strictly increasing: equal is reuse, less is replay. */
    if (counter <= st->last_seen) {
        return ATN_ERR_NONCE;
    }
    st->last_seen = counter;
    return ATN_OK;
}
