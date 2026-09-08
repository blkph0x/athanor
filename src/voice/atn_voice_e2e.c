/*
 * Nested E2E seal for hub-relay voice (DEC-0050).
 * ML-KEM-1024 session → HKDF-SHA-512 → ChaCha20-Poly1305. Hub never gets key.
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

static int derive_key(atn_voice_e2e *e, const uint8_t ss[ATN_MLKEM1024_SS_LEN])
{
    static const uint8_t info[] = "atn-voice-e2e-v1";
    int rc = atn_hkdf_sha512(NULL, 0, ss, ATN_MLKEM1024_SS_LEN, info,
                             sizeof(info) - 1u, e->key, ATN_AEAD_KEY_LEN);
    if (rc != ATN_OK) {
        return rc;
    }
    e->ready = 1;
    return ATN_OK;
}

void atn_voice_e2e_wipe(atn_voice_e2e *e)
{
    if (e == NULL) {
        return;
    }
    atn_memzero(e, sizeof(*e));
}

int atn_voice_e2e_initiator(atn_voice_e2e *e,
                            const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN])
{
    uint8_t ss[ATN_MLKEM1024_SS_LEN];
    int rc;
    if (e == NULL || peer_ek == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_voice_e2e_wipe(e);
    e->initiator = 1;
    e->sender_id = 1u;
    rc = atn_mlkem1024_encaps(peer_ek, ss, e->ct);
    if (rc != ATN_OK) {
        atn_voice_e2e_wipe(e);
        return rc;
    }
    rc = derive_key(e, ss);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        atn_voice_e2e_wipe(e);
    }
    return rc;
}

int atn_voice_e2e_responder(atn_voice_e2e *e,
                            const uint8_t own_dk[ATN_MLKEM1024_DK_LEN],
                            const uint8_t ct[ATN_MLKEM1024_CT_LEN])
{
    uint8_t ss[ATN_MLKEM1024_SS_LEN];
    int rc;
    if (e == NULL || own_dk == NULL || ct == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_voice_e2e_wipe(e);
    e->initiator = 0;
    e->sender_id = 2u;
    memcpy(e->ct, ct, ATN_MLKEM1024_CT_LEN);
    rc = atn_mlkem1024_decaps(own_dk, ct, ss);
    if (rc != ATN_OK) {
        atn_voice_e2e_wipe(e);
        return rc;
    }
    rc = derive_key(e, ss);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        atn_voice_e2e_wipe(e);
    }
    return rc;
}

int atn_voice_e2e_encode_ct_chunk(const atn_voice_e2e *e, uint32_t call_id,
                                  unsigned chunk_i, uint8_t *out, size_t out_cap,
                                  size_t *out_n)
{
    size_t off, len, need;
    if (e == NULL || out == NULL || out_n == NULL || !e->initiator) {
        return ATN_ERR_PARAM;
    }
    if (chunk_i >= ATN_VOICE_E2E_CT_NCHUNKS) {
        return ATN_ERR_PARAM;
    }
    off = (size_t)chunk_i * ATN_VOICE_E2E_CT_CHUNK;
    len = ATN_MLKEM1024_CT_LEN - off;
    if (len > ATN_VOICE_E2E_CT_CHUNK) {
        len = ATN_VOICE_E2E_CT_CHUNK;
    }
    need = 16u + len;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    out[0] = ATN_VOICE_WIRE;
    out[1] = ATN_VOICE_CTRL;
    out[2] = ATN_VOICE_OP_E2E_CT;
    out[3] = (uint8_t)chunk_i;
    put_be32(out + 4, call_id);
    out[8] = (uint8_t)ATN_VOICE_E2E_CT_NCHUNKS;
    out[9] = 0;
    out[10] = (uint8_t)((len >> 8) & 0xffu);
    out[11] = (uint8_t)(len & 0xffu);
    put_be32(out + 12, (uint32_t)off);
    memcpy(out + 16, e->ct + off, len);
    *out_n = 16u + len;
    return ATN_OK;
}

int atn_voice_e2e_ingest_ct_chunk(atn_voice_e2e *e, const uint8_t *msg, size_t n,
                                  uint32_t *call_id, int *complete)
{
    unsigned chunk_i, nchunks;
    size_t off, len;
    if (e == NULL || msg == NULL || call_id == NULL || complete == NULL) {
        return ATN_ERR_PARAM;
    }
    *complete = 0;
    if (n < 16u || msg[0] != ATN_VOICE_WIRE || msg[1] != ATN_VOICE_CTRL ||
        msg[2] != ATN_VOICE_OP_E2E_CT) {
        return ATN_ERR_PARAM;
    }
    chunk_i = msg[3];
    *call_id = get_be32(msg + 4);
    nchunks = msg[8];
    len = ((size_t)msg[10] << 8) | (size_t)msg[11];
    off = get_be32(msg + 12);
    if (nchunks != ATN_VOICE_E2E_CT_NCHUNKS || chunk_i >= nchunks ||
        off + len > ATN_MLKEM1024_CT_LEN || n < 16u + len) {
        return ATN_ERR_LEN;
    }
    memcpy(e->ct_asm + off, msg + 16, len);
    e->ct_bits = (uint8_t)(e->ct_bits | (uint8_t)(1u << chunk_i));
    if (e->ct_bits == (uint8_t)((1u << nchunks) - 1u)) {
        memcpy(e->ct, e->ct_asm, ATN_MLKEM1024_CT_LEN);
        *complete = 1;
    }
    return ATN_OK;
}

int atn_voice_seal_audio(const atn_voice_e2e *e, uint32_t call_id, uint32_t seq,
                         uint32_t sample_ts, uint8_t codec,
                         const uint8_t *payload, size_t plen,
                         uint8_t *out, size_t out_cap, size_t *out_n)
{
    uint8_t pt[1u + ATN_VOICE_PCM_BYTES];
    uint8_t nonce[ATN_AEAD_NONCE_LEN];
    uint8_t tag[ATN_AEAD_TAG_LEN];
    uint8_t aad[10];
    size_t pt_len, need;
    int rc;
    if (e == NULL || !e->ready || out == NULL || out_n == NULL ||
        (plen > 0 && payload == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (plen + 1u > sizeof(pt)) {
        return ATN_ERR_LEN;
    }
    pt[0] = codec;
    if (plen > 0) {
        memcpy(pt + 1, payload, plen);
    }
    pt_len = 1u + plen;
    need = ATN_VOICE_SEALED_HDR + pt_len + ATN_AEAD_TAG_LEN;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        atn_memzero(pt, sizeof(pt));
        return ATN_ERR_LEN;
    }
    rc = atn_nonce_format(nonce, e->sender_id, (uint64_t)seq);
    if (rc != ATN_OK) {
        atn_memzero(pt, sizeof(pt));
        return rc;
    }
    aad[0] = ATN_VOICE_WIRE;
    aad[1] = ATN_VOICE_SEALED;
    put_be32(aad + 2, call_id);
    put_be32(aad + 6, seq);
    out[0] = ATN_VOICE_WIRE;
    out[1] = ATN_VOICE_SEALED;
    put_be32(out + 2, call_id);
    put_be32(out + 6, seq);
    put_be32(out + 10, sample_ts);
    rc = atn_aead_encrypt(e->key, nonce, aad, sizeof(aad), pt, pt_len,
                          out + ATN_VOICE_SEALED_HDR, tag);
    atn_memzero(pt, sizeof(pt));
    atn_memzero(nonce, sizeof(nonce));
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(out + ATN_VOICE_SEALED_HDR + pt_len, tag, ATN_AEAD_TAG_LEN);
    atn_memzero(tag, sizeof(tag));
    *out_n = need;
    return ATN_OK;
}

int atn_voice_unseal_audio(const atn_voice_e2e *e, const uint8_t *msg, size_t n,
                           uint32_t *call_id, uint32_t *seq, uint32_t *sample_ts,
                           uint8_t *codec, uint8_t *payload, size_t payload_cap,
                           size_t *plen)
{
    uint8_t nonce[ATN_AEAD_NONCE_LEN];
    uint8_t tag[ATN_AEAD_TAG_LEN];
    uint8_t aad[10];
    uint8_t pt[1u + ATN_VOICE_PCM_BYTES];
    size_t ct_len;
    uint32_t peer_sender;
    int rc;
    if (e == NULL || !e->ready || msg == NULL || call_id == NULL || seq == NULL ||
        sample_ts == NULL || codec == NULL || payload == NULL || plen == NULL) {
        return ATN_ERR_PARAM;
    }
    if (n < ATN_VOICE_SEALED_HDR + 1u + ATN_AEAD_TAG_LEN ||
        msg[0] != ATN_VOICE_WIRE || msg[1] != ATN_VOICE_SEALED) {
        return ATN_ERR_PARAM;
    }
    *call_id = get_be32(msg + 2);
    *seq = get_be32(msg + 6);
    *sample_ts = get_be32(msg + 10);
    ct_len = n - ATN_VOICE_SEALED_HDR - ATN_AEAD_TAG_LEN;
    if (ct_len < 1u || ct_len > sizeof(pt)) {
        return ATN_ERR_LEN;
    }
    memcpy(tag, msg + ATN_VOICE_SEALED_HDR + ct_len, ATN_AEAD_TAG_LEN);
    peer_sender = e->initiator ? 2u : 1u;
    rc = atn_nonce_format(nonce, peer_sender, (uint64_t)(*seq));
    if (rc != ATN_OK) {
        return rc;
    }
    aad[0] = ATN_VOICE_WIRE;
    aad[1] = ATN_VOICE_SEALED;
    put_be32(aad + 2, *call_id);
    put_be32(aad + 6, *seq);
    rc = atn_aead_decrypt(e->key, nonce, aad, sizeof(aad),
                          msg + ATN_VOICE_SEALED_HDR, ct_len, tag, pt);
    atn_memzero(nonce, sizeof(nonce));
    atn_memzero(tag, sizeof(tag));
    if (rc != ATN_OK) {
        atn_memzero(pt, sizeof(pt));
        return rc;
    }
    *codec = pt[0];
    *plen = ct_len - 1u;
    if (*plen > payload_cap) {
        atn_memzero(pt, sizeof(pt));
        return ATN_ERR_LEN;
    }
    if (*plen > 0) {
        memcpy(payload, pt + 1, *plen);
    }
    atn_memzero(pt, sizeof(pt));
    return ATN_OK;
}
