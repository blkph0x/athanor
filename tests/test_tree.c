/*
 * REQ-3.2 verification: insert/get/delete/scan, encrypted snapshot,
 * no plaintext of values in the snapshot blob.
 */
#include "atn_tree.h"
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

typedef struct {
    int n;
    uint8_t last;
    int order_ok;
} scan_ctx;

static int scan_fn(const uint8_t *key, size_t klen, const uint8_t *val, size_t vlen,
                   void *ctx)
{
    scan_ctx *s = (scan_ctx *)ctx;
    (void)val;
    (void)vlen;
    if (klen != 1) {
        s->order_ok = 0;
        return ATN_ERR_PARAM;
    }
    if (s->n > 0 && key[0] < s->last) {
        s->order_ok = 0;
    }
    s->last = key[0];
    s->n++;
    return ATN_OK;
}

static int memmem_absent(const uint8_t *hay, size_t hn, const uint8_t *nd, size_t nn)
{
    size_t i;
    if (nn == 0 || hn < nn) {
        return 1;
    }
    for (i = 0; i + nn <= hn; i++) {
        if (memcmp(hay + i, nd, nn) == 0) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    atn_tree t;
    uint8_t v[64], snap[8192], key[32];
    size_t n = 0, sn = 0;
    int i, rc;
    scan_ctx sc;
    static const uint8_t secret[] = "TREE-SECRET-VALUE-NOT-ON-DISK";

    printf("athanor tree  platform=%s\n", atn_platform_id());
    atn_tree_init(&t);
    check("put a", atn_tree_put(&t, (const uint8_t *)"a", 1, (const uint8_t *)"1", 1) == ATN_OK);
    check("put c", atn_tree_put(&t, (const uint8_t *)"c", 1, (const uint8_t *)"3", 1) == ATN_OK);
    check("put b", atn_tree_put(&t, (const uint8_t *)"b", 1, (const uint8_t *)"2", 1) == ATN_OK);
    check("count 3", t.count == 3);
    n = 0;
    check("get b", atn_tree_get(&t, (const uint8_t *)"b", 1, v, &n, sizeof(v)) == ATN_OK &&
          n == 1 && v[0] == '2');
    check("get missing", atn_tree_get(&t, (const uint8_t *)"z", 1, v, &n, sizeof(v)) == ATN_ERR_STATE);
    check("del b", atn_tree_del(&t, (const uint8_t *)"b", 1) == ATN_OK && t.count == 2);
    check("get b gone", atn_tree_get(&t, (const uint8_t *)"b", 1, v, &n, sizeof(v)) == ATN_ERR_STATE);

    check("put b again", atn_tree_put(&t, (const uint8_t *)"b", 1, (const uint8_t *)"2", 1) == ATN_OK);
    sc.n = 0;
    sc.last = 0;
    sc.order_ok = 1;
    check("scan", atn_tree_scan(&t, scan_fn, &sc) == ATN_OK && sc.n == 3 && sc.order_ok);

    for (i = 0; i < 64; i++) {
        uint8_t k[2], val[2];
        k[0] = (uint8_t)(200u - (unsigned)i);
        k[1] = (uint8_t)i;
        val[0] = (uint8_t)i;
        val[1] = 0x5a;
        rc = atn_tree_put(&t, k, 2, val, 2);
        if (rc != ATN_OK) {
            break;
        }
    }
    check("random 64 inserts", rc == ATN_OK && t.count == 3 + 64);

    check("secret put",
          atn_tree_put(&t, (const uint8_t *)"secret", 6, secret, sizeof(secret) - 1u) == ATN_OK);
    check("rng key", atn_random_bytes(key, 32) == ATN_OK);
    rc = atn_tree_snapshot(&t, key, snap, &sn, sizeof(snap));
    check("snapshot rc", rc == ATN_OK && sn > 28);
    check("snapshot hides value",
          memmem_absent(snap, sn, secret, sizeof(secret) - 1u));

    {
        atn_tree t2;
        uint8_t out[64];
        size_t on = 0;
        atn_tree_init(&t2);
        check("restore", atn_tree_restore(&t2, key, snap, sn) == ATN_OK);
        check("restore count", t2.count == t.count);
        check("restore secret",
              atn_tree_get(&t2, (const uint8_t *)"secret", 6, out, &on, sizeof(out)) == ATN_OK &&
              on == sizeof(secret) - 1u &&
              memcmp(out, secret, on) == 0);
        check("bad key fails",
              atn_tree_restore(&t2, key, snap, sn - 1u) == ATN_ERR_AUTH ||
              atn_tree_restore(&t2, key, snap, sn - 1u) == ATN_ERR_PARAM);
        atn_tree_wipe(&t2);
    }

    atn_tree_wipe(&t);
    check("wipe empty get",
          atn_tree_get(&t, (const uint8_t *)"a", 1, v, &n, sizeof(v)) == ATN_ERR_STATE);

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
