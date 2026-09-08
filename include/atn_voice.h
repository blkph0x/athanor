/*
 * Secure voice (DEC-0050). Family 'A' on ATN_TUN_DATA.
 * Primary: P2P PQ/AEAD tunnel (E2E). Fallback: nested AEAD seal; hub sees
 * ciphertext only. No plaintext voice fallback. Not wire 'V'.
 */
#ifndef ATN_VOICE_H
#define ATN_VOICE_H

#include "atn_tun.h"

#define ATN_VOICE_WIRE         0x41u /* 'A' */
#define ATN_VOICE_CTRL         0x43u /* 'C' */
#define ATN_VOICE_AUDIO        0x46u /* 'F' — clear payload; P2P tunnel only */
#define ATN_VOICE_SEALED       0x53u /* 'S' — nested AEAD; hub-opaque */

#define ATN_VOICE_OP_OFFER     1u
#define ATN_VOICE_OP_ACCEPT    2u
#define ATN_VOICE_OP_REJECT    3u
#define ATN_VOICE_OP_HANGUP    4u
#define ATN_VOICE_OP_BUSY      5u
#define ATN_VOICE_OP_KEEPALIVE 6u
#define ATN_VOICE_OP_CODEC     7u
#define ATN_VOICE_OP_DIAL_INFO 8u  /* ipv4 + port for P2P attempt */
#define ATN_VOICE_OP_E2E_CT    9u  /* ML-KEM CT chunk */
#define ATN_VOICE_OP_PROBE     10u /* latency probe */
#define ATN_VOICE_OP_PROBE_ACK 11u

#define ATN_VOICE_CODEC_PCM16  0u
#define ATN_VOICE_CODEC_IMA    1u

#define ATN_VOICE_RATE_HZ      16000u
#define ATN_VOICE_FRAME_MS     20u
#define ATN_VOICE_FRAME_SAMPLES 320u
#define ATN_VOICE_PCM_BYTES    (ATN_VOICE_FRAME_SAMPLES * 2u)
#define ATN_VOICE_AUDIO_HDR    15u
#define ATN_VOICE_SEALED_HDR   14u /* 'A''S' + call_id + seq + sample_ts */
#define ATN_VOICE_CTRL_LEN     12u
#define ATN_VOICE_IMA_HDR      4u
#define ATN_VOICE_IMA_BYTES    (ATN_VOICE_IMA_HDR + (ATN_VOICE_FRAME_SAMPLES / 2u))
#define ATN_VOICE_E2E_CT_CHUNK 400u
#define ATN_VOICE_E2E_CT_NCHUNKS \
    ((ATN_MLKEM1024_CT_LEN + ATN_VOICE_E2E_CT_CHUNK - 1u) / ATN_VOICE_E2E_CT_CHUNK)

#define ATN_VOICE_JB_MIN_MS    40u
#define ATN_VOICE_JB_MAX_MS    120u
#define ATN_VOICE_JB_TARGET_MS 60u
#define ATN_VOICE_JB_SLOTS     8u

#define ATN_VOICE_IDLE         0
#define ATN_VOICE_OUTGOING     1
#define ATN_VOICE_RINGING      2
#define ATN_VOICE_CONNECTING   3
#define ATN_VOICE_ACTIVE       4
#define ATN_VOICE_HOLD         5
#define ATN_VOICE_TERMINATING  6

#define ATN_VOICE_PATH_NONE    0
#define ATN_VOICE_PATH_P2P     1 /* clear 'A''F' inside peer tunnel = E2E */
#define ATN_VOICE_PATH_RELAY   2 /* 'A''S' nested seal; hub opaque */

#define ATN_VOICE_LABEL_MAX    64u
#define ATN_VOICE_MAX_CONTACTS 32u
#define ATN_VOICE_MAX_HUBS     8u

typedef struct {
    uint32_t frames_sent;
    uint32_t frames_recv;
    uint32_t frames_dropped;
    uint32_t frames_lost;
    uint32_t frames_late;
    uint32_t jitter_ms;
    uint32_t loss_permille;
    uint8_t  path; /* ATN_VOICE_PATH_* */
} atn_voice_stats;

typedef struct {
    uint32_t seq;
    uint32_t sample_ts;
    uint8_t  codec;
    uint16_t nbytes;
    uint8_t  data[ATN_VOICE_PCM_BYTES];
    uint8_t  used;
} atn_voice_jb_slot;

typedef struct {
    atn_voice_jb_slot slot[ATN_VOICE_JB_SLOTS];
    uint32_t play_seq;
    int      play_armed;
    uint32_t depth_target;
    uint32_t depth_max;
    int16_t  last_pcm[ATN_VOICE_FRAME_SAMPLES];
    int      have_last;
    uint32_t plc_count;
} atn_voice_jitter;

typedef struct {
    uint8_t  ready;
    uint8_t  initiator;
    uint32_t sender_id; /* 1=caller, 2=callee for nonce */
    uint8_t  key[ATN_AEAD_KEY_LEN];
    uint8_t  ct[ATN_MLKEM1024_CT_LEN];
    uint8_t  ct_asm[ATN_MLKEM1024_CT_LEN];
    uint8_t  ct_bits; /* bit i = chunk i received (responder) */
    uint8_t  ct_sent_bits; /* initiator chunk send progress */
} atn_voice_e2e;

typedef struct {
    char     label[ATN_VOICE_LABEL_MAX];
    uint32_t ipv4_host;
    uint16_t port;
    uint8_t  peer_ek[ATN_MLKEM1024_EK_LEN];
    int      have_ek;
    uint32_t hub_ipv4[ATN_VOICE_MAX_HUBS];
    uint16_t hub_port[ATN_VOICE_MAX_HUBS];
    unsigned nhubs;
} atn_voice_contact;

typedef struct {
    atn_voice_contact c[ATN_VOICE_MAX_CONTACTS];
    unsigned n;
} atn_voice_roster;

typedef struct {
    uint32_t ipv4_host;
    uint16_t port;
    uint32_t rtt_ms; /* UINT32_MAX = unknown / failed */
    int      ok;
} atn_voice_hub_rtt;

typedef struct atn_voice {
    atn_tun *tun;            /* media/signalling transport (P2P or hub) */
    int      state;
    uint32_t call_id;
    uint8_t  codec;
    uint8_t  mute;
    uint8_t  path;           /* ATN_VOICE_PATH_* */
    uint32_t send_seq;
    uint32_t sample_ts;
    uint32_t peer_max_seq;
    atn_voice_jitter jb;
    atn_voice_stats  stats;
    atn_voice_e2e    e2e;
    int32_t  ima_pred;
    int32_t  ima_step_idx;
    uint32_t peer_ipv4;
    uint16_t peer_port;
} atn_voice;

void atn_voice_init(atn_voice *v, atn_tun *tun);
void atn_voice_wipe(atn_voice *v);

int  atn_voice_state(const atn_voice *v);
int  atn_voice_path(const atn_voice *v);
int  atn_voice_call(atn_voice *v, uint32_t call_id, uint8_t codec);
int  atn_voice_answer(atn_voice *v);
int  atn_voice_reject(atn_voice *v);
int  atn_voice_hangup(atn_voice *v);
int  atn_voice_busy(atn_voice *v);
int  atn_voice_set_mute(atn_voice *v, int mute);
int  atn_voice_set_hold(atn_voice *v, int hold);

/* Force path: P2P (clear 'F' in tunnel) or RELAY (require e2e ready). */
int  atn_voice_set_path(atn_voice *v, uint8_t path);

int  atn_voice_send_pcm(atn_voice *v, const int16_t *pcm, size_t nsamples);
int  atn_voice_receive_pcm(atn_voice *v, int16_t *pcm, size_t max_samples,
                           size_t *out_n);
int  atn_voice_on_frame(atn_voice *v, const uint8_t *pt, size_t n);
int  atn_voice_pump(atn_voice *v, uint8_t *out_pt, size_t *out_n, size_t out_max,
                    int timeout_ms);
void atn_voice_get_stats(const atn_voice *v, atn_voice_stats *st);

/* ---- E2E nested seal (DEC-0050 hub fallback) ----------------------------- */

int atn_voice_e2e_initiator(atn_voice_e2e *e,
                            const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN]);
int atn_voice_e2e_responder(atn_voice_e2e *e,
                            const uint8_t own_dk[ATN_MLKEM1024_DK_LEN],
                            const uint8_t ct[ATN_MLKEM1024_CT_LEN]);
void atn_voice_e2e_wipe(atn_voice_e2e *e);

/* Chunk CT for hub signaling; *out_n includes CONTROL hdr + chunk. */
int atn_voice_e2e_encode_ct_chunk(const atn_voice_e2e *e, uint32_t call_id,
                                  unsigned chunk_i, uint8_t *out, size_t out_cap,
                                  size_t *out_n);
int atn_voice_e2e_ingest_ct_chunk(atn_voice_e2e *e, const uint8_t *msg, size_t n,
                                  uint32_t *call_id, int *complete);

int atn_voice_seal_audio(const atn_voice_e2e *e, uint32_t call_id, uint32_t seq,
                         uint32_t sample_ts, uint8_t codec,
                         const uint8_t *payload, size_t plen,
                         uint8_t *out, size_t out_cap, size_t *out_n);
int atn_voice_unseal_audio(const atn_voice_e2e *e, const uint8_t *msg, size_t n,
                           uint32_t *call_id, uint32_t *seq, uint32_t *sample_ts,
                           uint8_t *codec, uint8_t *payload, size_t payload_cap,
                           size_t *plen);

/* ---- wire helpers ------------------------------------------------------- */

int atn_voice_encode_ctrl(uint8_t op, uint8_t codec, uint32_t call_id,
                          uint8_t *out, size_t out_cap, size_t *out_n);
int atn_voice_parse_ctrl(const uint8_t *msg, size_t n, uint8_t *op,
                         uint8_t *codec, uint32_t *call_id);

int atn_voice_encode_dial_info(uint32_t call_id, uint32_t ipv4_host,
                               uint16_t port, uint8_t *out, size_t out_cap,
                               size_t *out_n);
int atn_voice_parse_dial_info(const uint8_t *msg, size_t n, uint32_t *call_id,
                              uint32_t *ipv4_host, uint16_t *port);

int atn_voice_encode_audio(uint32_t call_id, uint32_t seq, uint32_t sample_ts,
                           uint8_t codec, const uint8_t *payload, size_t plen,
                           uint8_t *out, size_t out_cap, size_t *out_n);
int atn_voice_parse_audio(const uint8_t *msg, size_t n, uint32_t *call_id,
                          uint32_t *seq, uint32_t *sample_ts, uint8_t *codec,
                          const uint8_t **payload, size_t *plen);

int atn_voice_pcm16_pack(const int16_t *pcm, size_t nsamples,
                         uint8_t *out, size_t out_cap, size_t *out_n);
int atn_voice_pcm16_unpack(const uint8_t *in, size_t in_n,
                           int16_t *pcm, size_t max_samples, size_t *out_n);
int atn_voice_ima_encode(int32_t *pred, int32_t *step_idx,
                         const int16_t *pcm, size_t nsamples,
                         uint8_t *out, size_t out_cap, size_t *out_n);
int atn_voice_ima_decode(const uint8_t *in, size_t in_n,
                         int16_t *pcm, size_t max_samples, size_t *out_n);

void atn_voice_jitter_init(atn_voice_jitter *j);
void atn_voice_jitter_reset(atn_voice_jitter *j);
int  atn_voice_jitter_push(atn_voice_jitter *j, uint32_t seq, uint32_t sample_ts,
                           uint8_t codec, const int16_t *pcm, size_t nsamples,
                           atn_voice_stats *st);
int  atn_voice_jitter_pop(atn_voice_jitter *j, int16_t *pcm, size_t max_samples,
                          size_t *out_n, atn_voice_stats *st);

/* ---- contacts + latency ranking ----------------------------------------- */

void atn_voice_roster_init(atn_voice_roster *r);
int  atn_voice_roster_add(atn_voice_roster *r, const atn_voice_contact *c);
const atn_voice_contact *atn_voice_roster_find(const atn_voice_roster *r,
                                               const char *label);
int  atn_voice_roster_parse_line(const char *line, atn_voice_contact *c);
int  atn_voice_roster_load_text(atn_voice_roster *r, const char *text, size_t n);

/* Sort hubs ascending by rtt_ms (unknowns last). Stable for equal RTT. */
void atn_voice_rank_hubs(atn_voice_hub_rtt *hubs, unsigned n);

/*
 * Attempt P2P: initiator HS to peer_ek at ipv4:port. On success set path P2P
 * and bind v->tun to p2p_tun. Returns ATN_OK or error (caller may relay).
 */
int atn_voice_try_p2p(atn_voice *v, atn_tun *p2p_tun,
                      const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN],
                      uint32_t ipv4_host, uint16_t port, uint32_t call_id,
                      uint8_t codec, int timeout_ms);

/*
 * Start sealed-relay call on existing hub tun: initiator e2e + offer + CT chunks.
 * Caller must pump peer to ingest CT / accept.
 */
int atn_voice_start_relay(atn_voice *v, atn_tun *hub_tun,
                          const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN],
                          uint32_t call_id, uint8_t codec);

/* Responder: after CT complete, finish e2e and ACCEPT on hub tun. */
int atn_voice_accept_relay(atn_voice *v,
                           const uint8_t own_dk[ATN_MLKEM1024_DK_LEN]);

#endif
