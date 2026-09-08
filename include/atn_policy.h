/*
 * Org network + device policy (DEC-0045 / DEC-0046).
 * Hub-authoritative; phones apply over tunnel; phone stores Keystore wrap.
 *
 * Wire: ATN_TUN_DATA plaintext starts with ATN_POLICY_WIRE ('P'), then
 * key=value text. File on hub: lab/org-policy.conf (gitignored).
 */
#ifndef ATN_POLICY_H
#define ATN_POLICY_H

#include "atn_cfg.h"

#define ATN_POLICY_WIRE       0x50u /* 'P' */
#define ATN_POLICY_MAX_TEXT   1024u
#define ATN_POLICY_MAX_WIRE   (1u + ATN_POLICY_MAX_TEXT)

#define ATN_POLICY_BOOM_SILENCE_DEFAULT 30u
#define ATN_POLICY_FAIL_MAX_DEFAULT     5u
#define ATN_POLICY_PWD_MIN_DEFAULT      12u

typedef struct {
    uint32_t ver; /* monotonic; 0 = unset / defaults */
    uint8_t  diag;
    uint8_t  flush_mode;   /* ATN_CFG_FLUSH_* */
    uint8_t  wipe_armed;
    uint8_t  outage_class; /* ATN_CFG_OUTAGE_* */
    /* DEC-0046 device lock / boom */
    uint32_t boom_silence_s;   /* hub silence / no-net → BOOM */
    uint8_t  password_fail_max;
    uint8_t  biometric_allowed; /* 0 = FP+iris/face off */
    uint8_t  password_min_len;
    uint8_t  usb_data_block;    /* 1 = charge-only / no data */
    uint8_t  pwd_deny_check;    /* 1 = check deny-hash set */
} atn_policy;

void atn_policy_init(atn_policy *p);
int  atn_policy_parse(const char *text, size_t n, atn_policy *p);
int  atn_policy_load_file(const char *path, atn_policy *p);
int  atn_policy_encode(const atn_policy *p, char *out, size_t out_cap,
                       size_t *out_n);
int  atn_policy_encode_wire(const atn_policy *p, uint8_t *out, size_t out_cap,
                            size_t *out_n);
int  atn_policy_parse_wire(const uint8_t *msg, size_t n, atn_policy *p);
void atn_policy_to_cfg(const atn_policy *p, atn_cfg *c);

#endif
