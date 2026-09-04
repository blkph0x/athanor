/*
 * Athanor in-house cryptographic core (REQ-1.1).
 *
 * Purpose:  Public API for primitives compiled from our source only.
 * Spec:     DEC-0002 / DEC-0005; see docs/SPEC_INDEX.md for the RFC/FIPS list.
 * Policy:   No outside crypto libraries. Callers must atn_memzero secrets
 *           they no longer need. Tags are compared with atn_ct_equal only.
 *
 * Error codes are the only negative-path ABI. 0 is success.
 */
#ifndef ATN_CRYPTO_H
#define ATN_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "atn_platform.h"

#define ATN_SHA256_LEN         32u
#define ATN_SHA256_BLOCK       64u
#define ATN_HMAC_LEN           32u
#define ATN_CHACHA20_KEY_LEN   32u
#define ATN_CHACHA20_NONCE_LEN 12u
#define ATN_CHACHA20_BLOCK     64u
#define ATN_POLY1305_KEY_LEN   32u
#define ATN_POLY1305_TAG_LEN   16u
#define ATN_AEAD_KEY_LEN       32u
#define ATN_AEAD_NONCE_LEN     12u
#define ATN_AEAD_TAG_LEN       16u

/* FIPS 202 */
#define ATN_SHA3_256_LEN       32u
#define ATN_SHA3_512_LEN       64u
#define ATN_SHA512_LEN         64u
#define ATN_SHA512_BLOCK       128u
#define ATN_HMAC_SHA512_LEN    64u

/* FIPS 203 ML-KEM-1024 (NIST category 5) */
#define ATN_MLKEM1024_EK_LEN   1568u
#define ATN_MLKEM1024_DK_LEN   3168u
#define ATN_MLKEM1024_CT_LEN   1568u
#define ATN_MLKEM1024_SS_LEN   32u
#define ATN_MLKEM1024_SEED_LEN 32u

/* FIPS 204 ML-DSA-87 (NIST category 5) */
#define ATN_MLDSA87_PK_LEN     2592u
#define ATN_MLDSA87_SK_LEN     4896u
#define ATN_MLDSA87_SIG_LEN    4627u
#define ATN_MLDSA87_SEED_LEN   32u
#define ATN_MLDSA87_RND_LEN    32u
#define ATN_MLDSA87_MSG_MAX    65536u /* DEC-0019 source manifest fits */

enum {
    ATN_OK          = 0,
    ATN_ERR_PARAM   = 1, /* NULL where forbidden, or size 0 where required */
    ATN_ERR_AUTH    = 2, /* AEAD tag mismatch — treat as unauthenticated */
    ATN_ERR_NONCE   = 3, /* reused or non-monotonic nonce for this key/sender */
    ATN_ERR_ENTROPY = 4, /* OS CSPRNG failed */
    ATN_ERR_LEN     = 5, /* output length exceeds construction limit */
    ATN_ERR_SOCK    = 6, /* OS socket failure */
    ATN_ERR_STATE   = 7, /* protocol/state machine refused the call */
    ATN_ERR_LOCKOUT  = 8, /* 2FA slot locked after ATN_2FA_FAIL_MAX */
    ATN_ERR_CONFLICT = 9  /* concurrent version vectors; both kept */
};

/* ---- secure helpers (RFC 8439 §4; OS RNG) -------------------------------- */

/*
 * Purpose:  Wipe a buffer; attempts to survive compiler dead-store elimination.
 * Spec:     REQ-1.1 zeroization gate; no RFC number, DEC-0002.
 * Params:   p may be NULL iff n==0.
 * Returns:  void. Does not claim to defeat DMA or cold-boot.
 */
void atn_memzero(void *p, size_t n);

/*
 * Purpose:  Constant-time equality of two n-byte buffers.
 * Spec:     RFC 8439 §4 (tag compare MUST NOT short-circuit).
 * Returns:  1 if equal, 0 if not. n==0 is equal. NULL illegal unless n==0.
 */
int atn_ct_equal(const void *a, const void *b, size_t n);

/*
 * Purpose:  Fill buf with OS entropy. Not a userspace PRNG.
 * Spec:     DEC-0002 / DEC-0004; OS CSPRNG (BCrypt / arc4random / getrandom / urandom).
 * Returns:  ATN_OK or ATN_ERR_ENTROPY / ATN_ERR_PARAM.
 */
int atn_random_bytes(void *buf, size_t n);

/* atn_platform_id lives in atn_platform.h (included above). */

/* ---- SHA-256 (RFC 6234 §§4.1, 5.1, 6.1, 6.2) ----------------------------- */

typedef struct {
    uint32_t h[8];
    uint64_t nbits;          /* message length in bits, L in RFC 6234 §4.1 */
    uint8_t  block[64];
    size_t   used;           /* bytes pending in block[] */
} atn_sha256_ctx;

void atn_sha256_init(atn_sha256_ctx *ctx);
int  atn_sha256_update(atn_sha256_ctx *ctx, const void *data, size_t n);
int  atn_sha256_final(atn_sha256_ctx *ctx, uint8_t out[ATN_SHA256_LEN]);
int  atn_sha256(const void *data, size_t n, uint8_t out[ATN_SHA256_LEN]);

/* ---- HMAC-SHA-256 (RFC 2104) --------------------------------------------- */

int atn_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[ATN_HMAC_LEN]);

/* ---- HKDF-SHA-256 (RFC 5869 §§2.2–2.3) ----------------------------------- */

int atn_hkdf_extract(const uint8_t *salt, size_t salt_len,
                     const uint8_t *ikm, size_t ikm_len,
                     uint8_t prk[ATN_SHA256_LEN]);

int atn_hkdf_expand(const uint8_t *prk, size_t prk_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *okm, size_t okm_len);

int atn_hkdf(const uint8_t *salt, size_t salt_len,
             const uint8_t *ikm, size_t ikm_len,
             const uint8_t *info, size_t info_len,
             uint8_t *okm, size_t okm_len);

/* ---- ChaCha20 (RFC 8439 §§2.1–2.4) --------------------------------------- */

/*
 * Purpose:  One 64-byte ChaCha20 block.
 * Spec:     RFC 8439 §2.3. counter is the 32-bit block counter (word 12).
 */
void atn_chacha20_block(const uint8_t key[ATN_CHACHA20_KEY_LEN],
                        uint32_t counter,
                        const uint8_t nonce[ATN_CHACHA20_NONCE_LEN],
                        uint8_t out[ATN_CHACHA20_BLOCK]);

int atn_chacha20_xor(const uint8_t key[ATN_CHACHA20_KEY_LEN],
                     uint32_t counter,
                     const uint8_t nonce[ATN_CHACHA20_NONCE_LEN],
                     const uint8_t *in, uint8_t *out, size_t n);

/* ---- Poly1305 (RFC 8439 §2.5) -------------------------------------------- */

typedef struct {
    uint32_t r[5];           /* clamped r, 26-bit limbs */
    uint32_t s[4];           /* s as four 32-bit LE words */
    uint32_t h[5];           /* accumulator, 26-bit limbs */
    uint8_t  buf[16];
    size_t   used;
    int      finalized;
} atn_poly1305_ctx;

int  atn_poly1305_init(atn_poly1305_ctx *ctx, const uint8_t key[ATN_POLY1305_KEY_LEN]);
int  atn_poly1305_update(atn_poly1305_ctx *ctx, const uint8_t *msg, size_t n);
int  atn_poly1305_final(atn_poly1305_ctx *ctx, uint8_t tag[ATN_POLY1305_TAG_LEN]);
int  atn_poly1305(const uint8_t key[ATN_POLY1305_KEY_LEN],
                  const uint8_t *msg, size_t n,
                  uint8_t tag[ATN_POLY1305_TAG_LEN]);

/* ---- AEAD_CHACHA20_POLY1305 (RFC 8439 §§2.6, 2.8) ------------------------ */

int atn_aead_encrypt(const uint8_t key[ATN_AEAD_KEY_LEN],
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *pt, size_t pt_len,
                     uint8_t *ct,
                     uint8_t tag[ATN_AEAD_TAG_LEN]);

int atn_aead_decrypt(const uint8_t key[ATN_AEAD_KEY_LEN],
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *ct, size_t ct_len,
                     const uint8_t tag[ATN_AEAD_TAG_LEN],
                     uint8_t *pt);

/* ---- nonce sequencer (RFC 8439 §2.3) ------------------------------------- */

typedef struct {
    uint32_t sender;
    uint64_t next_counter;   /* next counter we will emit (encrypt side) */
    uint64_t last_seen;      /* last counter we accepted (decrypt side) */
    int      have_sender;
    int      have_seen;
} atn_nonce_state;

/*
 * Wire layout of the 12-byte nonce (little-endian fields):
 *   bytes 0-3  sender id   -> ChaCha20 state word 13
 *   bytes 4-11 64-bit counter -> words 14 and 15
 * A (key, nonce) pair MUST NOT repeat. Counter is strictly increasing
 * per sender. Overflow of the 64-bit counter is ATN_ERR_NONCE.
 */
int atn_nonce_format(uint8_t nonce[ATN_AEAD_NONCE_LEN],
                     uint32_t sender, uint64_t counter);

int atn_nonce_parse(const uint8_t nonce[ATN_AEAD_NONCE_LEN],
                    uint32_t *sender, uint64_t *counter);

int atn_nonce_next(atn_nonce_state *st, uint32_t sender,
                   uint8_t nonce[ATN_AEAD_NONCE_LEN]);

int atn_nonce_accept(atn_nonce_state *st,
                     const uint8_t nonce[ATN_AEAD_NONCE_LEN]);

/* ---- SHA-512 (RFC 6234 / FIPS 180-4) — Grover-margin hash -------------- */

int atn_sha512(const void *data, size_t n, uint8_t out[ATN_SHA512_LEN]);

int atn_hmac_sha512(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[ATN_HMAC_SHA512_LEN]);

int atn_hkdf_sha512(const uint8_t *salt, size_t salt_len,
                    const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *okm, size_t okm_len);

/* ---- FIPS 202 SHA-3 / SHAKE -------------------------------------------- */

int atn_sha3_256(const void *data, size_t n, uint8_t out[ATN_SHA3_256_LEN]);
int atn_sha3_512(const void *data, size_t n, uint8_t out[ATN_SHA3_512_LEN]);
int atn_shake128(const void *data, size_t n, uint8_t *out, size_t outlen);
int atn_shake256(const void *data, size_t n, uint8_t *out, size_t outlen);

typedef struct {
    uint64_t s[25];
    uint8_t  buf[168];
    size_t   used;
    int      squeezing;
} atn_shake128_ctx;

void atn_shake128_init(atn_shake128_ctx *ctx);
void atn_shake128_absorb(atn_shake128_ctx *ctx, const uint8_t *in, size_t n);
void atn_shake128_finalize(atn_shake128_ctx *ctx);
void atn_shake128_squeeze(atn_shake128_ctx *ctx, uint8_t *out, size_t n);

typedef struct {
    uint64_t s[25];
    uint8_t  buf[136];
    size_t   used;
    int      squeezing;
} atn_shake256_ctx;

void atn_shake256_init(atn_shake256_ctx *ctx);
void atn_shake256_absorb(atn_shake256_ctx *ctx, const uint8_t *in, size_t n);
void atn_shake256_finalize(atn_shake256_ctx *ctx);
void atn_shake256_squeeze(atn_shake256_ctx *ctx, uint8_t *out, size_t n);

/* ---- FIPS 203 ML-KEM-1024 ---------------------------------------------- */

/*
 * Internal (deterministic) APIs exist so KATs can inject seeds. Production
 * callers use the random APIs. FIPS 203 §3.3: do not expose internals to
 * applications other than testing.
 */
int atn_mlkem1024_keygen_internal(const uint8_t d[32], const uint8_t z[32],
                                  uint8_t ek[ATN_MLKEM1024_EK_LEN],
                                  uint8_t dk[ATN_MLKEM1024_DK_LEN]);

int atn_mlkem1024_encaps_internal(const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                                  const uint8_t m[32],
                                  uint8_t ss[ATN_MLKEM1024_SS_LEN],
                                  uint8_t ct[ATN_MLKEM1024_CT_LEN]);

int atn_mlkem1024_decaps(const uint8_t dk[ATN_MLKEM1024_DK_LEN],
                         const uint8_t ct[ATN_MLKEM1024_CT_LEN],
                         uint8_t ss[ATN_MLKEM1024_SS_LEN]);

int atn_mlkem1024_keygen(uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         uint8_t dk[ATN_MLKEM1024_DK_LEN]);

int atn_mlkem1024_encaps(const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         uint8_t ss[ATN_MLKEM1024_SS_LEN],
                         uint8_t ct[ATN_MLKEM1024_CT_LEN]);

/* ---- FIPS 204 ML-DSA-87 ---------------------------------------------- */

int atn_mldsa87_keygen_internal(const uint8_t xi[ATN_MLDSA87_SEED_LEN],
                                uint8_t pk[ATN_MLDSA87_PK_LEN],
                                uint8_t sk[ATN_MLDSA87_SK_LEN]);

int atn_mldsa87_sign_internal(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                              const uint8_t *mp, size_t mp_len,
                              const uint8_t rnd[ATN_MLDSA87_RND_LEN],
                              uint8_t sig[ATN_MLDSA87_SIG_LEN]);

int atn_mldsa87_verify_internal(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                                const uint8_t *mp, size_t mp_len,
                                const uint8_t sig[ATN_MLDSA87_SIG_LEN]);

int atn_mldsa87_keygen(uint8_t pk[ATN_MLDSA87_PK_LEN],
                       uint8_t sk[ATN_MLDSA87_SK_LEN]);

int atn_mldsa87_sign(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                     const uint8_t *msg, size_t n,
                     const uint8_t *ctx, size_t ctx_len,
                     uint8_t sig[ATN_MLDSA87_SIG_LEN]);

int atn_mldsa87_verify(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                       const uint8_t *msg, size_t n,
                       const uint8_t *ctx, size_t ctx_len,
                       const uint8_t sig[ATN_MLDSA87_SIG_LEN]);

#endif /* ATN_CRYPTO_H */
