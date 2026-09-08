/*
 * In-tree voice codecs (DEC-0049): PCM16 + IMA ADPCM. No external libs.
 */
#include "atn_voice.h"

#include <string.h>

int atn_voice_pcm16_pack(const int16_t *pcm, size_t nsamples,
                         uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t i, need;
    if (pcm == NULL || out == NULL || out_n == NULL || nsamples == 0) {
        return ATN_ERR_PARAM;
    }
    need = nsamples * 2u;
    if (need > out_cap || need > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    for (i = 0; i < nsamples; i++) {
        int16_t s = pcm[i];
        out[2u * i] = (uint8_t)(s & 0xff);
        out[2u * i + 1u] = (uint8_t)((s >> 8) & 0xff);
    }
    *out_n = need;
    return ATN_OK;
}

int atn_voice_pcm16_unpack(const uint8_t *in, size_t in_n,
                           int16_t *pcm, size_t max_samples, size_t *out_n)
{
    size_t i, ns;
    if (in == NULL || pcm == NULL || out_n == NULL || (in_n & 1u) != 0) {
        return ATN_ERR_PARAM;
    }
    ns = in_n / 2u;
    if (ns > max_samples) {
        return ATN_ERR_LEN;
    }
    for (i = 0; i < ns; i++) {
        pcm[i] = (int16_t)((uint16_t)in[2u * i] |
                           ((uint16_t)in[2u * i + 1u] << 8));
    }
    *out_n = ns;
    return ATN_OK;
}

/* IMA ADPCM step table / index table (public domain IMA ADPCM). */
static const int16_t ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static int32_t ima_clamp_pred(int32_t p)
{
    if (p > 32767) {
        return 32767;
    }
    if (p < -32768) {
        return -32768;
    }
    return p;
}

static int32_t ima_clamp_idx(int32_t i)
{
    if (i < 0) {
        return 0;
    }
    if (i > 88) {
        return 88;
    }
    return i;
}

static uint8_t ima_encode_nibble(int32_t *pred, int32_t *step_idx, int16_t sample)
{
    int32_t step = ima_step_table[*step_idx];
    int32_t diff = (int32_t)sample - *pred;
    uint8_t nibble = 0;
    int32_t vpdiff;

    if (diff < 0) {
        nibble = 8;
        diff = -diff;
    }
    vpdiff = step >> 3;
    if (diff >= step) {
        nibble |= 4;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 2;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 1;
        vpdiff += step;
    }
    if (nibble & 8) {
        *pred = ima_clamp_pred(*pred - vpdiff);
    } else {
        *pred = ima_clamp_pred(*pred + vpdiff);
    }
    *step_idx = ima_clamp_idx(*step_idx + ima_index_table[nibble & 15]);
    return (uint8_t)(nibble & 15u);
}

static int16_t ima_decode_nibble(int32_t *pred, int32_t *step_idx, uint8_t nibble)
{
    int32_t step = ima_step_table[*step_idx];
    int32_t vpdiff = step >> 3;
    nibble &= 15u;
    if (nibble & 4) {
        vpdiff += step;
    }
    if (nibble & 2) {
        vpdiff += step >> 1;
    }
    if (nibble & 1) {
        vpdiff += step >> 2;
    }
    if (nibble & 8) {
        *pred = ima_clamp_pred(*pred - vpdiff);
    } else {
        *pred = ima_clamp_pred(*pred + vpdiff);
    }
    *step_idx = ima_clamp_idx(*step_idx + ima_index_table[nibble]);
    return (int16_t)*pred;
}

int atn_voice_ima_encode(int32_t *pred, int32_t *step_idx,
                         const int16_t *pcm, size_t nsamples,
                         uint8_t *out, size_t out_cap, size_t *out_n)
{
    size_t i, need;
    int32_t p, sidx;
    if (pred == NULL || step_idx == NULL || pcm == NULL || out == NULL ||
        out_n == NULL || nsamples == 0 || (nsamples & 1u) != 0) {
        return ATN_ERR_PARAM;
    }
    need = ATN_VOICE_IMA_HDR + nsamples / 2u;
    if (need > out_cap || need + ATN_VOICE_AUDIO_HDR > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    p = *pred;
    sidx = *step_idx;
    /* Block header: predictor LE16 + step index + reserved. */
    out[0] = (uint8_t)(p & 0xff);
    out[1] = (uint8_t)((p >> 8) & 0xff);
    out[2] = (uint8_t)(sidx & 0xff);
    out[3] = 0;
    for (i = 0; i < nsamples; i += 2u) {
        uint8_t lo = ima_encode_nibble(&p, &sidx, pcm[i]);
        uint8_t hi = ima_encode_nibble(&p, &sidx, pcm[i + 1u]);
        out[ATN_VOICE_IMA_HDR + i / 2u] = (uint8_t)(lo | (hi << 4));
    }
    *pred = p;
    *step_idx = sidx;
    *out_n = need;
    return ATN_OK;
}

int atn_voice_ima_decode(const uint8_t *in, size_t in_n,
                         int16_t *pcm, size_t max_samples, size_t *out_n)
{
    size_t i, ns, nbytes;
    int32_t pred, sidx;
    if (in == NULL || pcm == NULL || out_n == NULL || in_n < ATN_VOICE_IMA_HDR) {
        return ATN_ERR_PARAM;
    }
    nbytes = in_n - ATN_VOICE_IMA_HDR;
    ns = nbytes * 2u;
    if (ns > max_samples || ns == 0) {
        return ATN_ERR_LEN;
    }
    pred = (int16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
    sidx = ima_clamp_idx((int32_t)in[2]);
    for (i = 0; i < nbytes; i++) {
        uint8_t b = in[ATN_VOICE_IMA_HDR + i];
        pcm[2u * i] = ima_decode_nibble(&pred, &sidx, (uint8_t)(b & 15u));
        pcm[2u * i + 1u] = ima_decode_nibble(&pred, &sidx, (uint8_t)(b >> 4));
    }
    *out_n = ns;
    return ATN_OK;
}
