/*
 * REQ-1.3 verification: enroll, challenge, verify, wrong key, replay, lockout.
 */
#include "atn_2fa.h"

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
    atn_2fa_store st;
    uint8_t id[32], key[32], chal[32], resp[64], wrong[32];
    int i, rc;

    printf("athanor 2fa  platform=%s\n", atn_platform_id());
    atn_2fa_store_init(&st);
    memset(id, 0x42, sizeof(id));
    check("enroll", atn_2fa_enroll(&st, id, key) == ATN_OK);
    check("challenge", atn_2fa_challenge(&st, id, chal) == ATN_OK);
    check("respond", atn_2fa_respond(key, chal, resp) == ATN_OK);
    check("verify", atn_2fa_verify(&st, id, chal, resp) == ATN_OK);

    /* Replay of the used pair: pending is cleared. */
    rc = atn_2fa_verify(&st, id, chal, resp);
    check("replay used", rc == ATN_ERR_AUTH || rc == ATN_ERR_NONCE);

    check("challenge2", atn_2fa_challenge(&st, id, chal) == ATN_OK);
    memset(wrong, 0x99, sizeof(wrong));
    check("wrong key respond", atn_2fa_respond(wrong, chal, resp) == ATN_OK);
    check("wrong key verify", atn_2fa_verify(&st, id, chal, resp) == ATN_ERR_AUTH);

    /* Lockout: keep failing. pending still set after wrong verify. */
    for (i = 0; i < 10; i++) {
        rc = atn_2fa_verify(&st, id, chal, resp);
        if (rc == ATN_ERR_LOCKOUT) {
            break;
        }
    }
    check("lockout", rc == ATN_ERR_LOCKOUT);
    check("challenge locked", atn_2fa_challenge(&st, id, chal) == ATN_ERR_LOCKOUT);
    check("revoke", atn_2fa_revoke(&st, id) == ATN_OK);

    atn_memzero(key, sizeof(key));
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
