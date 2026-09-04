/*
 * REQ-3.3 verification: three nodes stay live; forged MAC ignored;
 * silence → UNTRUSTED then self-wipe of the silent node only.
 */
#include "atn_hb.h"
#include "atn_platform.h"

#include <stdio.h>
#include <string.h>

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

static void wr64(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static int pack(atn_hb *h, uint64_t b, uint8_t tok[1 + 8 + 8 + 32 + ATN_HB_MAC_LEN])
{
    uint8_t mac[ATN_HB_MAC_LEN];
    if (atn_hb_token(h, b, mac) != ATN_OK) {
        return -1;
    }
    tok[0] = ATN_HB_WIRE_H;
    wr64(tok + 1, b);
    wr64(tok + 9, h->epoch);
    memcpy(tok + 17, h->head, 32);
    memcpy(tok + 49, mac, ATN_HB_MAC_LEN);
    return 0;
}

static int deliver(atn_hb *from, atn_hb *to, uint64_t b)
{
    uint8_t tok[1 + 8 + 8 + 32 + ATN_HB_MAC_LEN];
    if (from->state == ATN_HB_DEAD || to->state == ATN_HB_DEAD) {
        return 0;
    }
    if (pack(from, b, tok) != 0) {
        return -1;
    }
    return atn_hb_ingest(to, tok, sizeof(tok)) == ATN_OK ? 0 : -1;
}

static int hs_pair(atn_tun *init, atn_tun *resp)
{
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
        return -1;
    }
    if (atn_tun_init_initiator(init, ek) != ATN_OK ||
        atn_tun_init_responder(resp, dk) != ATN_OK) {
        return -1;
    }
    if (atn_tun_bind(init, 0) != ATN_OK || atn_tun_bind(resp, 0) != ATN_OK) {
        return -1;
    }
    if (atn_tun_set_peer(init, 0x7f000001u, resp->local_port) != ATN_OK ||
        atn_tun_set_peer(resp, 0x7f000001u, init->local_port) != ATN_OK) {
        return -1;
    }
    if (atn_tun_hs_send_init(init) != ATN_OK) {
        return -1;
    }
    if (atn_tun_pump(resp, 3000) != ATN_OK ||
        atn_tun_pump(init, 3000) != ATN_OK) {
        return -1;
    }
    return 0;
}

static int emit_on(atn_hb *h, atn_tun *t, uint64_t bucket)
{
    h->tun = t;
    return atn_hb_emit(h, bucket) == ATN_OK ? 0 : -1;
}

static int pump_on(atn_hb *h, atn_tun *t)
{
    int rc;
    h->tun = t;
    rc = atn_hb_pump(h, 1000);
    return rc == ATN_OK ? 0 : -1;
}

static int round3(atn_hb *x, atn_hb *y, atn_hb *z, uint64_t b)
{
    if (deliver(x, y, b) != 0 || deliver(x, z, b) != 0) {
        return -1;
    }
    if (deliver(y, x, b) != 0 || deliver(y, z, b) != 0) {
        return -1;
    }
    if (deliver(z, x, b) != 0 || deliver(z, y, b) != 0) {
        return -1;
    }
    if (x->state != ATN_HB_DEAD && atn_hb_tick(x, b) != ATN_OK) {
        return -1;
    }
    if (y->state != ATN_HB_DEAD && atn_hb_tick(y, b) != ATN_OK) {
        return -1;
    }
    if (z->state != ATN_HB_DEAD && atn_hb_tick(z, b) != ATN_OK) {
        return -1;
    }
    return 0;
}

int main(void)
{
    atn_hb a, b, c;
    uint8_t ida[8], idb[8], idc[8];
    uint8_t ka[32], kb[32], kc[32], head[32];
    uint8_t bad[1 + 8 + 8 + 32 + ATN_HB_MAC_LEN];
    uint64_t t;
    int rc;

    printf("athanor hb  platform=%s\n", atn_platform_id());
    memset(ida, 0xa1, 8);
    memset(idb, 0xb2, 8);
    memset(idc, 0xc3, 8);
    check("rng",
          atn_random_bytes(ka, 32) == ATN_OK &&
          atn_random_bytes(kb, 32) == ATN_OK &&
          atn_random_bytes(kc, 32) == ATN_OK &&
          atn_random_bytes(head, 32) == ATN_OK);
    check("init A", atn_hb_init(&a, ida, ka, 1, head, NULL) == ATN_OK);
    check("init B", atn_hb_init(&b, idb, kb, 1, head, NULL) == ATN_OK);
    check("init C", atn_hb_init(&c, idc, kc, 1, head, NULL) == ATN_OK);
    check("peers A",
          atn_hb_add_peer(&a, idb, kb) == ATN_OK &&
          atn_hb_add_peer(&a, idc, kc) == ATN_OK);
    check("peers B",
          atn_hb_add_peer(&b, ida, ka) == ATN_OK &&
          atn_hb_add_peer(&b, idc, kc) == ATN_OK);
    check("peers C",
          atn_hb_add_peer(&c, ida, ka) == ATN_OK &&
          atn_hb_add_peer(&c, idb, kb) == ATN_OK);

    rc = 0;
    for (t = 1; t <= 4; t++) {
        if (round3(&a, &b, &c, t) != 0) {
            rc = -1;
            break;
        }
    }
    check("three nodes live",
          rc == 0 && a.state == ATN_HB_LIVE && b.state == ATN_HB_LIVE &&
          c.state == ATN_HB_LIVE);

    check("lossy full round", round3(&a, &b, &c, 5) == 0);
    check("pack forge base", pack(&a, 6, bad) == 0);
    bad[49] ^= 0xff;
    check("forged ignored", atn_hb_ingest(&b, bad, sizeof(bad)) == ATN_ERR_AUTH);
    check("B still live", b.state == ATN_HB_LIVE);

    /* Silence C: A and B keep talking; C hears nothing. */
    for (t = 10; t < 10u + ATN_HB_N + ATN_HB_M; t++) {
        if (deliver(&a, &b, t) != 0 || deliver(&b, &a, t) != 0) {
            rc = -1;
        }
        (void)atn_hb_tick(&a, t);
        (void)atn_hb_tick(&b, t);
        (void)atn_hb_tick(&c, t);
    }
    check("silence loop", rc == 0);
    check("C untrusted on A", atn_hb_peer_state(&a, idc) >= ATN_HB_UNTRUSTED);
    check("C untrusted on B", atn_hb_peer_state(&b, idc) >= ATN_HB_UNTRUSTED);
    check("A B still live", a.state == ATN_HB_LIVE && b.state == ATN_HB_LIVE);
    check("C wiped itself", c.state == ATN_HB_DEAD);
    {
        uint8_t z[32];
        memset(z, 0, 32);
        check("C key zeroed", memcmp(c.key, z, 32) == 0);
        check("A key live", memcmp(a.key, z, 32) != 0);
    }

    /* One tunnel hop: emit on DATA, peer pump verifies MAC. */
    {
        atn_tun ta, tb;
        atn_hb ha, hb;
        uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
        check("net", atn_net_init() == ATN_OK);
        check("kem", atn_mlkem1024_keygen(ek, dk) == ATN_OK);
        check("tun A", atn_tun_init_initiator(&ta, ek) == ATN_OK);
        check("tun B", atn_tun_init_responder(&tb, dk) == ATN_OK);
        check("bind", atn_tun_bind(&ta, 0) == ATN_OK && atn_tun_bind(&tb, 0) == ATN_OK);
        check("peer",
              atn_tun_set_peer(&ta, 0x7f000001u, tb.local_port) == ATN_OK &&
              atn_tun_set_peer(&tb, 0x7f000001u, ta.local_port) == ATN_OK);
        check("hs", atn_tun_hs_send_init(&ta) == ATN_OK &&
              atn_tun_pump(&tb, 3000) == ATN_OK &&
              atn_tun_pump(&ta, 3000) == ATN_OK);
        check("hb tun A", atn_hb_init(&ha, ida, ka, 1, head, &ta) == ATN_OK);
        check("hb tun B", atn_hb_init(&hb, idb, kb, 1, head, &tb) == ATN_OK);
        check("peer keys",
              atn_hb_add_peer(&ha, idb, kb) == ATN_OK &&
              atn_hb_add_peer(&hb, ida, ka) == ATN_OK);
        check("emit", atn_hb_emit(&ha, 1) == ATN_OK);
        check("pump MAC", atn_hb_pump(&hb, 3000) == ATN_OK);
        atn_hb_wipe(&ha);
        atn_hb_wipe(&hb);
        atn_tun_wipe(&ta);
        atn_tun_wipe(&tb);
        atn_net_fini();
    }

    /* ISS-0015: three UDP pairs (AB, AC, BC) + lossy drop of AC. */
    {
        atn_tun ab_a, ab_b, ac_a, ac_c, bc_b, bc_c;
        atn_hb ha, hb, hc;
        uint64_t u;
        int mesh_ok = 1;
        check("mesh net", atn_net_init() == ATN_OK);
        check("mesh hs AB", hs_pair(&ab_a, &ab_b) == 0);
        check("mesh hs AC", hs_pair(&ac_a, &ac_c) == 0);
        check("mesh hs BC", hs_pair(&bc_b, &bc_c) == 0);
        check("mesh hb",
              atn_hb_init(&ha, ida, ka, 1, head, NULL) == ATN_OK &&
              atn_hb_init(&hb, idb, kb, 1, head, NULL) == ATN_OK &&
              atn_hb_init(&hc, idc, kc, 1, head, NULL) == ATN_OK);
        check("mesh peers",
              atn_hb_add_peer(&ha, idb, kb) == ATN_OK &&
              atn_hb_add_peer(&ha, idc, kc) == ATN_OK &&
              atn_hb_add_peer(&hb, ida, ka) == ATN_OK &&
              atn_hb_add_peer(&hb, idc, kc) == ATN_OK &&
              atn_hb_add_peer(&hc, ida, ka) == ATN_OK &&
              atn_hb_add_peer(&hc, idb, kb) == ATN_OK);
        for (u = 20; u <= 22; u++) {
            if (emit_on(&ha, &ab_a, u) != 0 || emit_on(&ha, &ac_a, u) != 0 ||
                emit_on(&hb, &ab_b, u) != 0 || emit_on(&hb, &bc_b, u) != 0 ||
                emit_on(&hc, &ac_c, u) != 0 || emit_on(&hc, &bc_c, u) != 0) {
                mesh_ok = 0;
            }
            if (pump_on(&hb, &ab_b) != 0 || pump_on(&hc, &ac_c) != 0 ||
                pump_on(&ha, &ab_a) != 0 || pump_on(&hc, &bc_c) != 0 ||
                pump_on(&ha, &ac_a) != 0 || pump_on(&hb, &bc_b) != 0) {
                mesh_ok = 0;
            }
            if (atn_hb_tick(&ha, u) != ATN_OK ||
                atn_hb_tick(&hb, u) != ATN_OK ||
                atn_hb_tick(&hc, u) != ATN_OK) {
                mesh_ok = 0;
            }
        }
        check("mesh 3-pair live",
              mesh_ok && ha.state == ATN_HB_LIVE && hb.state == ATN_HB_LIVE &&
              hc.state == ATN_HB_LIVE);
        /* Drop AC both ways for N buckets; AB and BC still flow. */
        for (u = 23; u < 23u + ATN_HB_N; u++) {
            if (emit_on(&ha, &ab_a, u) != 0 ||
                emit_on(&hb, &ab_b, u) != 0 || emit_on(&hb, &bc_b, u) != 0 ||
                emit_on(&hc, &bc_c, u) != 0) {
                mesh_ok = 0;
            }
            if (pump_on(&hb, &ab_b) != 0 || pump_on(&ha, &ab_a) != 0 ||
                pump_on(&hc, &bc_c) != 0 || pump_on(&hb, &bc_b) != 0) {
                mesh_ok = 0;
            }
            (void)atn_hb_tick(&ha, u);
            (void)atn_hb_tick(&hb, u);
            (void)atn_hb_tick(&hc, u);
        }
        check("lossy AC drop", mesh_ok);
        check("A marks C untrusted",
              atn_hb_peer_state(&ha, idc) >= ATN_HB_UNTRUSTED);
        check("C marks A untrusted",
              atn_hb_peer_state(&hc, ida) >= ATN_HB_UNTRUSTED);
        check("B still trusts A", atn_hb_peer_state(&hb, ida) == ATN_HB_LIVE);
        check("nodes stay live",
              ha.state == ATN_HB_LIVE && hb.state == ATN_HB_LIVE &&
              hc.state == ATN_HB_LIVE);
        atn_hb_wipe(&ha);
        atn_hb_wipe(&hb);
        atn_hb_wipe(&hc);
        atn_tun_wipe(&ab_a);
        atn_tun_wipe(&ab_b);
        atn_tun_wipe(&ac_a);
        atn_tun_wipe(&ac_c);
        atn_tun_wipe(&bc_b);
        atn_tun_wipe(&bc_c);
        atn_net_fini();
    }

    atn_hb_wipe(&a);
    atn_hb_wipe(&b);
    atn_hb_wipe(&c);
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
