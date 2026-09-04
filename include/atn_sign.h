/*
 * Signed source manifest (REQ-5.1). Spec: DEC-0019.
 *
 * Manifest is SHA3-256 lines, then ML-DSA-87 over those bytes with
 * ctx "atn-mf-v1". This is the pen. The air-gap factory is still REQ-5.1.
 */
#ifndef ATN_SIGN_H
#define ATN_SIGN_H

#include "atn_crypto.h"

#define ATN_MF_MAX_ENTRIES 256u
#define ATN_MF_MAX_PATH    200u
#define ATN_MF_MAX_FILE    (2u * 1024u * 1024u)
#define ATN_MF_HDR         "ATN-MANIFEST-1\n"
#define ATN_MF_CTX         "atn-mf-v1"
#define ATN_MF_CTX_LEN     8u

typedef struct {
    unsigned n;
    struct {
        char    path[ATN_MF_MAX_PATH];
        uint8_t hash[ATN_SHA3_256_LEN];
    } e[ATN_MF_MAX_ENTRIES];
} atn_mf;

void atn_mf_init(atn_mf *m);
int  atn_mf_add(atn_mf *m, const char *path, const uint8_t *data, size_t n);
int  atn_mf_add_file(atn_mf *m, const char *path);
int  atn_mf_encode(const atn_mf *m, uint8_t *out, size_t *n, size_t max);
int  atn_mf_sign(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                 const uint8_t *mf, size_t n,
                 uint8_t sig[ATN_MLDSA87_SIG_LEN]);
int  atn_mf_verify(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                   const uint8_t *mf, size_t n,
                   const uint8_t sig[ATN_MLDSA87_SIG_LEN]);

#endif
