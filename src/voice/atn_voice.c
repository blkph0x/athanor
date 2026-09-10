/*
 * Athanor secure voice session (DEC-0050).
 * P2P: clear 'A''F' inside peer tunnel (E2E). Relay: 'A''S' nested seal.
 */
#include "atn_voice.h"

#include <string.h>

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int tun_ready(const atn_voice *v)
{
    return v != NULL && v->tun != NULL && v->tun->state == ATN_TUN_ESTABLISHED;
}

static int send_ctrl(atn_voice *v, uint8_t op)
{
    uint8_t wire[ATN_VOICE_CTRL_LEN];
    size_t wn = 0;
    int rc;
    if (!tun_ready(v)) {
        return ATN_ERR_STATE;
    }
    rc = atn_voice_encode_ctrl(op, v->codec, v->call_id, wire, sizeof(wire), &wn);
    if (rc != ATN_OK) {
        return rc;
    }
    return atn_tun_send(v->tun, wire, wn);
}

void atn_voice_init(atn_voice *v, atn_tun *tun)
{
    if (v == NULL) {
        return;
    }
    atn_memzero(v, sizeof(*v));
    v->tun = tun;
    v->state = ATN_VOICE_IDLE;
    v->codec = ATN_VOICE_CODEC_PCM16;
    v->path = ATN_VOICE_PATH_P2P; /* default: clear-in-tunnel when dialed P2P */
    atn_voice_jitter_init(&v->jb);
}

void atn_voice_wipe(atn_voice *v)
{
    atn_tun *tun;
    if (v == NULL) {
        return;
    }
    tun = v->tun;
    atn_voice_e2e_wipe(&v->e2e);
    atn_voice_jitter_reset(&v->jb);
    atn_memzero(v, sizeof(*v));
    v->tun = tun;
    v->state = ATN_VOICE_IDLE;
}

int atn_voice_state(const atn_voice *v)
{
    return v == NULL ? ATN_VOICE_IDLE : v->state;
}

int atn_voice_path(const atn_voice *v)
{
    return v == NULL ? ATN_VOICE_PATH_NONE : (int)v->path;
}

int atn_voice_set_path(atn_voice *v, uint8_t path)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (path == ATN_VOICE_PATH_RELAY && !v->e2e.ready) {
        return ATN_ERR_STATE;
    }
    if (path != ATN_VOICE_PATH_P2P && path != ATN_VOICE_PATH_RELAY) {
        return ATN_ERR_PARAM;
    }
    v->path = path;
    v->stats.path = path;
    return ATN_OK;
}

void atn_voice_get_stats(const atn_voice *v, atn_voice_stats *st)
{
    if (st == NULL) {
        return;
    }
    if (v == NULL) {
        atn_memzero(st, sizeof(*st));
        return;
    }
    *st = v->stats;
    st->path = v->path;
    if (st->frames_sent + st->frames_recv > 0) {
        uint32_t tot = st->frames_sent + st->frames_recv;
        if (tot == 0) {
            st->loss_permille = 0;
        } else {
            st->loss_permille = (st->frames_lost * 1000u) /
                                (tot + st->frames_lost);
        }
    }
}

int atn_voice_encode_ctrl(uint8_t op, uint8_t codec, uint32_t call_id,
                          uint8_t *out, size_t out_cap, size_t *out_n)
{
    if (out == NULL || out_n == NULL || out_cap < ATN_VOICE_CTRL_LEN) {
        return ATN_ERR_PARAM;
    }
    if (op < ATN_VOICE_OP_OFFER || op > ATN_VOICE_OP_PROBE_ACK) {
        return ATN_ERR_PARAM;
    }
    out[0] = ATN_VOICE_WIRE;
    out[1] = ATN_VOICE_CTRL;
    out[2] = op;
    out[3] = codec;
    put_be32(out + 4, call_id);
    put_be32(out + 8, 0);
    *out_n = ATN_VOICE_CTRL_LEN;
    return ATN_OK;
}

int atn_voice_parse_ctrl(const uint8_t *msg, size_t n, uint8_t *op,
                         uint8_t *codec, uint32_t *call_id)
{
    if (msg == NULL || op == NULL || codec == NULL || call_id == NULL) {
        return ATN_ERR_PARAM;
    }
    if (n < ATN_VOICE_CTRL_LEN || msg[0] != ATN_VOICE_WIRE ||
        msg[1] != ATN_VOICE_CTRL) {
        return ATN_ERR_PARAM;
    }
    if (msg[2] < ATN_VOICE_OP_OFFER || msg[2] > ATN_VOICE_OP_PROBE_ACK) {
        return ATN_ERR_PARAM;
    }
    /* E2E_CT / DIAL_INFO use extended layouts — not via this helper. */
    if (msg[2] == ATN_VOICE_OP_E2E_CT || msg[2] == ATN_VOICE_OP_DIAL_INFO) {
        return ATN_ERR_PARAM;
    }
    *op = msg[2];
    *codec = msg[3];
    *call_id = get_be32(msg + 4);
    return ATN_OK;
}

int atn_voice_encode_audio(uint32_t call_id, uint32_t seq, uint32_t sample_ts,
                           uint8_t codec, const uint8_t *payload, size_t plen,
                           uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t need;
    if (out == NULL || out_n == NULL || (plen > 0 && payload == NULL)) {
        return ATN_ERR_PARAM;
    }
    need = ATN_VOICE_AUDIO_HDR + plen;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    out[0] = ATN_VOICE_WIRE;
    out[1] = ATN_VOICE_AUDIO;
    put_be32(out + 2, call_id);
    put_be32(out + 6, seq);
    put_be32(out + 10, sample_ts);
    out[14] = codec;
    if (plen > 0) {
        memcpy(out + ATN_VOICE_AUDIO_HDR, payload, plen);
    }
    *out_n = need;
    return ATN_OK;
}

int atn_voice_parse_audio(const uint8_t *msg, size_t n, uint32_t *call_id,
                          uint32_t *seq, uint32_t *sample_ts, uint8_t *codec,
                          const uint8_t **payload, size_t *plen)
{
    if (msg == NULL || call_id == NULL || seq == NULL || sample_ts == NULL ||
        codec == NULL || payload == NULL || plen == NULL) {
        return ATN_ERR_PARAM;
    }
    if (n < ATN_VOICE_AUDIO_HDR || msg[0] != ATN_VOICE_WIRE ||
        msg[1] != ATN_VOICE_AUDIO) {
        return ATN_ERR_PARAM;
    }
    *call_id = get_be32(msg + 2);
    *seq = get_be32(msg + 6);
    *sample_ts = get_be32(msg + 10);
    *codec = msg[14];
    if (*codec != ATN_VOICE_CODEC_PCM16 && *codec != ATN_VOICE_CODEC_IMA) {
        return ATN_ERR_PARAM;
    }
    *payload = msg + ATN_VOICE_AUDIO_HDR;
    *plen = n - ATN_VOICE_AUDIO_HDR;
    if (*codec == ATN_VOICE_CODEC_PCM16 && (*plen & 1u) != 0) {
        return ATN_ERR_PARAM;
    }
    if (n > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    return ATN_OK;
}

static void enter_idle(atn_voice *v)
{
    atn_voice_e2e_wipe(&v->e2e);
    atn_voice_jitter_reset(&v->jb);
    atn_memzero(v->jb.last_pcm, sizeof(v->jb.last_pcm));
    v->send_seq = 0;
    v->sample_ts = 0;
    v->peer_max_seq = 0;
    v->ima_pred = 0;
    v->ima_step_idx = 0;
    v->mute = 0;
    v->call_id = 0;
    v->path = ATN_VOICE_PATH_NONE;
    v->state = ATN_VOICE_IDLE;
}

int atn_voice_call(atn_voice *v, uint32_t call_id, uint8_t codec)
{
    int rc;
    if (v == NULL || call_id == 0) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_IDLE) {
        return ATN_ERR_STATE;
    }
    if (!tun_ready(v)) {
        return ATN_ERR_STATE; /* no plaintext fallback */
    }
    if (codec != ATN_VOICE_CODEC_PCM16 && codec != ATN_VOICE_CODEC_IMA) {
        return ATN_ERR_PARAM;
    }
    v->call_id = call_id;
    v->codec = codec;
    if (v->path == ATN_VOICE_PATH_NONE) {
        v->path = ATN_VOICE_PATH_P2P;
    }
    v->stats.path = v->path;
    v->state = ATN_VOICE_OUTGOING;
    rc = send_ctrl(v, ATN_VOICE_OP_OFFER);
    if (rc != ATN_OK) {
        enter_idle(v);
        return rc;
    }
    (void)send_ctrl(v, ATN_VOICE_OP_CODEC);
    return ATN_OK;
}

int atn_voice_answer(atn_voice *v)
{
    int rc;
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_RINGING) {
        return ATN_ERR_STATE;
    }
    if (!tun_ready(v)) {
        return ATN_ERR_STATE;
    }
    v->state = ATN_VOICE_CONNECTING;
    rc = send_ctrl(v, ATN_VOICE_OP_ACCEPT);
    if (rc != ATN_OK) {
        return rc;
    }
    v->state = ATN_VOICE_ACTIVE;
    return ATN_OK;
}

int atn_voice_reject(atn_voice *v)
{
    int rc;
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_RINGING) {
        return ATN_ERR_STATE;
    }
    rc = send_ctrl(v, ATN_VOICE_OP_REJECT);
    enter_idle(v);
    return rc;
}

int atn_voice_busy(atn_voice *v)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_RINGING && v->state != ATN_VOICE_ACTIVE &&
        v->state != ATN_VOICE_HOLD && v->state != ATN_VOICE_CONNECTING) {
        return ATN_ERR_STATE;
    }
    return send_ctrl(v, ATN_VOICE_OP_BUSY);
}

int atn_voice_hangup(atn_voice *v)
{
    int rc = ATN_OK;
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state == ATN_VOICE_IDLE || v->state == ATN_VOICE_TERMINATING) {
        return ATN_ERR_STATE;
    }
    v->state = ATN_VOICE_TERMINATING;
    if (tun_ready(v)) {
        rc = send_ctrl(v, ATN_VOICE_OP_HANGUP);
    }
    enter_idle(v);
    return rc;
}

int atn_voice_set_mute(atn_voice *v, int mute)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    v->mute = mute ? 1 : 0;
    return ATN_OK;
}

int atn_voice_set_hold(atn_voice *v, int hold)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (hold) {
        if (v->state != ATN_VOICE_ACTIVE) {
            return ATN_ERR_STATE;
        }
        v->state = ATN_VOICE_HOLD;
    } else {
        if (v->state != ATN_VOICE_HOLD) {
            return ATN_ERR_STATE;
        }
        v->state = ATN_VOICE_ACTIVE;
    }
    return ATN_OK;
}

int atn_voice_jb_set_target_ms(atn_voice *v, uint32_t target_ms)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    return atn_voice_jitter_set_target_ms(&v->jb, target_ms);
}

int atn_voice_jb_target_ms(const atn_voice *v)
{
    if (v == NULL) {
        return 0;
    }
    return (int)(v->jb.depth_target * ATN_VOICE_FRAME_MS);
}

int atn_voice_send_pcm(atn_voice *v, const int16_t *pcm, size_t nsamples)
{
    uint8_t payload[ATN_VOICE_PCM_BYTES];
    uint8_t wire[ATN_TUN_MAX_PT];
    size_t plen = 0, wn = 0;
    int rc;
    if (v == NULL || pcm == NULL || nsamples != ATN_VOICE_FRAME_SAMPLES) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_ACTIVE && v->state != ATN_VOICE_HOLD) {
        return ATN_ERR_STATE;
    }
    if (v->mute || v->state == ATN_VOICE_HOLD) {
        return ATN_OK; /* mute = stop sending frames, not zero flood */
    }
    if (!tun_ready(v)) {
        return ATN_ERR_STATE;
    }
    if (v->path == ATN_VOICE_PATH_RELAY && !v->e2e.ready) {
        return ATN_ERR_STATE;
    }
    if (v->codec == ATN_VOICE_CODEC_IMA) {
        rc = atn_voice_ima_encode(&v->ima_pred, &v->ima_step_idx, pcm, nsamples,
                                  payload, sizeof(payload), &plen);
    } else {
        rc = atn_voice_pcm16_pack(pcm, nsamples, payload, sizeof(payload), &plen);
    }
    if (rc != ATN_OK) {
        return rc;
    }
    if (v->path == ATN_VOICE_PATH_RELAY) {
        /* Nested seal — hub must not see PCM. */
        rc = atn_voice_seal_audio(&v->e2e, v->call_id, v->send_seq, v->sample_ts,
                                  v->codec, payload, plen, wire, sizeof(wire),
                                  &wn);
    } else {
        /* P2P tunnel AEAD is the E2E floor. */
        rc = atn_voice_encode_audio(v->call_id, v->send_seq, v->sample_ts,
                                    v->codec, payload, plen, wire, sizeof(wire),
                                    &wn);
    }
    atn_memzero(payload, sizeof(payload));
    if (rc != ATN_OK) {
        return rc;
    }
    if (wn > ATN_TUN_MAX_PT) {
        atn_memzero(wire, wn);
        return ATN_ERR_LEN;
    }
    rc = atn_tun_send(v->tun, wire, wn);
    atn_memzero(wire, wn);
    if (rc == ATN_OK) {
        v->send_seq++;
        v->sample_ts += (uint32_t)nsamples;
        v->stats.frames_sent++;
        v->stats.path = v->path;
    }
    return rc;
}

int atn_voice_receive_pcm(atn_voice *v, int16_t *pcm, size_t max_samples,
                          size_t *out_n)
{
    if (v == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_ACTIVE && v->state != ATN_VOICE_HOLD &&
        v->state != ATN_VOICE_CONNECTING) {
        return ATN_ERR_STATE;
    }
    return atn_voice_jitter_pop(&v->jb, pcm, max_samples, out_n, &v->stats);
}

static int handle_ctrl(atn_voice *v, uint8_t op, uint8_t codec, uint32_t call_id)
{
    switch (op) {
    case ATN_VOICE_OP_OFFER:
        if (v->state == ATN_VOICE_IDLE) {
            v->call_id = call_id;
            if (codec == ATN_VOICE_CODEC_PCM16 || codec == ATN_VOICE_CODEC_IMA) {
                v->codec = codec;
            }
            v->state = ATN_VOICE_RINGING;
            return ATN_OK;
        }
        if (v->state != ATN_VOICE_IDLE) {
            (void)send_ctrl(v, ATN_VOICE_OP_BUSY);
        }
        return ATN_ERR_STATE;
    case ATN_VOICE_OP_ACCEPT:
        if (v->state == ATN_VOICE_OUTGOING && call_id == v->call_id) {
            if (codec == ATN_VOICE_CODEC_PCM16 || codec == ATN_VOICE_CODEC_IMA) {
                v->codec = codec;
            }
            v->state = ATN_VOICE_CONNECTING;
            v->state = ATN_VOICE_ACTIVE;
            return ATN_OK;
        }
        return ATN_ERR_STATE;
    case ATN_VOICE_OP_REJECT:
    case ATN_VOICE_OP_BUSY:
    case ATN_VOICE_OP_HANGUP:
        if (v->state == ATN_VOICE_IDLE) {
            return ATN_ERR_STATE;
        }
        if (call_id != 0 && call_id != v->call_id &&
            v->state != ATN_VOICE_RINGING) {
            return ATN_ERR_STATE;
        }
        enter_idle(v);
        return ATN_OK;
    case ATN_VOICE_OP_KEEPALIVE:
        return ATN_OK;
    case ATN_VOICE_OP_PROBE:
        /* Latency probe: answer with ACK so peer can measure RTT (DEC-0053). */
        if (v->state == ATN_VOICE_ACTIVE || v->state == ATN_VOICE_HOLD ||
            v->state == ATN_VOICE_CONNECTING || v->state == ATN_VOICE_OUTGOING) {
            if (call_id == 0 || call_id == v->call_id) {
                (void)send_ctrl(v, ATN_VOICE_OP_PROBE_ACK);
            }
            return ATN_OK;
        }
        return ATN_ERR_STATE;
    case ATN_VOICE_OP_PROBE_ACK:
        /* Caller samples RTT; voice SM does not store clocks (platform does). */
        return ATN_OK;
    case ATN_VOICE_OP_CODEC:
        if ((v->state == ATN_VOICE_OUTGOING || v->state == ATN_VOICE_RINGING ||
             v->state == ATN_VOICE_CONNECTING || v->state == ATN_VOICE_ACTIVE) &&
            call_id == v->call_id) {
            if (codec == ATN_VOICE_CODEC_PCM16 || codec == ATN_VOICE_CODEC_IMA) {
                /* Prefer PCM16 if either side offers it; else take peer. */
                if (v->codec == ATN_VOICE_CODEC_IMA &&
                    codec == ATN_VOICE_CODEC_PCM16) {
                    v->codec = ATN_VOICE_CODEC_PCM16;
                } else if (v->state == ATN_VOICE_RINGING) {
                    v->codec = codec;
                }
            }
            return ATN_OK;
        }
        return ATN_ERR_STATE;
    default:
        return ATN_ERR_PARAM;
    }
}

static int handle_audio(atn_voice *v, uint32_t call_id, uint32_t seq,
                        uint32_t sample_ts, uint8_t codec,
                        const uint8_t *payload, size_t plen)
{
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES];
    size_t ns = 0;
    int rc;
    /* Product SoT: clear 'F' forbidden on hub-relay path. */
    if (v->path == ATN_VOICE_PATH_RELAY) {
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    if (v->state != ATN_VOICE_ACTIVE && v->state != ATN_VOICE_HOLD &&
        v->state != ATN_VOICE_CONNECTING) {
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    if (call_id != v->call_id) {
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    if (codec == ATN_VOICE_CODEC_IMA) {
        rc = atn_voice_ima_decode(payload, plen, pcm, ATN_VOICE_FRAME_SAMPLES,
                                  &ns);
    } else if (codec == ATN_VOICE_CODEC_PCM16) {
        rc = atn_voice_pcm16_unpack(payload, plen, pcm, ATN_VOICE_FRAME_SAMPLES,
                                    &ns);
    } else {
        v->stats.frames_dropped++;
        return ATN_ERR_PARAM;
    }
    if (rc != ATN_OK) {
        v->stats.frames_dropped++;
        return rc;
    }
    if (seq > v->peer_max_seq) {
        v->peer_max_seq = seq;
    }
    rc = atn_voice_jitter_push(&v->jb, seq, sample_ts, codec, pcm, ns,
                               &v->stats);
    atn_memzero(pcm, sizeof(pcm));
    if (rc == ATN_OK) {
        v->stats.frames_recv++;
    }
    return rc;
}

static int handle_sealed(atn_voice *v, const uint8_t *pt, size_t n)
{
    uint32_t call_id = 0, seq = 0, sample_ts = 0;
    uint8_t codec = 0;
    uint8_t payload[ATN_VOICE_PCM_BYTES];
    size_t plen = 0;
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES];
    size_t ns = 0;
    int rc;
    if (!v->e2e.ready) {
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    if (v->state != ATN_VOICE_ACTIVE && v->state != ATN_VOICE_HOLD &&
        v->state != ATN_VOICE_CONNECTING && v->state != ATN_VOICE_OUTGOING) {
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    rc = atn_voice_unseal_audio(&v->e2e, pt, n, &call_id, &seq, &sample_ts,
                                &codec, payload, sizeof(payload), &plen);
    if (rc != ATN_OK) {
        v->stats.frames_dropped++;
        return rc;
    }
    if (call_id != v->call_id) {
        atn_memzero(payload, plen);
        v->stats.frames_dropped++;
        return ATN_ERR_STATE;
    }
    if (codec == ATN_VOICE_CODEC_IMA) {
        rc = atn_voice_ima_decode(payload, plen, pcm, ATN_VOICE_FRAME_SAMPLES,
                                  &ns);
    } else {
        rc = atn_voice_pcm16_unpack(payload, plen, pcm, ATN_VOICE_FRAME_SAMPLES,
                                    &ns);
    }
    atn_memzero(payload, sizeof(payload));
    if (rc != ATN_OK) {
        v->stats.frames_dropped++;
        return rc;
    }
    if (seq > v->peer_max_seq) {
        v->peer_max_seq = seq;
    }
    rc = atn_voice_jitter_push(&v->jb, seq, sample_ts, codec, pcm, ns,
                               &v->stats);
    atn_memzero(pcm, sizeof(pcm));
    if (rc == ATN_OK) {
        v->stats.frames_recv++;
        v->path = ATN_VOICE_PATH_RELAY;
        v->stats.path = ATN_VOICE_PATH_RELAY;
    }
    return rc;
}

int atn_voice_on_frame(atn_voice *v, const uint8_t *pt, size_t n)
{
    if (v == NULL || pt == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    if (pt[0] != ATN_VOICE_WIRE) {
        return ATN_ERR_STATE;
    }
    if (n > ATN_TUN_MAX_PT) {
        v->stats.frames_dropped++;
        return ATN_ERR_LEN;
    }
    if (n >= 2 && pt[1] == ATN_VOICE_CTRL) {
        if (n >= 3 && pt[2] == ATN_VOICE_OP_E2E_CT) {
            uint32_t cid = 0;
            int complete = 0;
            int rc = atn_voice_e2e_ingest_ct_chunk(&v->e2e, pt, n, &cid,
                                                   &complete);
            if (rc != ATN_OK) {
                v->stats.frames_dropped++;
                return rc;
            }
            if (v->state == ATN_VOICE_IDLE) {
                return ATN_ERR_STATE;
            }
            if (cid != 0 && v->call_id != 0 && cid != v->call_id) {
                return ATN_ERR_STATE;
            }
            if (complete) {
                v->path = ATN_VOICE_PATH_RELAY;
            }
            return ATN_OK;
        }
        if (n >= 3 && pt[2] == ATN_VOICE_OP_DIAL_INFO) {
            uint32_t cid = 0, ip = 0;
            uint16_t port = 0;
            if (atn_voice_parse_dial_info(pt, n, &cid, &ip, &port) == ATN_OK) {
                if (cid == v->call_id || v->state == ATN_VOICE_RINGING) {
                    v->peer_ipv4 = ip;
                    v->peer_port = port;
                }
                return ATN_OK;
            }
            v->stats.frames_dropped++;
            return ATN_ERR_PARAM;
        }
        {
            uint8_t op = 0, codec = 0;
            uint32_t call_id = 0;
            int rc = atn_voice_parse_ctrl(pt, n, &op, &codec, &call_id);
            if (rc != ATN_OK) {
                v->stats.frames_dropped++;
                return rc;
            }
            return handle_ctrl(v, op, codec, call_id);
        }
    }
    if (n >= 2 && pt[1] == ATN_VOICE_SEALED) {
        return handle_sealed(v, pt, n);
    }
    if (n >= 2 && pt[1] == ATN_VOICE_AUDIO) {
        uint32_t call_id = 0, seq = 0, sample_ts = 0;
        uint8_t codec = 0;
        const uint8_t *payload = NULL;
        size_t plen = 0;
        int rc = atn_voice_parse_audio(pt, n, &call_id, &seq, &sample_ts,
                                       &codec, &payload, &plen);
        if (rc != ATN_OK) {
            v->stats.frames_dropped++;
            return rc;
        }
        return handle_audio(v, call_id, seq, sample_ts, codec, payload, plen);
    }
    v->stats.frames_dropped++;
    return ATN_ERR_PARAM;
}

int atn_voice_pump(atn_voice *v, uint8_t *out_pt, size_t *out_n, size_t out_max,
                   int timeout_ms)
{
    uint8_t pt[ATN_TUN_MAX_PT];
    size_t n = 0;
    int rc;
    if (v == NULL || v->tun == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_recv_data(v->tun, pt, &n, sizeof(pt), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    if (n == 0) {
        if (out_n != NULL) {
            *out_n = 0;
        }
        return ATN_OK;
    }
    if (n >= 1 && pt[0] == ATN_VOICE_WIRE) {
        rc = atn_voice_on_frame(v, pt, n);
        atn_memzero(pt, n);
        return rc;
    }
    /* Non-voice DATA — hand back if caller provided a buffer. */
    if (out_pt != NULL && out_n != NULL && n <= out_max) {
        memcpy(out_pt, pt, n);
        *out_n = n;
        atn_memzero(pt, n);
        return ATN_ERR_CONFLICT;
    }
    atn_memzero(pt, n);
    return ATN_ERR_STATE;
}
