/*
 * Athanor 2FA challenge-response (REQ-1.3, DEC-0008).
 * Response = HMAC-SHA-512(K_device, challenge ‖ "atn-2fa-v1")
 * Challenges are one-shot. Five failures lock the slot.
 */
#ifndef ATN_2FA_H
#define ATN_2FA_H

#include "atn_crypto.h"

#define ATN_2FA_CHAL_LEN    32u
#define ATN_2FA_RESP_LEN    ATN_HMAC_SHA512_LEN
#define ATN_2FA_KEY_LEN     32u
#define ATN_2FA_ID_LEN      32u
#define ATN_2FA_FAIL_MAX    5u
#define ATN_2FA_REPLAY_CAP  64u
#define ATN_2FA_SLOTS       16u

typedef struct {
    uint8_t  id[ATN_2FA_ID_LEN];
    uint8_t  key[ATN_2FA_KEY_LEN];
    uint8_t  in_use;
    uint8_t  locked;
    uint32_t fails;
    uint8_t  used[ATN_2FA_REPLAY_CAP][32]; /* SHA3-256 of consumed challenges */
    uint32_t used_n;
    uint8_t  pending[ATN_2FA_CHAL_LEN];
    uint8_t  have_pending;
} atn_2fa_slot;

typedef struct {
    atn_2fa_slot slots[ATN_2FA_SLOTS];
} atn_2fa_store;

void atn_2fa_store_init(atn_2fa_store *st);
int  atn_2fa_enroll(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                    uint8_t key_out[ATN_2FA_KEY_LEN]);
int  atn_2fa_revoke(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN]);
int  atn_2fa_challenge(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                       uint8_t chal[ATN_2FA_CHAL_LEN]);
int  atn_2fa_respond(const uint8_t key[ATN_2FA_KEY_LEN],
                     const uint8_t chal[ATN_2FA_CHAL_LEN],
                     uint8_t resp[ATN_2FA_RESP_LEN]);
int  atn_2fa_verify(atn_2fa_store *st, const uint8_t id[ATN_2FA_ID_LEN],
                    const uint8_t chal[ATN_2FA_CHAL_LEN],
                    const uint8_t resp[ATN_2FA_RESP_LEN]);

#endif
