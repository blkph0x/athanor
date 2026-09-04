/*
 * Module: atn_hb.c
 * REQ:    REQ-3.3
 * Spec:   DEC-0014. HMAC-SHA-512(K, bucket64le || epoch64le || head32)
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

static void mac_input(uint64_t bucket, uint64_t epoch, const uint8_t head[32],
                      uint8_t out[8 + 8 + 32])
{
    wr64(out, bucket);
    wr64(out + 8, epoch);
    memcpy(out + 16, head, 32);
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
    return ATN_OK;
}

int atn_hb_add_peer(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN],
                    const uint8_t key[32])
{
    if (h == NULL || id == NULL || key == NULL) {
        return ATN_ERR_PARAM;
    }
    if (h->n_peers >= ATN_HB_MAX_PEERS) {
        return ATN_ERR_LEN;
    }
    memcpy(h->peer[h->n_peers].id, id, ATN_HB_ID_LEN);
    memcpy(h->peer[h->n_peers].key, key, 32);
    h->peer[h->n_peers].state = ATN_HB_LIVE;
    h->n_peers++;
    h->live_peers = h->n_peers;
    return ATN_OK;
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
    mac_input(bucket, h->epoch, h->head, in);
    return atn_hmac_sha512(h->key, 32, in, sizeof(in), mac);
}

int atn_hb_emit(atn_hb *h, uint64_t bucket)
{
    uint8_t msg[1 + 8 + 8 + 32 + ATN_HB_MAC_LEN];
    uint8_t mac[ATN_HB_MAC_LEN];
    int rc;
    if (h == NULL) {
        return ATN_ERR_PARAM;
    }
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

int atn_hb_ingest(atn_hb *h, const uint8_t *msg, size_t n)
{
    uint64_t bucket, epoch;
    uint8_t expect[ATN_HB_MAC_LEN], in[8 + 8 + 32];
    unsigned i;
    int rc;
    if (h == NULL || msg == NULL) {
        return ATN_ERR_PARAM;
    }
    if (h->state == ATN_HB_DEAD) {
        return ATN_ERR_STATE;
    }
    if (n != 1u + 8u + 8u + 32u + ATN_HB_MAC_LEN || msg[0] != ATN_HB_WIRE_H) {
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
        rc = atn_hmac_sha512(h->peer[i].key, 32, in, sizeof(in), expect);
        if (rc != ATN_OK) {
            return rc;
        }
        if (atn_ct_equal(expect, msg + 49, ATN_HB_MAC_LEN)) {
            if (h->peer[i].last_bucket != 0 &&
                bucket + 1u < h->peer[i].last_bucket) {
                return ATN_ERR_NONCE; /* too old */
            }
            h->peer[i].last_bucket = bucket;
            h->peer[i].misses = 0;
            if (h->peer[i].state != ATN_HB_DEAD) {
                h->peer[i].state = ATN_HB_LIVE;
            }
            atn_memzero(expect, sizeof(expect));
            return ATN_OK;
        }
        atn_memzero(expect, sizeof(expect));
    }
    return ATN_ERR_AUTH; /* no peer key matched — forged or unknown */
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
    return atn_hb_ingest(h, buf, n);
}

int atn_hb_tick(atn_hb *h, uint64_t bucket)
{
    unsigned i, heard = 0, live = 0;
    if (h == NULL) {
        return ATN_ERR_PARAM;
    }
    if (h->state == ATN_HB_DEAD) {
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
        if (h->peer[i].misses >= ATN_HB_N + ATN_HB_M) {
            h->peer[i].state = ATN_HB_DEAD;
        } else if (h->peer[i].misses >= ATN_HB_N) {
            h->peer[i].state = ATN_HB_UNTRUSTED;
        } else {
            live++;
        }
    }
    h->live_peers = live;
    if (heard == 0 && h->n_peers > 0) {
        h->misses++;
        if (h->misses >= ATN_HB_N + ATN_HB_M) {
            atn_memzero(h->key, 32);
            h->state = ATN_HB_DEAD;
            return ATN_ERR_STATE;
        }
        if (h->misses >= ATN_HB_N) {
            h->state = ATN_HB_UNTRUSTED;
        }
    } else {
        h->misses = 0;
        if (h->state == ATN_HB_UNTRUSTED) {
            h->state = ATN_HB_LIVE;
        }
    }
    return ATN_OK;
}

int atn_hb_peer_state(const atn_hb *h, const uint8_t id[ATN_HB_ID_LEN])
{
    unsigned i;
    if (h == NULL || id == NULL) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < h->n_peers; i++) {
        if (memcmp(h->peer[i].id, id, ATN_HB_ID_LEN) == 0) {
            return h->peer[i].state;
        }
    }
    return ATN_ERR_STATE;
}

void atn_hb_wipe(atn_hb *h)
{
    if (h == NULL) {
        return;
    }
    atn_memzero(h->key, 32);
    atn_memzero(h->peer, sizeof(h->peer));
    h->state = ATN_HB_DEAD;
}
