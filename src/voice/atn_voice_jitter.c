/*
 * Bounded jitter buffer + PLC for DEC-0049 voice (40–120 ms target).
 */
#include "atn_voice.h"

#include <string.h>

void atn_voice_jitter_init(atn_voice_jitter *j)
{
    if (j == NULL) {
        return;
    }
    atn_memzero(j, sizeof(*j));
    j->depth_target = ATN_VOICE_JB_TARGET_MS / ATN_VOICE_FRAME_MS;
    j->depth_max = ATN_VOICE_JB_MAX_MS / ATN_VOICE_FRAME_MS;
    if (j->depth_target < 2u) {
        j->depth_target = 2u; /* 40ms floor */
    }
    if (j->depth_max > ATN_VOICE_JB_SLOTS) {
        j->depth_max = ATN_VOICE_JB_SLOTS;
    }
    if (j->depth_target > j->depth_max) {
        j->depth_target = j->depth_max;
    }
}

void atn_voice_jitter_reset(atn_voice_jitter *j)
{
    atn_voice_jitter_init(j);
}

static uint32_t jb_count(const atn_voice_jitter *j)
{
    uint32_t i, n = 0;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (j->slot[i].used) {
            n++;
        }
    }
    return n;
}

static atn_voice_jb_slot *jb_find(atn_voice_jitter *j, uint32_t seq)
{
    uint32_t i;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (j->slot[i].used && j->slot[i].seq == seq) {
            return &j->slot[i];
        }
    }
    return NULL;
}

static atn_voice_jb_slot *jb_free(atn_voice_jitter *j)
{
    uint32_t i;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (!j->slot[i].used) {
            return &j->slot[i];
        }
    }
    return NULL;
}

static void jb_drop_oldest(atn_voice_jitter *j, atn_voice_stats *st)
{
    uint32_t i, best_i = 0;
    int found = 0;
    uint32_t best_seq = 0;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (!j->slot[i].used) {
            continue;
        }
        if (!found || j->slot[i].seq < best_seq) {
            found = 1;
            best_seq = j->slot[i].seq;
            best_i = i;
        }
    }
    if (found) {
        atn_memzero(j->slot[best_i].data, sizeof(j->slot[best_i].data));
        j->slot[best_i].used = 0;
        if (st != NULL) {
            st->frames_late++;
        }
    }
}

int atn_voice_jitter_push(atn_voice_jitter *j, uint32_t seq, uint32_t sample_ts,
                          uint8_t codec, const int16_t *pcm, size_t nsamples,
                          atn_voice_stats *st)
{
    atn_voice_jb_slot *s;
    size_t nbytes;
    (void)codec;
    if (j == NULL || pcm == NULL || nsamples == 0 ||
        nsamples > ATN_VOICE_FRAME_SAMPLES) {
        return ATN_ERR_PARAM;
    }
    if (j->play_armed && seq < j->play_seq) {
        if (st != NULL) {
            st->frames_late++;
            st->frames_dropped++;
        }
        return ATN_ERR_NONCE; /* stale */
    }
    if (jb_find(j, seq) != NULL) {
        if (st != NULL) {
            st->frames_dropped++;
        }
        return ATN_ERR_NONCE; /* duplicate */
    }
    while (jb_count(j) >= j->depth_max) {
        jb_drop_oldest(j, st);
    }
    s = jb_free(j);
    if (s == NULL) {
        jb_drop_oldest(j, st);
        s = jb_free(j);
        if (s == NULL) {
            return ATN_ERR_LEN;
        }
    }
    nbytes = nsamples * 2u;
    s->seq = seq;
    s->sample_ts = sample_ts;
    s->codec = codec;
    s->nbytes = (uint16_t)nbytes;
    {
        size_t i;
        for (i = 0; i < nsamples; i++) {
            int16_t v = pcm[i];
            s->data[2u * i] = (uint8_t)(v & 0xff);
            s->data[2u * i + 1u] = (uint8_t)((v >> 8) & 0xff);
        }
    }
    s->used = 1;
    if (!j->play_armed) {
        if (jb_count(j) >= j->depth_target) {
            /* Start at lowest seq in buffer. */
            uint32_t i, min_seq = 0;
            int have = 0;
            for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
                if (!j->slot[i].used) {
                    continue;
                }
                if (!have || j->slot[i].seq < min_seq) {
                    have = 1;
                    min_seq = j->slot[i].seq;
                }
            }
            j->play_seq = min_seq;
            j->play_armed = 1;
        }
    }
    /* Crude jitter estimate: depth * frame_ms. */
    if (st != NULL) {
        st->jitter_ms = jb_count(j) * ATN_VOICE_FRAME_MS;
    }
    return ATN_OK;
}

static void plc_fill(atn_voice_jitter *j, int16_t *pcm, size_t ns)
{
    size_t i;
    if (j->have_last) {
        /* Repeat last frame with mild fade (~7/8). */
        for (i = 0; i < ns; i++) {
            pcm[i] = (int16_t)((j->last_pcm[i] * 7) / 8);
        }
    } else {
        atn_memzero(pcm, ns * sizeof(int16_t));
    }
    j->plc_count++;
}

int atn_voice_jitter_pop(atn_voice_jitter *j, int16_t *pcm, size_t max_samples,
                         size_t *out_n, atn_voice_stats *st)
{
    atn_voice_jb_slot *s;
    size_t ns;
    size_t i;
    if (j == NULL || pcm == NULL || out_n == NULL) {
        return ATN_ERR_PARAM;
    }
    if (max_samples < ATN_VOICE_FRAME_SAMPLES) {
        return ATN_ERR_LEN;
    }
    if (!j->play_armed) {
        return ATN_ERR_STATE;
    }
    s = jb_find(j, j->play_seq);
    if (s == NULL) {
        /* Gap — PLC one frame, advance. */
        plc_fill(j, pcm, ATN_VOICE_FRAME_SAMPLES);
        *out_n = ATN_VOICE_FRAME_SAMPLES;
        j->play_seq++;
        if (st != NULL) {
            st->frames_lost++;
        }
        return ATN_OK;
    }
    ns = (size_t)s->nbytes / 2u;
    if (ns > max_samples) {
        ns = max_samples;
    }
    for (i = 0; i < ns; i++) {
        pcm[i] = (int16_t)((uint16_t)s->data[2u * i] |
                           ((uint16_t)s->data[2u * i + 1u] << 8));
    }
    memcpy(j->last_pcm, pcm, ns * sizeof(int16_t));
    if (ns < ATN_VOICE_FRAME_SAMPLES) {
        atn_memzero(j->last_pcm + ns,
                    (ATN_VOICE_FRAME_SAMPLES - ns) * sizeof(int16_t));
    }
    j->have_last = 1;
    atn_memzero(s->data, sizeof(s->data));
    s->used = 0;
    j->play_seq++;
    *out_n = ns;
    if (st != NULL) {
        st->jitter_ms = jb_count(j) * ATN_VOICE_FRAME_MS;
    }
    return ATN_OK;
}
