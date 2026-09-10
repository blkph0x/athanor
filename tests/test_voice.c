/*
 * DEC-0049 voice gates: direct tunnel, hub forward, loss/reorder, rekey,
 * hangup wipe, frame size ≤ ATN_TUN_MAX_PT.
 */
#include "atn_voice.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_fail;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("ok   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fail++;
    }
}

static void fill_sine(int16_t *pcm, size_t n, double freq, double phase0)
{
    size_t i;
    for (i = 0; i < n; i++) {
        double t = phase0 + (double)i * freq * 2.0 * M_PI /
                   (double)ATN_VOICE_RATE_HZ;
        pcm[i] = (int16_t)(sin(t) * 10000.0);
    }
}

static int jb_empty_ok(const atn_voice *v)
{
    uint32_t i;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (v->jb.slot[i].used) {
            return 0;
        }
    }
    return 1;
}

static uint32_t jb_count_slots(const atn_voice_jitter *j)
{
    uint32_t i, n = 0;
    for (i = 0; i < ATN_VOICE_JB_SLOTS; i++) {
        if (j->slot[i].used) {
            n++;
        }
    }
    return n;
}

static int establish_pair(atn_tun *a, atn_tun *b)
{
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    int i, rc;
    if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
        return -1;
    }
    if (atn_tun_init_initiator(a, ek) != ATN_OK ||
        atn_tun_init_responder(b, dk) != ATN_OK) {
        return -1;
    }
    if (atn_tun_bind(a, 0) != ATN_OK || atn_tun_bind(b, 0) != ATN_OK) {
        return -1;
    }
    if (atn_tun_set_peer(a, 0x7f000001u, b->local_port) != ATN_OK ||
        atn_tun_set_peer(b, 0x7f000001u, a->local_port) != ATN_OK) {
        return -1;
    }
    if (atn_tun_hs_send_init(a) != ATN_OK) {
        return -1;
    }
    for (i = 0; i < 16 && b->state != ATN_TUN_ESTABLISHED; i++) {
        (void)atn_tun_pump(b, 3000);
    }
    for (i = 0; i < 8; i++) {
        (void)atn_tun_pump(b, 50);
    }
    rc = atn_tun_pump(a, 3000);
    if (rc != ATN_OK || a->state != ATN_TUN_ESTABLISHED ||
        b->state != ATN_TUN_ESTABLISHED) {
        return -1;
    }
    return 0;
}

static void drain_voice(atn_voice *v, int rounds)
{
    int i;
    for (i = 0; i < rounds; i++) {
        (void)atn_voice_pump(v, NULL, NULL, 0, 50);
    }
}

static void test_fail_without_tunnel(void)
{
    atn_tun t;
    atn_voice v;
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES];
    atn_memzero(&t, sizeof(t));
    t.state = ATN_TUN_CLOSED;
    atn_voice_init(&v, &t);
    check("no call if closed",
          atn_voice_call(&v, 1, ATN_VOICE_CODEC_PCM16) == ATN_ERR_STATE);
    fill_sine(pcm, ATN_VOICE_FRAME_SAMPLES, 1.0, 0);
    v.state = ATN_VOICE_ACTIVE;
    v.call_id = 1;
    check("no send if closed",
          atn_voice_send_pcm(&v, pcm, ATN_VOICE_FRAME_SAMPLES) == ATN_ERR_STATE);
    check("bad transition answer", atn_voice_answer(&v) == ATN_ERR_STATE);
    atn_voice_wipe(&v);
}

static void test_ima_roundtrip(void)
{
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES], out[ATN_VOICE_FRAME_SAMPLES];
    uint8_t buf[ATN_VOICE_IMA_BYTES];
    size_t n = 0, on = 0;
    int32_t pred = 0, sidx = 0;
    int i, err = 0;

    printf("--- IMA ADPCM ---\n");
    fill_sine(pcm, ATN_VOICE_FRAME_SAMPLES, 440.0, 0);
    check("ima enc",
          atn_voice_ima_encode(&pred, &sidx, pcm, ATN_VOICE_FRAME_SAMPLES, buf,
                               sizeof(buf), &n) == ATN_OK);
    check("ima size", n == ATN_VOICE_IMA_BYTES);
    check("ima+hdr <= MAX_PT", ATN_VOICE_AUDIO_HDR + n <= ATN_TUN_MAX_PT);
    check("ima dec",
          atn_voice_ima_decode(buf, n, out, ATN_VOICE_FRAME_SAMPLES, &on) ==
              ATN_OK &&
          on == ATN_VOICE_FRAME_SAMPLES);
    for (i = 0; i < (int)ATN_VOICE_FRAME_SAMPLES; i++) {
        int d = (int)pcm[i] - (int)out[i];
        if (d < 0) {
            d = -d;
        }
        if (d > 2000) {
            err++;
        }
    }
    check("ima approx", err < (int)ATN_VOICE_FRAME_SAMPLES / 4);
}

static void test_direct_bidirectional(void)
{
    atn_tun ta, tb;
    atn_voice va, vb;
    int16_t tx[ATN_VOICE_FRAME_SAMPLES], rx[ATN_VOICE_FRAME_SAMPLES];
    size_t n = 0;
    int i, got = 0;
    uint8_t wire[ATN_TUN_MAX_PT];
    uint8_t payload[ATN_VOICE_PCM_BYTES];
    size_t plen = 0, wn = 0;

    printf("--- direct A<->B PCM ---\n");
    check("pair", establish_pair(&ta, &tb) == 0);
    atn_voice_init(&va, &ta);
    atn_voice_init(&vb, &tb);
    check("call", atn_voice_call(&va, 42, ATN_VOICE_CODEC_PCM16) == ATN_OK);
    drain_voice(&vb, 8);
    check("ringing", atn_voice_state(&vb) == ATN_VOICE_RINGING);
    check("answer", atn_voice_answer(&vb) == ATN_OK);
    drain_voice(&va, 8);
    check("a active", atn_voice_state(&va) == ATN_VOICE_ACTIVE);
    check("b active", atn_voice_state(&vb) == ATN_VOICE_ACTIVE);

    for (i = 0; i < 12; i++) {
        fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, (double)i * 0.1);
        check(i == 0 ? "send pcm" : "send ok",
              atn_voice_send_pcm(&va, tx, ATN_VOICE_FRAME_SAMPLES) == ATN_OK);
        drain_voice(&vb, 4);
        if (atn_voice_receive_pcm(&vb, rx, ATN_VOICE_FRAME_SAMPLES, &n) ==
            ATN_OK) {
            got++;
        }
        fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 880.0, (double)i * 0.1);
        check("send b",
              atn_voice_send_pcm(&vb, tx, ATN_VOICE_FRAME_SAMPLES) == ATN_OK);
        drain_voice(&va, 4);
        (void)atn_voice_receive_pcm(&va, rx, ATN_VOICE_FRAME_SAMPLES, &n);
    }
    check("got playout", got >= 4);

    fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, 0);
    check("pcm pack",
          atn_voice_pcm16_pack(tx, ATN_VOICE_FRAME_SAMPLES, payload,
                               sizeof(payload), &plen) == ATN_OK);
    check("audio encode",
          atn_voice_encode_audio(1, 0, 0, ATN_VOICE_CODEC_PCM16, payload, plen,
                                 wire, sizeof(wire), &wn) == ATN_OK);
    check("frame <= MAX_PT", wn <= ATN_TUN_MAX_PT);
    check("pcm frame size", wn == ATN_VOICE_AUDIO_HDR + ATN_VOICE_PCM_BYTES);

    check("hangup", atn_voice_hangup(&va) == ATN_OK);
    drain_voice(&vb, 4);
    check("b idle after hangup", atn_voice_state(&vb) == ATN_VOICE_IDLE);
    check("jb cleared", vb.jb.play_armed == 0 && jb_empty_ok(&vb));

    atn_voice_wipe(&va);
    atn_voice_wipe(&vb);
    atn_tun_wipe(&ta);
    atn_tun_wipe(&tb);
}

static int hub_forward_opaque(atn_tun *ha, atn_tun *hb, const int16_t *pcm_pat,
                              int *saw_clear_pcm)
{
    uint8_t pt[ATN_TUN_MAX_PT];
    size_t n = 0;
    int rc = atn_tun_recv_data(ha, pt, &n, sizeof(pt), 100);
    if (rc == ATN_OK && n > 0 && pt[0] == ATN_VOICE_WIRE) {
        /* Hub must not see clear PCM16 sine in plaintext DATA. */
        if (saw_clear_pcm != NULL && pcm_pat != NULL && n >= 64) {
            size_t i;
            uint8_t raw[64];
            for (i = 0; i < 32; i++) {
                raw[2u * i] = (uint8_t)(pcm_pat[i] & 0xff);
                raw[2u * i + 1u] = (uint8_t)((pcm_pat[i] >> 8) & 0xff);
            }
            for (i = 0; i + 64u <= n; i++) {
                if (memcmp(pt + i, raw, 64) == 0) {
                    *saw_clear_pcm = 1;
                }
            }
        }
        (void)atn_tun_send(hb, pt, n);
        atn_memzero(pt, n);
        return 1;
    }
    if (n > 0) {
        atn_memzero(pt, n);
    }
    n = 0;
    rc = atn_tun_recv_data(hb, pt, &n, sizeof(pt), 100);
    if (rc == ATN_OK && n > 0 && pt[0] == ATN_VOICE_WIRE) {
        if (saw_clear_pcm != NULL && pcm_pat != NULL && n >= 64) {
            size_t i;
            uint8_t raw[64];
            for (i = 0; i < 32; i++) {
                raw[2u * i] = (uint8_t)(pcm_pat[i] & 0xff);
                raw[2u * i + 1u] = (uint8_t)((pcm_pat[i] >> 8) & 0xff);
            }
            for (i = 0; i + 64u <= n; i++) {
                if (memcmp(pt + i, raw, 64) == 0) {
                    *saw_clear_pcm = 1;
                }
            }
        }
        (void)atn_tun_send(ha, pt, n);
        atn_memzero(pt, n);
        return 1;
    }
    if (n > 0) {
        atn_memzero(pt, n);
    }
    return 0;
}

static void test_opaque_hub_relay(void)
{
    atn_tun a, ha, hb, b;
    atn_voice va, vb;
    uint8_t tek1[ATN_MLKEM1024_EK_LEN], tdk1[ATN_MLKEM1024_DK_LEN];
    uint8_t tek2[ATN_MLKEM1024_EK_LEN], tdk2[ATN_MLKEM1024_DK_LEN];
    uint8_t vek[ATN_MLKEM1024_EK_LEN], vdk[ATN_MLKEM1024_DK_LEN];
    int16_t tx[ATN_VOICE_FRAME_SAMPLES], rx[ATN_VOICE_FRAME_SAMPLES];
    size_t n = 0;
    int i, got = 0, saw_clear = 0;

    printf("--- opaque hub relay (hub cannot decode PCM) ---\n");
    check("tun kem1", atn_mlkem1024_keygen(tek1, tdk1) == ATN_OK);
    check("tun kem2", atn_mlkem1024_keygen(tek2, tdk2) == ATN_OK);
    check("voice kem", atn_mlkem1024_keygen(vek, vdk) == ATN_OK);

    check("init a", atn_tun_init_initiator(&a, tek1) == ATN_OK);
    check("init ha", atn_tun_init_responder(&ha, tdk1) == ATN_OK);
    check("bind a", atn_tun_bind(&a, 0) == ATN_OK);
    check("bind ha", atn_tun_bind(&ha, 0) == ATN_OK);
    check("peer a", atn_tun_set_peer(&a, 0x7f000001u, ha.local_port) == ATN_OK);
    check("peer ha", atn_tun_set_peer(&ha, 0x7f000001u, a.local_port) == ATN_OK);
    check("hs a", atn_tun_hs_send_init(&a) == ATN_OK);
    for (i = 0; i < 16 && ha.state != ATN_TUN_ESTABLISHED; i++) {
        (void)atn_tun_pump(&ha, 3000);
    }
    for (i = 0; i < 8; i++) {
        (void)atn_tun_pump(&ha, 50);
    }
    check("a est", atn_tun_pump(&a, 3000) == ATN_OK &&
          a.state == ATN_TUN_ESTABLISHED);

    check("init b", atn_tun_init_initiator(&b, tek2) == ATN_OK);
    check("init hb", atn_tun_init_responder(&hb, tdk2) == ATN_OK);
    check("bind b", atn_tun_bind(&b, 0) == ATN_OK);
    check("bind hb", atn_tun_bind(&hb, 0) == ATN_OK);
    check("peer b", atn_tun_set_peer(&b, 0x7f000001u, hb.local_port) == ATN_OK);
    check("peer hb", atn_tun_set_peer(&hb, 0x7f000001u, b.local_port) == ATN_OK);
    check("hs b", atn_tun_hs_send_init(&b) == ATN_OK);
    for (i = 0; i < 16 && hb.state != ATN_TUN_ESTABLISHED; i++) {
        (void)atn_tun_pump(&hb, 3000);
    }
    for (i = 0; i < 8; i++) {
        (void)atn_tun_pump(&hb, 50);
    }
    check("b est", atn_tun_pump(&b, 3000) == ATN_OK &&
          b.state == ATN_TUN_ESTABLISHED);

    atn_voice_init(&vb, &b);
    check("relay start",
          atn_voice_start_relay(&va, &a, vek, 77, ATN_VOICE_CODEC_PCM16) ==
              ATN_OK);
    check("relay path A", atn_voice_path(&va) == ATN_VOICE_PATH_RELAY);

    for (i = 0; i < 64; i++) {
        (void)hub_forward_opaque(&ha, &hb, NULL, NULL);
        (void)atn_voice_pump(&vb, NULL, NULL, 0, 20);
    }
    check("relay ringing", atn_voice_state(&vb) == ATN_VOICE_RINGING);
    check("ct assembled",
          vb.e2e.ct_bits == (uint8_t)((1u << ATN_VOICE_E2E_CT_NCHUNKS) - 1u));
    check("ct matches",
          memcmp(va.e2e.ct, vb.e2e.ct, ATN_MLKEM1024_CT_LEN) == 0);
    check("accept relay", atn_voice_accept_relay(&vb, vdk) == ATN_OK);
    for (i = 0; i < 32; i++) {
        (void)hub_forward_opaque(&ha, &hb, NULL, NULL);
        (void)atn_voice_pump(&va, NULL, NULL, 0, 20);
    }
    check("relay active", atn_voice_state(&va) == ATN_VOICE_ACTIVE &&
          atn_voice_state(&vb) == ATN_VOICE_ACTIVE);

    fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, 0);
    /* Prove seal/unseal alone before path media. */
    {
        uint8_t payload[ATN_VOICE_PCM_BYTES];
        uint8_t sealed[ATN_TUN_MAX_PT];
        uint8_t back[ATN_VOICE_PCM_BYTES];
        size_t plen = 0, sn = 0, blen = 0;
        uint32_t cid = 0, seq = 0, ts = 0;
        uint8_t codec = 0;
        check("pcm for seal",
              atn_voice_pcm16_pack(tx, ATN_VOICE_FRAME_SAMPLES, payload,
                                   sizeof(payload), &plen) == ATN_OK);
        check("seal once",
              atn_voice_seal_audio(&va.e2e, 77, 0, 0, ATN_VOICE_CODEC_PCM16,
                                   payload, plen, sealed, sizeof(sealed),
                                   &sn) == ATN_OK);
        check("unseal once",
              atn_voice_unseal_audio(&vb.e2e, sealed, sn, &cid, &seq, &ts,
                                     &codec, back, sizeof(back), &blen) ==
                  ATN_OK &&
              blen == plen && cid == 77);
        atn_memzero(payload, sizeof(payload));
        atn_memzero(sealed, sn);
        atn_memzero(back, sizeof(back));
    }
    for (i = 0; i < 16; i++) {
        fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, (double)i);
        check("sealed send",
              atn_voice_send_pcm(&va, tx, ATN_VOICE_FRAME_SAMPLES) == ATN_OK);
        {
            int k;
            for (k = 0; k < 4; k++) {
                hub_forward_opaque(&ha, &hb, tx, &saw_clear);
                (void)atn_voice_pump(&vb, NULL, NULL, 0, 50);
            }
        }
        if (atn_voice_receive_pcm(&vb, rx, ATN_VOICE_FRAME_SAMPLES, &n) ==
            ATN_OK) {
            got++;
        }
    }
    check("hub never saw clear PCM", saw_clear == 0);
    check("sealed playout", got >= 3);
    check("B path relay", atn_voice_path(&vb) == ATN_VOICE_PATH_RELAY);

    atn_voice_wipe(&va);
    atn_voice_wipe(&vb);
    atn_tun_wipe(&a);
    atn_tun_wipe(&ha);
    atn_tun_wipe(&hb);
    atn_tun_wipe(&b);
    atn_memzero(tdk1, sizeof(tdk1));
    atn_memzero(tdk2, sizeof(tdk2));
    atn_memzero(vdk, sizeof(vdk));
}

static void test_latency_rank(void)
{
    atn_voice_hub_rtt hubs[4];
    printf("--- latency hub ranking ---\n");
    atn_memzero(hubs, sizeof(hubs));
    hubs[0].ipv4_host = 1;
    hubs[0].rtt_ms = 80;
    hubs[0].ok = 1;
    hubs[1].ipv4_host = 2;
    hubs[1].rtt_ms = 20;
    hubs[1].ok = 1;
    hubs[2].ipv4_host = 3;
    hubs[2].ok = 0;
    hubs[3].ipv4_host = 4;
    hubs[3].rtt_ms = 40;
    hubs[3].ok = 1;
    atn_voice_rank_hubs(hubs, 4);
    check("best first", hubs[0].ipv4_host == 2 && hubs[0].rtt_ms == 20);
    check("second", hubs[1].ipv4_host == 4);
    check("third", hubs[2].ipv4_host == 1);
    check("failed last", hubs[3].ok == 0);
}

static void test_roster(void)
{
    atn_voice_roster r;
    atn_voice_contact c;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    char line[8192];
    char hex[ATN_MLKEM1024_EK_LEN * 2u + 1u];
    size_t i;
    const atn_voice_contact *f;
    printf("--- contact roster ---\n");
    check("kem roster", atn_mlkem1024_keygen(ek, dk) == ATN_OK);
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        static const char *h = "0123456789abcdef";
        hex[2u * i] = h[(ek[i] >> 4) & 15];
        hex[2u * i + 1u] = h[ek[i] & 15];
    }
    hex[ATN_MLKEM1024_EK_LEN * 2u] = '\0';
    snprintf(line, sizeof(line), "alice 127.0.0.1 46000 %s 10.0.0.1 46001\n",
             hex);
    check("load roster",
          atn_voice_roster_load_text(&r, line, strlen(line)) == ATN_OK &&
              r.n == 1);
    f = atn_voice_roster_find(&r, "alice");
    check("find alice", f != NULL && f->port == 46000 && f->nhubs == 1);
    check("parse bad", atn_voice_roster_parse_line("# comment", &c) != ATN_OK);
    atn_memzero(dk, sizeof(dk));
}

static void test_malformed(void)
{
    atn_tun ta, tb;
    atn_voice va, vb;
    uint8_t bad[32];
    uint8_t wire[ATN_TUN_MAX_PT];
    uint8_t payload[ATN_VOICE_PCM_BYTES];
    size_t plen = 0, wn = 0;
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES];
    int16_t out[ATN_VOICE_FRAME_SAMPLES];
    size_t on = 0;
    atn_voice_stats st;
    int k;

    printf("--- malformed / duplicate / stale ---\n");
    check("pair m", establish_pair(&ta, &tb) == 0);
    atn_voice_init(&va, &ta);
    atn_voice_init(&vb, &tb);
    check("call m", atn_voice_call(&va, 9, ATN_VOICE_CODEC_PCM16) == ATN_OK);
    drain_voice(&vb, 8);
    check("ans m", atn_voice_answer(&vb) == ATN_OK);
    drain_voice(&va, 8);

    memset(bad, 0, sizeof(bad));
    bad[0] = ATN_VOICE_WIRE;
    bad[1] = ATN_VOICE_CTRL;
    bad[2] = 99;
    check("bad ctrl", atn_voice_on_frame(&vb, bad, 12) != ATN_OK);

    fill_sine(pcm, ATN_VOICE_FRAME_SAMPLES, 100.0, 0);
    check("pack",
          atn_voice_pcm16_pack(pcm, ATN_VOICE_FRAME_SAMPLES, payload,
                               sizeof(payload), &plen) == ATN_OK);
    check("enc",
          atn_voice_encode_audio(9, 0, 0, ATN_VOICE_CODEC_PCM16, payload, plen,
                                 wire, sizeof(wire), &wn) == ATN_OK);
    check("first audio", atn_voice_on_frame(&vb, wire, wn) == ATN_OK);
    check("dup audio", atn_voice_on_frame(&vb, wire, wn) != ATN_OK);

    for (k = 1; k < 6; k++) {
        check("seq",
              atn_voice_encode_audio(9, (uint32_t)k, (uint32_t)k * 320u,
                                     ATN_VOICE_CODEC_PCM16, payload, plen, wire,
                                     sizeof(wire), &wn) == ATN_OK);
        (void)atn_voice_on_frame(&vb, wire, wn);
    }
    for (k = 0; k < 8; k++) {
        (void)atn_voice_receive_pcm(&vb, out, ATN_VOICE_FRAME_SAMPLES, &on);
    }
    check("stale enc",
          atn_voice_encode_audio(9, 0, 0, ATN_VOICE_CODEC_PCM16, payload, plen,
                                 wire, sizeof(wire), &wn) == ATN_OK);
    check("stale reject", atn_voice_on_frame(&vb, wire, wn) != ATN_OK);
    atn_voice_get_stats(&vb, &st);
    check("drops counted", st.frames_dropped > 0);

    atn_voice_wipe(&va);
    atn_voice_wipe(&vb);
    atn_tun_wipe(&ta);
    atn_tun_wipe(&tb);
}

static void test_loss_reorder(int loss_pct)
{
    atn_voice_jitter j;
    atn_voice_stats st;
    int16_t pcm[ATN_VOICE_FRAME_SAMPLES], out[ATN_VOICE_FRAME_SAMPLES];
    size_t on = 0;
    int i, popped = 0;
    uint32_t seqs[64];
    int nseq = 40;
    char name[64];

    atn_memzero(&st, sizeof(st));
    atn_voice_jitter_init(&j);
    fill_sine(pcm, ATN_VOICE_FRAME_SAMPLES, 440.0, 0);

    for (i = 0; i < nseq; i++) {
        seqs[i] = (uint32_t)i;
    }
    for (i = 0; i + 1 < nseq; i += 2) {
        if ((i / 2) % 3 == 0) {
            uint32_t t = seqs[i];
            seqs[i] = seqs[i + 1];
            seqs[i + 1] = t;
        }
    }
    for (i = 0; i < nseq; i++) {
        int drop = 0;
        if (loss_pct > 0) {
            drop = ((int)(seqs[i] * 17u + 3u) % 100) < loss_pct;
        }
        if (drop) {
            continue;
        }
        (void)atn_voice_jitter_push(&j, seqs[i], seqs[i] * 320u,
                                    ATN_VOICE_CODEC_PCM16, pcm,
                                    ATN_VOICE_FRAME_SAMPLES, &st);
    }
    for (i = 0; i < 50; i++) {
        if (atn_voice_jitter_pop(&j, out, ATN_VOICE_FRAME_SAMPLES, &on, &st) ==
            ATN_OK) {
            popped++;
        } else {
            break;
        }
    }
    snprintf(name, sizeof(name), "loss %d%% playout", loss_pct);
    check(name, popped > 0 && popped <= 50);
    snprintf(name, sizeof(name), "loss %d%% bounded", loss_pct);
    check(name, jb_count_slots(&j) <= ATN_VOICE_JB_SLOTS &&
          j.depth_max <= ATN_VOICE_JB_SLOTS);
    atn_voice_jitter_reset(&j);
}

static void test_rekey_during_call(void)
{
    atn_tun ta, tb;
    atn_voice va, vb;
    int16_t tx[ATN_VOICE_FRAME_SAMPLES], rx[ATN_VOICE_FRAME_SAMPLES];
    size_t n = 0;
    int i, j, rc, got = 0;

    printf("--- rekey during active call ---\n");
    check("pair rk", establish_pair(&ta, &tb) == 0);
    atn_voice_init(&va, &ta);
    atn_voice_init(&vb, &tb);
    check("call rk", atn_voice_call(&va, 11, ATN_VOICE_CODEC_PCM16) == ATN_OK);
    drain_voice(&vb, 8);
    check("ans rk", atn_voice_answer(&vb) == ATN_OK);
    drain_voice(&va, 8);

    for (i = 0; i < 4; i++) {
        fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, (double)i);
        (void)atn_voice_send_pcm(&va, tx, ATN_VOICE_FRAME_SAMPLES);
        drain_voice(&vb, 4);
        (void)atn_voice_receive_pcm(&vb, rx, ATN_VOICE_FRAME_SAMPLES, &n);
    }

    check("rekey", atn_tun_rekey_send(&ta) == ATN_OK && ta.rekey_pending);
    rc = ATN_ERR_STATE;
    for (j = 0; j < 16; j++) {
        rc = atn_tun_pump(&tb, 3000);
        if (rc == ATN_OK && tb.state == ATN_TUN_ESTABLISHED && tb.send_seq == 1 &&
            tb.hs_asm_bits == 0) {
            break;
        }
    }
    check("B rekey", rc == ATN_OK);
    rc = ATN_ERR_STATE;
    for (j = 0; j < 8; j++) {
        rc = atn_tun_pump(&ta, 500);
        if (rc == ATN_OK && !ta.rekey_pending && ta.send_seq == 1) {
            break;
        }
    }
    check("A rekey ACK", rc == ATN_OK && !ta.rekey_pending);
    check("still active", atn_voice_state(&va) == ATN_VOICE_ACTIVE &&
          atn_voice_state(&vb) == ATN_VOICE_ACTIVE);

    for (i = 0; i < 8; i++) {
        fill_sine(tx, ATN_VOICE_FRAME_SAMPLES, 440.0, (double)i + 10);
        check("post-rekey send",
              atn_voice_send_pcm(&va, tx, ATN_VOICE_FRAME_SAMPLES) == ATN_OK);
        drain_voice(&vb, 4);
        if (atn_voice_receive_pcm(&vb, rx, ATN_VOICE_FRAME_SAMPLES, &n) ==
            ATN_OK) {
            got++;
        }
    }
    check("post-rekey playout", got >= 2);

    atn_voice_wipe(&va);
    atn_voice_wipe(&vb);
    atn_tun_wipe(&ta);
    atn_tun_wipe(&tb);
}

/* Soft config: retarget JB mid-call; PROBE/PROBE_ACK RTT path stays live. */
static void test_soft_jb_and_probe(void)
{
    atn_tun ta, tb;
    atn_voice va, vb;
    uint8_t ctrl[ATN_VOICE_CTRL_LEN];
    size_t cn = 0;

    printf("--- soft jb / probe ---\n");
    check("soft pair", establish_pair(&ta, &tb) == 0);
    atn_voice_init(&va, &ta);
    atn_voice_init(&vb, &tb);
    check("soft call", atn_voice_call(&va, 77, ATN_VOICE_CODEC_PCM16) == ATN_OK);
    drain_voice(&vb, 8);
    check("soft ringing", atn_voice_state(&vb) == ATN_VOICE_RINGING);
    check("soft answer", atn_voice_answer(&vb) == ATN_OK);
    drain_voice(&va, 8);
    check("soft active a", atn_voice_state(&va) == ATN_VOICE_ACTIVE);
    check("soft active b", atn_voice_state(&vb) == ATN_VOICE_ACTIVE);

    check("default jb 60", atn_voice_jb_target_ms(&va) == 60);
    check("set jb 240", atn_voice_jb_set_target_ms(&va, 240) == ATN_OK);
    check("jb now 240", atn_voice_jb_target_ms(&va) == 240);
    check("set jb 80", atn_voice_jb_set_target_ms(&va, 80) == ATN_OK);
    check("jb now 80", atn_voice_jb_target_ms(&va) == 80);
    /* Soft clamp — out-of-range retargets to JB_MIN / JB_MAX. */
    check("clamp tiny", atn_voice_jb_set_target_ms(&va, 10) == ATN_OK);
    check("jb min 40", atn_voice_jb_target_ms(&va) == 40);
    check("clamp huge", atn_voice_jb_set_target_ms(&va, 2000) == ATN_OK);
    check("jb max 480", atn_voice_jb_target_ms(&va) == 480);

    check("encode probe",
          atn_voice_encode_ctrl(ATN_VOICE_OP_PROBE, ATN_VOICE_CODEC_PCM16, 77,
                                ctrl, sizeof(ctrl), &cn) == ATN_OK);
    check("send probe", atn_tun_send(&ta, ctrl, cn) == ATN_OK);
    drain_voice(&vb, 8);
    drain_voice(&va, 8);
    check("still active after probe", atn_voice_state(&va) == ATN_VOICE_ACTIVE);
    check("peer still active", atn_voice_state(&vb) == ATN_VOICE_ACTIVE);

    check("hold soft", atn_voice_set_hold(&va, 1) == ATN_OK);
    check("held", atn_voice_state(&va) == ATN_VOICE_HOLD);
    check("unhold soft", atn_voice_set_hold(&va, 0) == ATN_OK);
    check("active again", atn_voice_state(&va) == ATN_VOICE_ACTIVE);

    atn_voice_wipe(&va);
    atn_voice_wipe(&vb);
    atn_tun_wipe(&ta);
    atn_tun_wipe(&tb);
}

int main(void)
{
    printf("athanor voice  platform=%s\n", atn_platform_id());
    check("net init", atn_net_init() == ATN_OK);

    test_fail_without_tunnel();
    test_ima_roundtrip();
    test_latency_rank();
    test_roster();
    test_direct_bidirectional();
    test_opaque_hub_relay();
    test_malformed();
    printf("--- loss / reorder / jitter ---\n");
    test_loss_reorder(0);
    test_loss_reorder(1);
    test_loss_reorder(5);
    test_loss_reorder(10);
    test_rekey_during_call();
    test_soft_jb_and_probe();

    atn_net_fini();
    if (g_fail) {
        printf("%d FAILED\n", g_fail);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
