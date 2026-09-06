/*
 * Athanor heartbeat mesh (REQ-3.3). Spec: DEC-0014, DEC-0025.
 *
 * Token = HMAC-SHA-512(K, bucket||epoch||head). Misses: UNTRUSTED, grace,
 * vote HOLD to cancel wipe. Witness retrieve is an immediate H emit.
 */
#ifndef ATN_HB_H
#define ATN_HB_H

#include "atn_tun.h"
#include "atn_sync.h"

#define ATN_HB_MAX_PEERS  16u
#define ATN_HB_ID_LEN     8u
#define ATN_HB_HEAD_LEN   32u
#define ATN_HB_MAC_LEN    ATN_HMAC_SHA512_LEN
#define ATN_HB_N          3u  /* misses → UNTRUSTED + WARN */
#define ATN_HB_G          3u  /* grace buckets; wipe if no HOLD */
#define ATN_HB_M          ATN_HB_G /* alias: old name was M */

#define ATN_HB_LIVE      0
#define ATN_HB_UNTRUSTED 1
#define ATN_HB_DEAD      2

#define ATN_HB_WIRE_H    0x48u /* 'H' heartbeat */
#define ATN_HB_WIRE_W    0x57u /* 'W' warn all nodes */
#define ATN_HB_WIRE_V    0x56u /* 'V' vote hold/wipe */

#define ATN_HB_VOTE_HOLD 0u
#define ATN_HB_VOTE_WIPE 1u

typedef struct {
    uint8_t  id[ATN_HB_ID_LEN];
    uint8_t  key[32];
    uint8_t  head[ATN_HB_HEAD_LEN];
    uint64_t epoch;
    uint64_t last_bucket;
    int      state;
    unsigned misses;
    unsigned live_peers;
    unsigned grace;
    unsigned hold_votes;
    unsigned wipe_votes;
    uint8_t  witness[ATN_HB_ID_LEN];
    uint8_t  have_witness;
    struct {
        uint8_t  id[ATN_HB_ID_LEN];
        uint8_t  key[32];
        uint64_t last_bucket;
        unsigned misses;
        int      state;
    } peer[ATN_HB_MAX_PEERS];
    unsigned n_peers;
    atn_tun *tun;
    atn_lock lock;
    uint8_t  lock_on;
} atn_hb;

int atn_hb_init(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN],
                const uint8_t key[32], uint64_t epoch,
                const uint8_t head[ATN_HB_HEAD_LEN], atn_tun *tun);
int atn_hb_add_peer(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN],
                    const uint8_t key[32]);
int atn_hb_set_witness(atn_hb *h, const uint8_t id[ATN_HB_ID_LEN]);
int atn_hb_token(const atn_hb *h, uint64_t bucket,
                 uint8_t mac[ATN_HB_MAC_LEN]);
int atn_hb_emit(atn_hb *h, uint64_t bucket);
int atn_hb_warn(atn_hb *h, uint64_t bucket,
                const uint8_t suspect[ATN_HB_ID_LEN]);
int atn_hb_vote(atn_hb *h, uint64_t bucket,
                const uint8_t suspect[ATN_HB_ID_LEN], unsigned vote);
int atn_hb_ingest(atn_hb *h, const uint8_t *msg, size_t n);
int atn_hb_pump(atn_hb *h, int timeout_ms);
int atn_hb_tick(atn_hb *h, uint64_t bucket);
int atn_hb_peer_state(const atn_hb *h, const uint8_t id[ATN_HB_ID_LEN]);
void atn_hb_wipe(atn_hb *h);

#endif
