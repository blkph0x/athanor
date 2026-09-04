/*
 * Module: atn_sign.c
 * REQ:    REQ-5.1
 * Spec:   DEC-0019. SHA3-256 lines, ML-DSA-87 ctx atn-mf-v1.
 */

#include "atn_sign.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void atn_mf_init(atn_mf *m)
{
    if (m != NULL) {
        memset(m, 0, sizeof(*m));
    }
}

static int valid_path(const char *path)
{
    size_t i, n;
    if (path == NULL) {
        return 0;
    }
    n = strlen(path);
    if (n == 0 || n >= ATN_MF_MAX_PATH) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x21u || c > 0x7eu || c == ' ') {
            return 0;
        }
        if (c == '\\') {
            return 0;
        }
    }
    return 1;
}

int atn_mf_add(atn_mf *m, const char *path, const uint8_t *data, size_t n)
{
    if (m == NULL || !valid_path(path) || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    if (m->n >= ATN_MF_MAX_ENTRIES) {
        return ATN_ERR_LEN;
    }
    if (atn_sha3_256(data, n, m->e[m->n].hash) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    memcpy(m->e[m->n].path, path, strlen(path) + 1u);
    m->n++;
    return ATN_OK;
}

int atn_mf_add_file(atn_mf *m, const char *path)
{
    FILE *f;
    long sz;
    uint8_t *buf;
    size_t n;
    int rc;

    if (m == NULL || !valid_path(path)) {
        return ATN_ERR_PARAM;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_ERR_PARAM;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ATN_ERR_PARAM;
    }
    sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > ATN_MF_MAX_FILE) {
        fclose(f);
        return ATN_ERR_LEN;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ATN_ERR_PARAM;
    }
    n = (size_t)sz;
    buf = (uint8_t *)malloc(n == 0 ? 1u : n);
    if (buf == NULL) {
        fclose(f);
        return ATN_ERR_PARAM;
    }
    if (n > 0 && fread(buf, 1, n, f) != n) {
        free(buf);
        fclose(f);
        return ATN_ERR_PARAM;
    }
    fclose(f);
    rc = atn_mf_add(m, path, buf, n);
    atn_memzero(buf, n);
    free(buf);
    return rc;
}

static int cmp_ent(const void *a, const void *b)
{
    /* e[].path is the first field of each entry. */
    return strcmp((const char *)a, (const char *)b);
}

int atn_mf_encode(const atn_mf *m, uint8_t *out, size_t *n, size_t max)
{
    atn_mf tmp;
    size_t used = 0;
    unsigned i, j;
    static const char hexd[] = "0123456789abcdef";

    if (m == NULL || out == NULL || n == NULL || m->n == 0) {
        return ATN_ERR_PARAM;
    }
    tmp = *m;
    /* Path is the first field of each e[] — qsort as array of those structs. */
    qsort(tmp.e, tmp.n, sizeof(tmp.e[0]), cmp_ent);

    if (used + strlen(ATN_MF_HDR) > max) {
        return ATN_ERR_LEN;
    }
    memcpy(out + used, ATN_MF_HDR, strlen(ATN_MF_HDR));
    used += strlen(ATN_MF_HDR);

    for (i = 0; i < tmp.n; i++) {
        size_t plen = strlen(tmp.e[i].path);
        size_t need = 64u + 1u + plen + 1u;
        if (used + need > max) {
            return ATN_ERR_LEN;
        }
        for (j = 0; j < ATN_SHA3_256_LEN; j++) {
            unsigned b = tmp.e[i].hash[j];
            out[used++] = (uint8_t)hexd[b >> 4];
            out[used++] = (uint8_t)hexd[b & 0xfu];
        }
        out[used++] = ' ';
        memcpy(out + used, tmp.e[i].path, plen);
        used += plen;
        out[used++] = '\n';
    }
    *n = used;
    return ATN_OK;
}

int atn_mf_sign(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                const uint8_t *mf, size_t n,
                uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    if (sk == NULL || mf == NULL || n == 0 || sig == NULL) {
        return ATN_ERR_PARAM;
    }
    return atn_mldsa87_sign(sk, mf, n,
                            (const uint8_t *)ATN_MF_CTX, ATN_MF_CTX_LEN, sig);
}

int atn_mf_verify(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                  const uint8_t *mf, size_t n,
                  const uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    if (pk == NULL || mf == NULL || n == 0 || sig == NULL) {
        return ATN_ERR_PARAM;
    }
    return atn_mldsa87_verify(pk, mf, n,
                              (const uint8_t *)ATN_MF_CTX, ATN_MF_CTX_LEN, sig);
}
