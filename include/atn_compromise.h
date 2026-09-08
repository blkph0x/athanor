/*
 * Compromise vote + boom command (DEC-0047).
 * Hub-authoritative file lab/compromise-vote.conf; wire ATN_TUN_DATA 'C'+kv.
 */
#ifndef ATN_COMPROMISE_H
#define ATN_COMPROMISE_H

#include "atn_crypto.h"

#define ATN_COMP_WIRE        0x43u /* 'C' */
#define ATN_COMP_MAX_TEXT   1024u
#define ATN_COMP_MAX_WIRE   (1u + ATN_COMP_MAX_TEXT)
#define ATN_COMP_LABEL_MAX   64u

#define ATN_COMP_STATE_NONE        0u
#define ATN_COMP_STATE_OPEN        1u
#define ATN_COMP_STATE_BOOM_PENDING 2u
#define ATN_COMP_STATE_DONE        3u
#define ATN_COMP_STATE_CLEARED     4u

#define ATN_COMP_ACT_OPEN     1u
#define ATN_COMP_ACT_VOTE_YES 2u
#define ATN_COMP_ACT_VOTE_NO  3u
#define ATN_COMP_ACT_BOOM     4u
#define ATN_COMP_ACT_CLEAR    5u

typedef struct {
    uint32_t vote_id;
    char     target_label[ATN_COMP_LABEL_MAX];
    uint32_t opened_unix;
    uint32_t timeout_s; /* default 300; expire → boom even with zero votes */
    uint32_t yes;
    uint32_t no;
    uint32_t quorum; /* YES votes needed; default 1 */
    uint8_t  state;  /* ATN_COMP_STATE_* */
    uint8_t  action; /* wire action ATN_COMP_ACT_* (transient) */
} atn_compromise;

void atn_compromise_init(atn_compromise *c);
int  atn_compromise_parse(const char *text, size_t n, atn_compromise *c);
int  atn_compromise_load_file(const char *path, atn_compromise *c);
int  atn_compromise_save_file(const char *path, const atn_compromise *c);
int  atn_compromise_encode(const atn_compromise *c, char *out, size_t out_cap,
                           size_t *out_n);
int  atn_compromise_encode_wire(const atn_compromise *c, uint8_t act,
                                uint8_t *out, size_t out_cap, size_t *out_n);
int  atn_compromise_parse_wire(const uint8_t *msg, size_t n,
                               atn_compromise *c);
/* Returns 1 if open vote timed out (caller should mark boom_pending). */
int  atn_compromise_timed_out(const atn_compromise *c, uint32_t now_unix);
/* Returns 1 if yes >= quorum. */
int  atn_compromise_quorum_met(const atn_compromise *c);

#endif
