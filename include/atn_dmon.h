/*
 * Native daemon session (REQ-4.1 / 4.4). Spec: DEC-0015..0017, 0025, 0027,
 * 0028, 0029, 0031.
 *
 * Purpose:  Hold 2FA + heartbeat + device keys in one object we can flush.
 * Policy:   Flush zeros OUR buffers only (unless diag log_only). TIMA enable
 *           APIs are dead on Android 12+; hardware keys live in Android
 *           Keystore (Java). This struct is the process-RAM copy.
 */
#ifndef ATN_DMON_H
#define ATN_DMON_H

#include "atn_2fa.h"
#include "atn_cfg.h"
#include "atn_hb.h"
#include "atn_tun.h"

#define ATN_DMON_HB_BUCKET_SEC 60u /* DEC-0017: one hb bucket per minute */

/* DEC-0031: pump+retry rounds per hub before advancing the failover list. */
#define ATN_DMON_HUB_HS_ATTEMPTS 3u

typedef struct {
    atn_2fa_store twofa;
    atn_hb        hb;
    atn_tun       tun;
    uint8_t       device_key[32];
    uint8_t       cluster[32];
    uint8_t       loaded;
    uint8_t       hb_ready;
    uint8_t       tun_ready;
    /* DEC-0027 / 0029 policy (defaults = production ZEROIZE / normal). */
    uint8_t       diag;
    uint8_t       flush_mode;
    uint8_t       wipe_armed;
    uint8_t       outage_class;
    unsigned      flush_log_count;
    unsigned      hub_idx; /* DEC-0031: current hub (0 = peer_*) */
} atn_dmon;

void atn_dmon_init(atn_dmon *d);
int  atn_dmon_load(atn_dmon *d, const uint8_t device_key[32],
                   const uint8_t cluster[32]);
int  atn_dmon_require(const atn_dmon *d);
void atn_dmon_flush(atn_dmon *d);

/*
 * Purpose:  Apply diag/outage policy from a parsed conf (DEC-0027/0029).
 * Spec:     Does not require ready(); copies policy fields only.
 */
int  atn_dmon_apply_cfg(atn_dmon *d, const atn_cfg *c);

int  atn_dmon_hb_init(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                      uint64_t epoch, const uint8_t head[ATN_HB_HEAD_LEN]);
int  atn_dmon_hb_add_peer(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                          const uint8_t key[32]);
int  atn_dmon_hb_ingest(atn_dmon *d, const uint8_t *msg, size_t n);
int  atn_dmon_hb_emit(atn_dmon *d, uint64_t bucket);
int  atn_dmon_hb_tick(atn_dmon *d, uint64_t bucket);
int  atn_dmon_hb_state(const atn_dmon *d);

int  atn_dmon_2fa_enroll(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                         uint8_t key_out[ATN_2FA_KEY_LEN]);
int  atn_dmon_2fa_challenge(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                            uint8_t chal[ATN_2FA_CHAL_LEN]);
int  atn_dmon_2fa_verify(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                         const uint8_t chal[ATN_2FA_CHAL_LEN],
                         const uint8_t resp[ATN_2FA_RESP_LEN]);

int  atn_dmon_tun_initiator(atn_dmon *d,
                            const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN]);
int  atn_dmon_tun_responder(atn_dmon *d,
                            const uint8_t own_dk[ATN_MLKEM1024_DK_LEN]);
int  atn_dmon_tun_bind(atn_dmon *d, uint16_t port);
int  atn_dmon_tun_bind_any(atn_dmon *d, uint16_t port);
int  atn_dmon_tun_set_peer(atn_dmon *d, uint32_t ipv4_host, uint16_t port);
int  atn_dmon_tun_hs_send(atn_dmon *d);
int  atn_dmon_tun_hs_retry(atn_dmon *d);
int  atn_dmon_tun_keepalive(atn_dmon *d);
int  atn_dmon_tun_pump(atn_dmon *d, int timeout_ms);
int  atn_dmon_tun_send(atn_dmon *d, const uint8_t *pt, size_t n);
int  atn_dmon_tun_recv(atn_dmon *d, uint8_t *pt, size_t *n, size_t max,
                       int timeout_ms);
int  atn_dmon_tun_state(const atn_dmon *d);
uint16_t atn_dmon_tun_port(const atn_dmon *d);

/*
 * Purpose:  Current hub index after connect/failover (0 = primary).
 * Spec:     DEC-0031.
 */
unsigned atn_dmon_hub_idx(const atn_dmon *d);

/*
 * Purpose:  Wipe prior tunnel (session keys stay) and HS to hub i.
 * Spec:     DEC-0028 / 0031. Clears old pin via wipe; set_peer pins hub i.
 */
int  atn_dmon_tun_connect_hub(atn_dmon *d, const atn_cfg *c, unsigned hub_i);

/*
 * Purpose:  Ordered hub failover until ESTABLISHED (D-08 wire path).
 * Spec:     DEC-0031. Does not flush device keys on failure.
 */
int  atn_dmon_tun_failover(atn_dmon *d, const atn_cfg *c, unsigned start_i,
                           int timeout_ms);

#endif
