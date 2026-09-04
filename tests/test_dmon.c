/*
 * REQ-4.4 native flush: after trigger, keys are zero and load is refused
 * until a new load. Runs on this builder (no phone). DEC-0017 hb/2FA paths.
 */
#include "atn_dmon.h"
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

int main(void)
{
    atn_dmon d;
    uint8_t dk[32], ck[32], z[32];
    uint8_t id[32], key[32], chal[32], resp[64], bad[64];
    uint8_t hid[8], pid[8], pkey[32], head[32];
    unsigned i;

    printf("athanor dmon  platform=%s\n", atn_platform_id());
    memset(z, 0, 32);
    check("rng", atn_random_bytes(dk, 32) == ATN_OK &&
          atn_random_bytes(ck, 32) == ATN_OK);
    atn_dmon_init(&d);
    check("require empty", atn_dmon_require(&d) == ATN_ERR_STATE);
    check("load", atn_dmon_load(&d, dk, ck) == ATN_OK);
    check("require loaded", atn_dmon_require(&d) == ATN_OK);
    memset(id, 0x44, 32);
    check("2fa enroll", atn_dmon_2fa_enroll(&d, id, key) == ATN_OK);
    check("2fa chal", atn_dmon_2fa_challenge(&d, id, chal) == ATN_OK);
    check("2fa resp", atn_2fa_respond(key, chal, resp) == ATN_OK);
    check("2fa verify", atn_dmon_2fa_verify(&d, id, chal, resp) == ATN_OK);

    atn_dmon_flush(&d);
    check("flushed not loaded", atn_dmon_require(&d) == ATN_ERR_STATE);
    check("device key zero", memcmp(d.device_key, z, 32) == 0);
    check("cluster zero", memcmp(d.cluster, z, 32) == 0);
    check("2fa gone",
          atn_dmon_2fa_challenge(&d, id, chal) == ATN_ERR_STATE);
    check("reload", atn_dmon_load(&d, dk, ck) == ATN_OK);
    check("require after reload", atn_dmon_require(&d) == ATN_OK);

    /* DEC-0017: no peers → silence does not flush. */
    memset(hid, 0xa1, 8);
    memset(head, 0x11, 32);
    check("hb init", atn_dmon_hb_init(&d, hid, 1, head) == ATN_OK);
    check("hb live", atn_dmon_hb_state(&d) == ATN_HB_LIVE);
    for (i = 1; i <= 10; i++) {
        if (atn_dmon_hb_tick(&d, i) != ATN_OK) {
            g_fail++;
            printf("FAIL lone tick %u\n", i);
            break;
        }
    }
    check("no-peer silence keeps keys", atn_dmon_require(&d) == ATN_OK);

    /* Peer enrolled, then silence → UNTRUSTED at N=3 → flush. */
    memset(pid, 0xb2, 8);
    check("peer rng", atn_random_bytes(pkey, 32) == ATN_OK);
    check("add peer", atn_dmon_hb_add_peer(&d, pid, pkey) == ATN_OK);
    check("tick 1 live", atn_dmon_hb_tick(&d, 11) == ATN_OK);
    check("tick 2 live", atn_dmon_hb_tick(&d, 12) == ATN_OK);
    check("tick 3 flush", atn_dmon_hb_tick(&d, 13) == ATN_ERR_STATE);
    check("untrusted wiped keys", atn_dmon_require(&d) == ATN_ERR_STATE);
    check("untrusted device zero", memcmp(d.device_key, z, 32) == 0);
    check("untrusted cluster zero", memcmp(d.cluster, z, 32) == 0);

    /* 2FA lockout flushes (ATN_2FA_FAIL_MAX=5). */
    check("reload 2fa", atn_dmon_load(&d, dk, ck) == ATN_OK);
    memset(id, 0x55, 32);
    check("enroll lock", atn_dmon_2fa_enroll(&d, id, key) == ATN_OK);
    check("chal lock", atn_dmon_2fa_challenge(&d, id, chal) == ATN_OK);
    memset(bad, 0, sizeof(bad));
    check("fail 1", atn_dmon_2fa_verify(&d, id, chal, bad) == ATN_ERR_AUTH);
    check("fail 2", atn_dmon_2fa_verify(&d, id, chal, bad) == ATN_ERR_AUTH);
    check("fail 3", atn_dmon_2fa_verify(&d, id, chal, bad) == ATN_ERR_AUTH);
    check("fail 4", atn_dmon_2fa_verify(&d, id, chal, bad) == ATN_ERR_AUTH);
    check("fail 5 lockout",
          atn_dmon_2fa_verify(&d, id, chal, bad) == ATN_ERR_LOCKOUT);
    check("lockout flushed", atn_dmon_require(&d) == ATN_ERR_STATE);
    check("lockout key zero", memcmp(d.device_key, z, 32) == 0);

    /* DEC-0020: two dmon ends speak DEC-0007 on loopback. */
    {
        atn_dmon da, db;
        uint8_t ek[ATN_MLKEM1024_EK_LEN], dkb[ATN_MLKEM1024_DK_LEN];
        uint8_t hello[5], back[64];
        size_t nrecv = 0;
        memcpy(hello, "mesh!", 5);
        atn_dmon_init(&da);
        atn_dmon_init(&db);
        check("tun net", atn_net_init() == ATN_OK);
        check("tun load",
              atn_dmon_load(&da, dk, ck) == ATN_OK &&
              atn_dmon_load(&db, dk, ck) == ATN_OK);
        check("tun kem", atn_mlkem1024_keygen(ek, dkb) == ATN_OK);
        check("tun init",
              atn_dmon_tun_initiator(&da, ek) == ATN_OK &&
              atn_dmon_tun_responder(&db, dkb) == ATN_OK);
        check("tun bind",
              atn_dmon_tun_bind(&da, 0) == ATN_OK &&
              atn_dmon_tun_bind(&db, 0) == ATN_OK &&
              atn_dmon_tun_port(&da) != 0 && atn_dmon_tun_port(&db) != 0);
        check("tun peer",
              atn_dmon_tun_set_peer(&da, 0x7f000001u, atn_dmon_tun_port(&db))
                  == ATN_OK &&
              atn_dmon_tun_set_peer(&db, 0x7f000001u, atn_dmon_tun_port(&da))
                  == ATN_OK);
        check("tun hs", atn_dmon_tun_hs_send(&da) == ATN_OK);
        check("tun B pump",
              atn_dmon_tun_pump(&db, 3000) == ATN_OK &&
              atn_dmon_tun_state(&db) == ATN_TUN_ESTABLISHED);
        check("tun A pump",
              atn_dmon_tun_pump(&da, 3000) == ATN_OK &&
              atn_dmon_tun_state(&da) == ATN_TUN_ESTABLISHED);
        check("tun send", atn_dmon_tun_send(&da, hello, 5) == ATN_OK);
        check("tun recv",
              atn_dmon_tun_recv(&db, back, &nrecv, sizeof(back), 3000) == ATN_OK
              && nrecv == 5 && memcmp(back, hello, 5) == 0);
        atn_dmon_flush(&da);
        check("tun flush A", atn_dmon_require(&da) == ATN_ERR_STATE);
        check("tun send after flush",
              atn_dmon_tun_send(&da, hello, 5) == ATN_ERR_STATE);
        atn_dmon_flush(&db);
        atn_net_fini();
    }

    atn_dmon_flush(&d);

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
