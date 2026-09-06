/*
 * REQ-5.1 / DEC-0019: manifest sort + ML-DSA-87 sign/verify/tamper.
 */
#include "atn_sign.h"
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
    atn_mf m;
    uint8_t pk[ATN_MLDSA87_PK_LEN], sk[ATN_MLDSA87_SK_LEN];
    uint8_t sig[ATN_MLDSA87_SIG_LEN], body[1024];
    size_t n = 0;
    static const uint8_t a[] = { 0x61 };
    static const uint8_t b[] = { 0x62 };
    const char *p;

    printf("athanor sign  platform=%s\n", atn_platform_id());
    atn_mf_init(&m);
    check("add b", atn_mf_add(&m, "z/b.txt", b, 1) == ATN_OK);
    check("add a", atn_mf_add(&m, "a.txt", a, 1) == ATN_OK);
    check("bad path", atn_mf_add(&m, "has space", a, 1) == ATN_ERR_PARAM);
    check("encode", atn_mf_encode(&m, body, &n, sizeof(body) - 1u) == ATN_OK);
    check("header", n >= 15 && memcmp(body, "ATN-MANIFEST-1\n", 15) == 0);
    body[n] = 0;
    p = (const char *)body;
    check("sorted a before z",
          strstr(p, "a.txt") != NULL && strstr(p, "z/b.txt") != NULL &&
          (strstr(p, "a.txt") < strstr(p, "z/b.txt")));

    atn_mf_init(&m);
    check("add_file Makefile", atn_mf_add_file(&m, "Makefile") == ATN_OK);
    check("encode file", atn_mf_encode(&m, body, &n, sizeof(body) - 1u) == ATN_OK);

    check("keygen", atn_mldsa87_keygen(pk, sk) == ATN_OK);
    check("sign", atn_mf_sign(sk, body, n, sig) == ATN_OK);
    check("verify", atn_mf_verify(pk, body, n, sig) == ATN_OK);
    body[20] ^= 1;
    check("tamper body", atn_mf_verify(pk, body, n, sig) == ATN_ERR_AUTH);
    body[20] ^= 1;
    sig[0] ^= 1;
    check("tamper sig", atn_mf_verify(pk, body, n, sig) == ATN_ERR_AUTH);

    check("report encode",
          atn_report_encode(1, atn_platform_id(), body, &n, sizeof(body) - 1u)
          == ATN_OK);
    body[n] = 0;
    check("report hdr", memcmp(body, "ATN-REPORT-1\n", 13) == 0);
    check("report pass", strstr((const char *)body, "status=PASS") != NULL);
    check("report diag0", strstr((const char *)body, "diag=0\n") != NULL);
    check("report sign", atn_report_sign(sk, body, n, sig) == ATN_OK);
    check("report verify", atn_report_verify(pk, body, n, sig) == ATN_OK);
    body[14] ^= 1;
    check("report tamper", atn_report_verify(pk, body, n, sig) == ATN_ERR_AUTH);
    check("report diag1",
          atn_report_encode_ex(1, atn_platform_id(), 1, body, &n, sizeof(body))
          == ATN_OK);
    body[n] = 0;
    check("report diag1 line", strstr((const char *)body, "diag=1\n") != NULL);
    atn_memzero(sk, sizeof(sk));

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
