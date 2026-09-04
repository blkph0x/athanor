/*
 * ML-DSA-87 KATs from usnistgov/ACVP-Server ML-DSA-*-FIPS204.
 * A FAIL line is a SoT gate failure. Do not edit a vector to match us.
 */
#include "atn_crypto.h"
#include "kat_mldsa87.h"
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
    uint8_t pk[ATN_MLDSA87_PK_LEN], sk[ATN_MLDSA87_SK_LEN];
    uint8_t sig[ATN_MLDSA87_SIG_LEN], rnd[ATN_MLDSA87_RND_LEN];
    uint8_t msg[32];
    static const uint8_t ctx[] = { 'a', 't', 'n' };

    printf("athanor mldsa-87  platform=%s\n", atn_platform_id());

    check("keygen_internal",
          atn_mldsa87_keygen_internal(kat_mldsa87_seed, pk, sk) == ATN_OK);
    check("pk KAT", memcmp(pk, kat_mldsa87_pk, ATN_MLDSA87_PK_LEN) == 0);
    check("sk KAT", memcmp(sk, kat_mldsa87_sk, ATN_MLDSA87_SK_LEN) == 0);

    memset(rnd, 0, sizeof(rnd));
    check("sign_internal",
          atn_mldsa87_sign_internal(kat_mldsa87_sign_sk,
                                    kat_mldsa87_sign_mp,
                                    sizeof(kat_mldsa87_sign_mp),
                                    rnd, sig) == ATN_OK);
    check("sig KAT", memcmp(sig, kat_mldsa87_sign_sig, ATN_MLDSA87_SIG_LEN) == 0);
    check("verify_internal KAT",
          atn_mldsa87_verify_internal(kat_mldsa87_sign_pk,
                                      kat_mldsa87_sign_mp,
                                      sizeof(kat_mldsa87_sign_mp),
                                      kat_mldsa87_sign_sig) == ATN_OK);
    sig[0] ^= 1;
    check("verify tamper",
          atn_mldsa87_verify_internal(kat_mldsa87_sign_pk,
                                      kat_mldsa87_sign_mp,
                                      sizeof(kat_mldsa87_sign_mp),
                                      sig) == ATN_ERR_AUTH);

    check("random keygen", atn_mldsa87_keygen(pk, sk) == ATN_OK);
    memset(msg, 0x61, sizeof(msg));
    check("hedged sign",
          atn_mldsa87_sign(sk, msg, sizeof(msg), ctx, sizeof(ctx), sig) == ATN_OK);
    check("hedged verify",
          atn_mldsa87_verify(pk, msg, sizeof(msg), ctx, sizeof(ctx), sig) == ATN_OK);
    msg[0] ^= 1;
    check("hedged wrong msg",
          atn_mldsa87_verify(pk, msg, sizeof(msg), ctx, sizeof(ctx), sig)
          == ATN_ERR_AUTH);

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
