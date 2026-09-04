/*
 * REQ-3.1 verification: put on A is readable on B; wipe A still serves
 * from B; tampered tag rejected; shard map is deterministic.
 */
#include "atn_repl.h"
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

static int hs(atn_tun *a, atn_tun *b, uint8_t ek[ATN_MLKEM1024_EK_LEN],
              uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    if (atn_net_init() != ATN_OK) {
        return -1;
    }
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
    if (atn_tun_pump(b, 3000) != ATN_OK || b->state != ATN_TUN_ESTABLISHED) {
        return -1;
    }
    if (atn_tun_pump(a, 3000) != ATN_OK || a->state != ATN_TUN_ESTABLISHED) {
        return -1;
    }
    return 0;
}

int main(void)
{
    atn_tun ta, tb;
    atn_repl a, b;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t cluster[32];
    uint8_t ida[8], idb[8], roster[2][8];
    uint8_t out[64];
    size_t n = 0;
    unsigned s1, s2;
    int rc;
    static const uint8_t key[] = "block-key";
    static const uint8_t val[] = "block-val";
    static const uint8_t key2[] = "other-key";
    static const uint8_t val2[] = "other-val";

    printf("athanor repl  platform=%s\n", atn_platform_id());
    memset(ida, 0xaa, 8);
    memset(idb, 0xbb, 8);
    memcpy(roster[0], ida, 8);
    memcpy(roster[1], idb, 8);
    check("rng cluster", atn_random_bytes(cluster, 32) == ATN_OK);

    s1 = atn_repl_shard(key, sizeof(key) - 1u);
    s2 = atn_repl_shard(key, sizeof(key) - 1u);
    check("shard deterministic", s1 == s2 && s1 < ATN_REPL_SHARDS);
    check("shard range other",
          atn_repl_shard(key2, sizeof(key2) - 1u) < ATN_REPL_SHARDS);

    check("handshake", hs(&ta, &tb, ek, dk) == 0);
    check("init A", atn_repl_init(&a, ida, cluster, roster, 2, 0, &ta) == ATN_OK);
    check("init B", atn_repl_init(&b, idb, cluster, roster, 2, 1, &tb) == ATN_OK);
    check("both own factor-2",
          atn_repl_owner_p(&a, key, sizeof(key) - 1u, 0) &&
          atn_repl_owner_p(&a, key, sizeof(key) - 1u, 1));

    check("put A", atn_repl_put(&a, key, sizeof(key) - 1u, val, sizeof(val) - 1u) == ATN_OK);
    rc = atn_repl_pump(&b, 3000);
    check("pump B", rc == ATN_OK);
    n = 0;
    check("get B",
          atn_repl_get(&b, key, sizeof(key) - 1u, out, &n, sizeof(out)) == ATN_OK &&
          n == sizeof(val) - 1u && memcmp(out, val, n) == 0);
    n = 0;
    check("get A",
          atn_repl_get(&a, key, sizeof(key) - 1u, out, &n, sizeof(out)) == ATN_OK &&
          n == sizeof(val) - 1u && memcmp(out, val, n) == 0);

    /* Kill A: wipe its tree. B still serves. */
    atn_tree_wipe(&a.tree);
    n = 0;
    check("A dead get fails",
          atn_repl_get(&a, key, sizeof(key) - 1u, out, &n, sizeof(out)) == ATN_ERR_STATE);
    n = 0;
    check("B serves after A killed",
          atn_repl_get(&b, key, sizeof(key) - 1u, out, &n, sizeof(out)) == ATN_OK &&
          n == sizeof(val) - 1u && memcmp(out, val, n) == 0);

    /* Tamper: flip tag in a well-formed PUT and apply on B. */
    {
        uint8_t rec[512], msg[ATN_TUN_MAX_PT];
        size_t rn = 0;
        rc = atn_tree_get(&b.tree, key, sizeof(key) - 1u, rec, &rn, sizeof(rec));
        check("load rec", rc == ATN_OK && rn > 16);
        rec[rn - 2u] ^= 0xff; /* flip a tag byte (tag is last 17 including conflict) */
        msg[0] = ATN_REPL_PUT;
        msg[1] = (uint8_t)(sizeof(key) - 1u);
        memcpy(msg + 2, key, sizeof(key) - 1u);
        memcpy(msg + 2 + sizeof(key) - 1u, rec, rn);
        rc = atn_repl_apply(&b, msg, 2u + (sizeof(key) - 1u) + rn);
        check("tamper AUTH", rc == ATN_ERR_AUTH);
        n = 0;
        check("B value unchanged",
              atn_repl_get(&b, key, sizeof(key) - 1u, out, &n, sizeof(out)) == ATN_OK &&
              n == sizeof(val) - 1u && memcmp(out, val, n) == 0);
    }

    /* Catch-up: A stores key2 without sending (offline send), then B asks. */
    a.online = 0;
    check("put A key2 offline",
          atn_repl_put(&a, key2, sizeof(key2) - 1u, val2, sizeof(val2) - 1u) == ATN_OK);
    a.online = 1;
    check("catchup req", atn_repl_catchup_req(&b) == ATN_OK);
    rc = atn_repl_pump(&a, 3000);
    check("A handles catchup", rc == ATN_OK);
    rc = atn_repl_pump(&b, 3000);
    check("B recv catchup PUT", rc == ATN_OK);
    n = 0;
    check("B has key2",
          atn_repl_get(&b, key2, sizeof(key2) - 1u, out, &n, sizeof(out)) == ATN_OK &&
          n == sizeof(val2) - 1u && memcmp(out, val2, n) == 0);

    atn_repl_wipe(&a);
    atn_repl_wipe(&b);
    atn_tun_wipe(&ta);
    atn_tun_wipe(&tb);
    atn_net_fini();
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
