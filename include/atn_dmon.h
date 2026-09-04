/*
 * Native daemon session (REQ-4.1 / 4.4). Spec: DEC-0015, DEC-0016, DEC-0017.
 *
 * Purpose:  Hold 2FA + heartbeat + device keys in one object we can flush.
 * Policy:   Flush zeros OUR buffers only. TIMA enable APIs are dead on
 *           Android 12+ (Samsung deprecation, API 33); hardware keys live
 *           in Android Keystore (Java). This struct is the process-RAM copy.
 * Triggers: hb UNTRUSTED/DEAD (DEC-0017), 2FA lockout, Java password-fail K.
 */
#ifndef ATN_DMON_H
#define ATN_DMON_H

#include "atn_2fa.h"
#include "atn_hb.h"

#define ATN_DMON_HB_BUCKET_SEC 60u /* DEC-0017: one hb bucket per minute */

typedef struct {
    atn_2fa_store twofa;
    atn_hb        hb;
    uint8_t       device_key[32];
    uint8_t       cluster[32];
    uint8_t       loaded;
    uint8_t       hb_ready;
} atn_dmon;

void atn_dmon_init(atn_dmon *d);
int  atn_dmon_load(atn_dmon *d, const uint8_t device_key[32],
                   const uint8_t cluster[32]);
int  atn_dmon_require(const atn_dmon *d);
void atn_dmon_flush(atn_dmon *d);

int  atn_dmon_hb_init(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                      uint64_t epoch, const uint8_t head[ATN_HB_HEAD_LEN]);
int  atn_dmon_hb_add_peer(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                          const uint8_t key[32]);
int  atn_dmon_hb_ingest(atn_dmon *d, const uint8_t *msg, size_t n);
int  atn_dmon_hb_tick(atn_dmon *d, uint64_t bucket);
int  atn_dmon_hb_state(const atn_dmon *d);

int  atn_dmon_2fa_enroll(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                         uint8_t key_out[ATN_2FA_KEY_LEN]);
int  atn_dmon_2fa_challenge(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                            uint8_t chal[ATN_2FA_CHAL_LEN]);
int  atn_dmon_2fa_verify(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                         const uint8_t chal[ATN_2FA_CHAL_LEN],
                         const uint8_t resp[ATN_2FA_RESP_LEN]);

#endif
