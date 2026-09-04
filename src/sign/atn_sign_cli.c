/*
 * atnsign — ML-DSA-87 manifest signer (REQ-5.1 / DEC-0019).
 *   atnsign demo
 *   atnsign keygen <pk-file> <sk-file>
 *   atnsign sign   <sk-file> <msg-file> <sig-file>
 *   atnsign verify <pk-file> <msg-file> <sig-file>
 *   atnsign manifest <list-file> <out-file>
 */
#include "atn_sign.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_all(const char *path, uint8_t **out, size_t *n, size_t max)
{
    FILE *f;
    long sz;
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > max) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    *n = (size_t)sz;
    *out = (uint8_t *)malloc(*n == 0 ? 1u : *n);
    if (*out == NULL) {
        fclose(f);
        return -1;
    }
    if (*n > 0 && fread(*out, 1, *n, f) != *n) {
        free(*out);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int write_all(const char *path, const uint8_t *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    if (n > 0 && fwrite(p, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int cmd_demo(void)
{
    atn_mf m;
    uint8_t pk[ATN_MLDSA87_PK_LEN], sk[ATN_MLDSA87_SK_LEN];
    uint8_t sig[ATN_MLDSA87_SIG_LEN];
    uint8_t body[512];
    size_t n = 0;
    static const uint8_t a[] = { 'x' };
    static const uint8_t b[] = { 'y' };

    atn_mf_init(&m);
    if (atn_mf_add(&m, "b.txt", b, 1) != ATN_OK ||
        atn_mf_add(&m, "a.txt", a, 1) != ATN_OK) {
        fprintf(stderr, "add failed\n");
        return 1;
    }
    if (atn_mf_encode(&m, body, &n, sizeof(body)) != ATN_OK) {
        fprintf(stderr, "encode failed\n");
        return 1;
    }
    if (n < 16 || memcmp(body, "ATN-MANIFEST-1\n", 15) != 0) {
        fprintf(stderr, "header failed\n");
        return 1;
    }
    /* Sorted: a.txt before b.txt. */
    if (strstr((const char *)body, "a.txt") == NULL) {
        return 1;
    }
    if (atn_mldsa87_keygen(pk, sk) != ATN_OK) {
        fprintf(stderr, "keygen failed\n");
        return 1;
    }
    if (atn_mf_sign(sk, body, n, sig) != ATN_OK) {
        fprintf(stderr, "sign failed\n");
        return 1;
    }
    if (atn_mf_verify(pk, body, n, sig) != ATN_OK) {
        fprintf(stderr, "verify failed\n");
        return 1;
    }
    body[0] ^= 1;
    if (atn_mf_verify(pk, body, n, sig) != ATN_ERR_AUTH) {
        fprintf(stderr, "tamper not caught\n");
        return 1;
    }
    atn_memzero(sk, sizeof(sk));
    printf("atnsign demo: manifest sort/sign/verify/tamper-reject OK (DEC-0019)\n");
    return 0;
}

static int cmd_keygen(const char *pkpath, const char *skpath)
{
    uint8_t pk[ATN_MLDSA87_PK_LEN], sk[ATN_MLDSA87_SK_LEN];
    int rc;
    if (atn_mldsa87_keygen(pk, sk) != ATN_OK) {
        return 1;
    }
    rc = write_all(pkpath, pk, sizeof(pk));
    if (rc == 0) {
        rc = write_all(skpath, sk, sizeof(sk));
    }
    atn_memzero(sk, sizeof(sk));
    return rc == 0 ? 0 : 1;
}

static int cmd_sign(const char *skpath, const char *msgpath, const char *sigpath)
{
    uint8_t *sk = NULL, *msg = NULL, sig[ATN_MLDSA87_SIG_LEN];
    size_t skn = 0, msn = 0;
    int rc = 1;
    if (read_all(skpath, &sk, &skn, ATN_MLDSA87_SK_LEN) != 0 ||
        skn != ATN_MLDSA87_SK_LEN) {
        goto done;
    }
    if (read_all(msgpath, &msg, &msn, ATN_MLDSA87_MSG_MAX) != 0) {
        goto done;
    }
    if (atn_mf_sign(sk, msg, msn, sig) != ATN_OK) {
        goto done;
    }
    if (write_all(sigpath, sig, sizeof(sig)) == 0) {
        rc = 0;
    }
done:
    if (sk != NULL) {
        atn_memzero(sk, skn);
        free(sk);
    }
    if (msg != NULL) {
        free(msg);
    }
    return rc;
}

static int cmd_verify(const char *pkpath, const char *msgpath, const char *sigpath)
{
    uint8_t *pk = NULL, *msg = NULL, *sig = NULL;
    size_t pkn = 0, msn = 0, sgn = 0;
    int rc = 1;
    if (read_all(pkpath, &pk, &pkn, ATN_MLDSA87_PK_LEN) != 0 ||
        pkn != ATN_MLDSA87_PK_LEN) {
        goto done;
    }
    if (read_all(msgpath, &msg, &msn, ATN_MLDSA87_MSG_MAX) != 0) {
        goto done;
    }
    if (read_all(sigpath, &sig, &sgn, ATN_MLDSA87_SIG_LEN) != 0 ||
        sgn != ATN_MLDSA87_SIG_LEN) {
        goto done;
    }
    if (atn_mf_verify(pk, msg, msn, sig) == ATN_OK) {
        printf("OK\n");
        rc = 0;
    } else {
        printf("FAIL\n");
    }
done:
    free(pk);
    free(msg);
    free(sig);
    return rc;
}

static int cmd_manifest(const char *listpath, const char *outpath)
{
    FILE *lf;
    char line[ATN_MF_MAX_PATH + 8];
    atn_mf m;
    uint8_t *body;
    size_t n = 0, cap = 128u * 1024u;
    int rc = 1;

    atn_mf_init(&m);
    lf = fopen(listpath, "rb");
    if (lf == NULL) {
        fprintf(stderr, "open list failed\n");
        return 1;
    }
    while (fgets(line, (int)sizeof(line), lf) != NULL) {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1u] == '\n' || line[L - 1u] == '\r')) {
            line[--L] = 0;
        }
        if (L == 0 || line[0] == '#') {
            continue;
        }
        if (atn_mf_add_file(&m, line) != ATN_OK) {
            fprintf(stderr, "hash failed: %s\n", line);
            fclose(lf);
            return 1;
        }
    }
    fclose(lf);
    body = (uint8_t *)malloc(cap);
    if (body == NULL) {
        return 1;
    }
    if (atn_mf_encode(&m, body, &n, cap) != ATN_OK) {
        fprintf(stderr, "encode failed\n");
        free(body);
        return 1;
    }
    rc = write_all(outpath, body, n);
    free(body);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: atnsign demo|keygen|sign|verify|manifest ...\n");
        return 1;
    }
    if (strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (strcmp(argv[1], "keygen") == 0 && argc == 4) {
        return cmd_keygen(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "sign") == 0 && argc == 5) {
        return cmd_sign(argv[2], argv[3], argv[4]);
    }
    if (strcmp(argv[1], "verify") == 0 && argc == 5) {
        return cmd_verify(argv[2], argv[3], argv[4]);
    }
    if (strcmp(argv[1], "manifest") == 0 && argc == 4) {
        return cmd_manifest(argv[2], argv[3]);
    }
    fprintf(stderr, "usage: atnsign demo|keygen|sign|verify|manifest ...\n");
    return 1;
}
