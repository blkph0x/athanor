/*
 * Athanor UDP tunnel (REQ-1.2). Spec: docs/TUNNEL.md, DEC-0007 / 0035.
 */
#ifndef ATN_TUN_H
#define ATN_TUN_H

#include "atn_crypto.h"

#define ATN_TUN_VERSION     1u
#define ATN_TUN_HDR_LEN     16u
#define ATN_TUN_MAX_PT      1024u
#define ATN_TUN_MAX_DG      (ATN_TUN_HDR_LEN + ATN_MLKEM1024_CT_LEN)
/* DEC-0044: cellular CLAT MTU — KEM CT split into this many payload bytes. */
#define ATN_TUN_HS_CHUNK    512u
#define ATN_TUN_HS_NCHUNKS  \
    ((ATN_MLKEM1024_CT_LEN + ATN_TUN_HS_CHUNK - 1u) / ATN_TUN_HS_CHUNK)

#define ATN_TUN_HS_INIT     1u
#define ATN_TUN_HS_ACK      2u
#define ATN_TUN_DATA        3u
#define ATN_TUN_KA          4u
#define ATN_TUN_CLOSE       5u
#define ATN_TUN_REKEY_INIT  6u /* DEC-0035 */
#define ATN_TUN_REKEY_ACK   7u

#define ATN_TUN_CLOSED      0
#define ATN_TUN_HANDSHAKE   1
#define ATN_TUN_ESTABLISHED 2

typedef struct {
    int      state;
    int      initiator;
    uint16_t local_port;
    uint8_t  k_ack[32];
    uint8_t  k_send[32];
    uint8_t  k_recv[32];
    uint64_t send_seq;
    uint64_t recv_max;
    uint64_t recv_win;       /* bit i set => seq (recv_max-i) already seen */
    uint8_t  peer_ek[ATN_MLKEM1024_EK_LEN];
    uint8_t  own_dk[ATN_MLKEM1024_DK_LEN];
    uint8_t  kem_ct[ATN_MLKEM1024_CT_LEN];
    uint8_t  confirm[32];
    /* DEC-0035: staged keys while waiting for REKEY_ACK (initiator). */
    uint8_t  rk_ack[32];
    uint8_t  rk_send[32];
    uint8_t  rk_recv[32];
    uint8_t  rk_confirm[32];
    uint8_t  rekey_pending;
    /* OS socket handle stored as intptr-sized int; INVALID = -1 */
    intptr_t sock;
    uint32_t peer_addr;      /* IPv4 host order */
    uint16_t peer_port;      /* host order */
    int      have_peer;
    uint8_t  last_wire[ATN_TUN_MAX_DG];
    size_t   last_wire_len;
    /* DEC-0044: reassemble fragmented HS_INIT / REKEY_INIT */
    uint8_t  hs_asm[ATN_MLKEM1024_CT_LEN];
    uint8_t  hs_asm_bits;   /* bit i = chunk i received */
    uint8_t  hs_asm_type;   /* ATN_TUN_HS_INIT or REKEY_INIT while assembling */
} atn_tun;

int atn_net_init(void);
void atn_net_fini(void);

int atn_tun_init_initiator(atn_tun *t, const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN]);
int atn_tun_init_responder(atn_tun *t, const uint8_t own_dk[ATN_MLKEM1024_DK_LEN]);
int atn_tun_bind(atn_tun *t, uint16_t port);           /* loopback, 0 = ephemeral */
int atn_tun_bind_any(atn_tun *t, uint16_t port);       /* INADDR_ANY, DEC-0021 */
int atn_tun_set_peer(atn_tun *t, uint32_t ipv4_host, uint16_t port);
int atn_tun_hs_send_init(atn_tun *t);
int atn_tun_hs_retry(atn_tun *t);                      /* resend HS_INIT, DEC-0022 */
int atn_tun_rekey_send(atn_tun *t);                    /* initiator REKEY_INIT, DEC-0035 */
int atn_tun_pump(atn_tun *t, int timeout_ms);          /* recv one datagram */
int atn_tun_send(atn_tun *t, const uint8_t *pt, size_t n);
int atn_tun_recv_data(atn_tun *t, uint8_t *pt, size_t *n, size_t max, int timeout_ms);
int atn_tun_keepalive(atn_tun *t);
int atn_tun_close(atn_tun *t);
void atn_tun_wipe(atn_tun *t);
/* Test hook: retransmit the last datagram unchanged (replay-window proof). */
int atn_tun_resend_last(atn_tun *t);
/* Test hook: send 16 unauthenticated bytes at t's IPv4 loopback port. */
int atn_tun_test_stray(const atn_tun *t);

#endif
