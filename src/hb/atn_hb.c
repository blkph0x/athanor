/*
 * Module: atn_hb.c
 * REQ:    REQ-3.3
 * Spec:   DEC-0014 / DEC-0025. HMAC-SHA-512; WARN+vote before wipe.
 */

#include "atn_hb.h"

#include <string.h>

static void wr64(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static uint64_t rd64(const uint8_t *p)
{
    unsigned i;
    uint64_t v = 0;
    for (i = 0; i < 8u; i++) {
        v |= ((uint64_t)p[i]) << (8u * i);
    }
    return v;
}

static int mac_match(atn_hb *h, const uint8_t *in, size_t inlen,
                     const uint8_t *mac)
{
    uint8_t expect[ATN_HB_MAC_LEN];
    unsigned i;
    int rc, hit = 0;
    for (i = 0; i < h->n_peers; i++) {
        rc = atn_hmac_sha512(h->peer[i].key, 32, in, inlen, expect);
        if (rc != ATN_OK) {
            return rc;
        }
        if (atn_ct_equal(expect, mac, ATN_HB_MAC_LEN)) {
            hit = 1;
        }
        atn_memzero(expect, sizeof(expect));
        if (hit) {
            return ATN_OK;
        }
    }
    return ATN_ERR_AUTH;
}

int atn_hb_init(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN],
                const uint8_t key[32], uint64_t epoch,
                const uint8_t head[ATN_HB_HEAD_LEN], atn_tun *tun)
{
    if (h == NULL || id == NULL || key == NULL || head == NULL) {
        return ATN_ERR_PARAM;
    }
    memset(h, 0, sizeof(*h));
    memcpy(h->id, id, ATN_HB_ID_LEN);
    memcpy(h->key, key, 32);
    memcpy(h->head, head, ATN_HB_HEAD_LEN);
    h->epoch = epoch;
    h->state = ATN_HB_LIVE;
    h->tun = tun;
    atn_lock_init(&h->lock);
    h->lock_on = 1;
    return ATN_OK;
}

int atn_hb_add_peer(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN],
                    const uint8_t key[32])
{
    if (h == NULL || id == NULL || key == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    if (h->n_peers >= ATN_HB_MAX_PEERS) {
        atn_lock_release(&h->lock);
        return ATN_ERR_LEN;
    }
    memcpy(h->peer[h->n_peers].id, id, ATN_HB_ID_LEN);
    memcpy(h->peer[h->n_peers].key, key, 32);
    h->peer[h->n_peers].state = ATN_HB_LIVE;
    h->n_peers++;
    h->live_peers = h->n_peers;
    atn_lock_release(&h->lock);
    return ATN_OK;
}

int atn_hb_set_witness(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN])
{
    unsigned i;
    if (h == NULL || id == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    for (i = 0; i < h->n_peers; i++) {
        if (memcmp(h->peer[i].id, id, ATN_HB_ID_LEN) == 0) {
            memcpy(h->witness, id, ATN_HB_ID_LEN);
            h->have_witness = 1;
            atn_lock_release(&h->lock);
            return ATN_OK;
        }
    }
    atn_lock_release(&h->lock);
    return ATN_ERR_STATE;
}

int atn_hb_token(const atn_hb *h, uint64_t bucket, uint8_t mac[ATN_HB_MAC_LEN])
{
    uint8_t in[8 + 8 + 32];
    if (h == NULL || mac == NULL) {
        return ATN_ERR_PARAM;
    }
    if (h->state == ATN_HB_DEAD) {
        return ATN_ERR_STATE;
    }
    wr64(in, bucket);
    wr64(in + 8, h->epoch);
    memcpy(in + 16, h->head, 32);
    return atn_hmac_sha512(h->key, 32, in, sizeof(in), mac);
}

static int emit_h_u(atn_hb *h, uint64_t bucket)
{
    uint8_t msg[1 + 8 + 8 + 32 + ATN_HB_MAC_LEN];
    uint8_t mac[ATN_HB_MAC_LEN];
    int rc;
    rc = atn_hb_token(h, bucket, mac);
    if (rc != ATN_OK) {
        return rc;
    }
    msg[0] = ATN_HB_WIRE_H;
    wr64(msg + 1, bucket);
    wr64(msg + 9, h->epoch);
    memcpy(msg + 17, h->head, 32);
    memcpy(msg + 49, mac, ATN_HB_MAC_LEN);
    h->last_bucket = bucket;
    if (h->tun == NULL) {
        return ATN_OK;
    }
    return atn_tun_send(h->tun, msg, sizeof(msg));
}

int atn_hb_emit(atn_hb *h, uint64_t bucket)
{
    int rc;
    if (h == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    rc = emit_h_u(h, bucket);
    atn_lock_release(&h->lock);
    return rc;
}

static int emit_w_u(atn_hb *h, uint64_t bucket, const uint8_t suspect[8])
{
    uint8_t in[8 + 8 + 8], msg[1 + 8 + 8 + 8 + ATN_HB_MAC_LEN];
    uint8_t mac[ATN_HB_MAC_LEN];
    int rc;
    wr64(in, bucket);
    wr64(in + 8, h->epoch);
    memcpy(in + 16, suspect, 8);
    rc = atn_hmac_sha512(h->key, 32, in, sizeof(in), mac);
    if (rc != ATN_OK) {
        return rc;
    }
    msg[0] = ATN_HB_WIRE_W;
    memcpy(msg + 1, in, 24);
    memcpy(msg + 25, mac, ATN_HB_MAC_LEN);
    if (h->tun == NULL) {
        return ATN_OK;
    }
    return atn_tun_send(h->tun, msg, sizeof(msg));
}

int atn_hb_warn(atn_hb *h, uint64_t bucket, const uint8_t suspect[ATN_HB_ID_LEN])
{
    int rc;
    if (h == NULL || suspect == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    rc = emit_w_u(h, bucket, suspect);
    atn_lock_release(&h->lock);
    return rc;
}

int atn_hb_vote(atn_hb *h, uint64_t bucket,
                const uint8_t suspect[ATN_HB_ID_LEN], unsigned vote)
{
    uint8_t in[8 + 8 + 8 + 1], msg[1 + 8 + 8 + 8 + 1 + ATN_HB_MAC_LEN];
    uint8_t mac[ATN_HB_MAC_LEN];
    int rc;
    if (h == NULL || suspect == NULL ||
        (vote != ATN_HB_VOTE_HOLD && vote != ATN_HB_VOTE_WIPE)) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    if (vote == ATN_HB_VOTE_HOLD) {
        h->hold_votes++;
    } else {
        h->wipe_votes++;
    }
    wr64(in, bucket);
    wr64(in + 8, h->epoch);
    memcpy(in + 16, suspect, 8);
    in[24] = (uint8_t)vote;
    rc = atn_hmac_sha512(h->key, 32, in, sizeof(in), mac);
    if (rc != ATN_OK) {
        atn_lock_release(&h->lock);
        return rc;
    }
    msg[0] = ATN_HB_WIRE_V;
    memcpy(msg + 1, in, 25);
    memcpy(msg + 26, mac, ATN_HB_MAC_LEN);
    if (h->tun != NULL) {
        rc = atn_tun_send(h->tun, msg, sizeof(msg));
    }
    atn_lock_release(&h->lock);
    return rc;
}

static int ingest_h(atn_hb *h, const uint8_t *msg, size_t n)
{
    uint64_t bucket, epoch;
    uint8_t in[8 + 8 + 32];
    unsigned i;
    int rc;
    if (n != 1u + 8u + 8u + 32u + ATN_HB_MAC_LEN) {
        return ATN_ERR_PARAM;
    }
    bucket = rd64(msg + 1);
    epoch = rd64(msg + 9);
    if (epoch != h->epoch) {
        return ATN_ERR_STATE;
    }
    memcpy(in, msg + 1, 8);
    memcpy(in + 8, msg + 9, 8);
    memcpy(in + 16, msg + 17, 32);
    for (i = 0; i < h->n_peers; i++) {
        uint8_t expect[ATN_HB_MAC_LEN];
        rc = atn_hmac_sha512(h->peer[i].key, 32, in, sizeof(in), expect);
        if (rc != ATN_OK) {
            return rc;
        }
        if (!atn_ct_equal(expect, msg + 49, ATN_HB_MAC_LEN)) {
            atn_memzero(expect, sizeof(expect));
            continue;
        }
        atn_memzero(expect, sizeof(expect));
        if (h->peer[i].last_bucket != 0 &&
            bucket + 1u < h->peer[i].last_bucket) {
            return ATN_ERR_NONCE;
        }
        h->peer[i].last_bucket = bucket;
        h->peer[i].misses = 0;
        if (h->peer[i].state != ATN_HB_DEAD) {
            h->peer[i].state = ATN_HB_LIVE;
        }
        /* Retrieve: a live token during grace restores us. */
        if (h->state == ATN_HB_UNTRUSTED) {
            h->misses = 0;
            h->grace = 0;
            h->state = ATN_HB_LIVE;
            h->hold_votes = 0;
            h->wipe_votes = 0;
        }
        return ATN_OK;
    }
    return ATN_ERR_AUTH;
}

static int ingest_w(atn_hb *h, const uint8_t *msg, size_t n, uint64_t *emit_b)
{
    uint8_t in[24];
    int rc;
    if (n != 1u + 24u + ATN_HB_MAC_LEN) {
        return ATN_ERR_PARAM;
    }
    if (rd64(msg + 9) != h->epoch) {
        return ATN_ERR_STATE;
    }
    memcpy(in, msg + 1, 24);
    rc = mac_match(h, in, 24, msg + 25);
    if (rc != ATN_OK) {
        return rc;
    }
    /* Automatic retrieve: reply with our heartbeat. */
    *emit_b = rd64(msg + 1);
    return ATN_OK;
}

static int ingest_v(atn_hb *h, const uint8_t *msg, size_t n)
{
    uint8_t in[25];
    int rc;
    unsigned vote;
    if (n != 1u + 25u + ATN_HB_MAC_LEN) {
        return ATN_ERR_PARAM;
    }
    if (rd64(msg + 9) != h->epoch) {
        return ATN_ERR_STATE;
    }
    memcpy(in, msg + 1, 25);
    rc = mac_match(h, in, 25, msg + 26);
    if (rc != ATN_OK) {
        return rc;
    }
    vote = msg[25];
    if (vote == ATN_HB_VOTE_HOLD) {
        h->hold_votes++;
    } else if (vote == ATN_HB_VOTE_WIPE) {
        h->wipe_votes++;
    } else {
        return ATN_ERR_PARAM;
    }
    return ATN_OK;
}

int atn_hb_ingest(atn_hb *h, const uint8_t *msg, size_t n)
{
    int rc;
    uint64_t emit_b = 0;
    int do_emit = 0;
    if (h == NULL || msg == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    if (h->state == ATN_HB_DEAD) {
        atn_lock_release(&h->lock);
        return ATN_ERR_STATE;
    }
    if (msg[0] == ATN_HB_WIRE_H) {
        rc = ingest_h(h, msg, n);
    } else if (msg[0] == ATN_HB_WIRE_W) {
        rc = ingest_w(h, msg, n, &emit_b);
        do_emit = (rc == ATN_OK);
    } else if (msg[0] == ATN_HB_WIRE_V) {
        rc = ingest_v(h, msg, n);
    } else {
        rc = ATN_ERR_PARAM;
    }
    if (do_emit) {
        (void)emit_h_u(h, emit_b);
    }
    atn_lock_release(&h->lock);
    return rc;
}

int atn_hb_pump(atn_hb *h, int timeout_ms)
{
    uint8_t buf[ATN_TUN_MAX_PT];
    size_t n = 0;
    int rc;
    if (h == NULL || h->tun == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_recv_data(h->tun, buf, &n, sizeof(buf), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = atn_hb_ingest(h, buf, n);
    atn_memzero(buf, n);
    return rc;
}

int atn_hb_tick(atn_hb *h, uint64_t bucket)
{
    unsigned i, heard = 0, live = 0;
    int rc = ATN_OK;
    if (h == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_lock_acquire(&h->lock);
    if (h->state == ATN_HB_DEAD) {
        atn_lock_release(&h->lock);
        return ATN_ERR_STATE;
    }
    for (i = 0; i < h->n_peers; i++) {
        if (h->peer[i].state == ATN_HB_DEAD) {
            continue;
        }
        if (h->peer[i].last_bucket == bucket) {
            heard++;
            live++;
            continue;
        }
        h->peer[i].misses++;
        if (h->peer[i].misses >= ATN_HB_N + ATN_HB_G) {
            h->peer[i].state = ATN_HB_DEAD;
        } else if (h->peer[i].misses >= ATN_HB_N) {
            h->peer[i].state = ATN_HB_UNTRUSTED;
            (void)emit_w_u(h, bucket, h->peer[i].id);
        } else {
            live++;
        }
    }
    h->live_peers = live;
    if (heard == 0 && h->n_peers > 0) {
        if (h->state == ATN_HB_LIVE) {
            h->misses++;
            if (h->misses >= ATN_HB_N) {
                h->state = ATN_HB_UNTRUSTED;
                h->grace = ATN_HB_G;
                (void)emit_w_u(h, bucket, h->id);
                (void)emit_h_u(h, bucket);
            }
        } else if (h->state == ATN_HB_UNTRUSTED) {
            (void)emit_w_u(h, bucket, h->id);
            (void)emit_h_u(h, bucket);
            if (h->grace > 0) {
                h->grace--;
            }
            if (h->grace == 0) {
                if (h->hold_votes > 0) {
                    h->misses = 0;
                    h->state = ATN_HB_LIVE;
                    h->hold_votes = 0;
                    h->wipe_votes = 0;
                } else {
                    atn_memzero(h->key, 32);
                    h->state = ATN_HB_DEAD;
                    rc = ATN_ERR_STATE;
                }
            }
        }
    } else {
        h->misses = 0;
        if (h->state == ATN_HB_UNTRUSTED) {
            h->state = ATN_HB_LIVE;
            h->grace = 0;
        }
    }
    atn_lock_release(&h->lock);
    return rc;
}

int atn_hb_peer_state(const atn_hb *h, const uint8_t id[ATN_HB_ID_LEN])
{
    unsigned i;
    int st = ATN_ERR_STATE;
    if (h == NULL || id == NULL) {
        return ATN_ERR_PARAM;
    }
    /* lock is mutable; cast for const API */
    atn_lock_acquire((atn_lock *)&h->lock);
    for (i = 0; i < h->n_peers; i++) {
        if (memcmp(h->peer[i].id, id, ATN_HB_ID_LEN) == 0) {
            st = h->peer[i].state;
            break;
        }
    }
    atn_lock_release((atn_lock *)&h->lock);
    return st;
}

void atn_hb_wipe(atn_hb *h)
{
    if (h == NULL) {
        return;
    }
    if (h->lock_on) {
        atn_lock_acquire(&h->lock);
        atn_memzero(h->key, 32);
        atn_memzero(h->peer, sizeof(h->peer));
        h->state = ATN_HB_DEAD;
        atn_lock_release(&h->lock);
        atn_lock_fini(&h->lock);
        h->lock_on = 0;
    } else {
        atn_memzero(h->key, 32);
        atn_memzero(h->peer, sizeof(h->peer));
        h->state = ATN_HB_DEAD;
    }
}
