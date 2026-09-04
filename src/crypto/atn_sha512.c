/*
 * Module: atn_sha512.c
 * REQ:    REQ-1.1-PQ (DEC-0005) — 512-bit hash for Grover margin / CNSA 2.0
 * Spec:   RFC 6234 §§4.2, 5.2, 6.3, 6.4 (SHA-512)
 */

#include "atn_crypto.h"

#include <string.h>

static uint64_t rotr64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64u - n));
}

static uint64_t Ch(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ ((~x) & z);
}

static uint64_t Maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint64_t BSIG0(uint64_t x)
{
    return rotr64(x, 28) ^ rotr64(x, 34) ^ rotr64(x, 39);
}

static uint64_t BSIG1(uint64_t x)
{
    return rotr64(x, 14) ^ rotr64(x, 18) ^ rotr64(x, 41);
}

static uint64_t SSIG0(uint64_t x)
{
    return rotr64(x, 1) ^ rotr64(x, 8) ^ (x >> 7);
}

static uint64_t SSIG1(uint64_t x)
{
    return rotr64(x, 19) ^ rotr64(x, 61) ^ (x >> 6);
}

/* RFC 6234 §5.2 K0..K79 */
static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static uint64_t load_be64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

static void store_be64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)v;
}

static void compress(uint64_t h[8], const uint8_t block[128])
{
    uint64_t W[80], a, b, c, d, e, f, g, hh;
    unsigned t;
    for (t = 0; t < 16; t++) {
        W[t] = load_be64(block + 8u * t);
    }
    for (t = 16; t < 80; t++) {
        W[t] = SSIG1(W[t - 2]) + W[t - 7] + SSIG0(W[t - 15]) + W[t - 16];
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (t = 0; t < 80; t++) {
        uint64_t T1 = hh + BSIG1(e) + Ch(e, f, g) + K[t] + W[t];
        uint64_t T2 = BSIG0(a) + Maj(a, b, c);
        hh = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

typedef struct {
    uint64_t h[8];
    uint64_t nbits;
    uint8_t  block[128];
    size_t   used;
} sha512_ctx;

static void sha512_init(sha512_ctx *c)
{
    c->h[0] = 0x6a09e667f3bcc908ULL; c->h[1] = 0xbb67ae8584caa73bULL;
    c->h[2] = 0x3c6ef372fe94f82bULL; c->h[3] = 0xa54ff53a5f1d36f1ULL;
    c->h[4] = 0x510e527fade682d1ULL; c->h[5] = 0x9b05688c2b3e6c1fULL;
    c->h[6] = 0x1f83d9abfb41bd6bULL; c->h[7] = 0x5be0cd19137e2179ULL;
    c->nbits = 0;
    c->used = 0;
}

static int sha512_update(sha512_ctx *c, const uint8_t *p, size_t n)
{
    if (n == 0) {
        return ATN_OK;
    }
    if (p == NULL) {
        return ATN_ERR_PARAM;
    }
    c->nbits += (uint64_t)n * 8u;
    while (n > 0) {
        size_t take = 128u - c->used;
        if (take > n) {
            take = n;
        }
        memcpy(c->block + c->used, p, take);
        c->used += take;
        p += take;
        n -= take;
        if (c->used == 128u) {
            compress(c->h, c->block);
            c->used = 0;
        }
    }
    return ATN_OK;
}

static int sha512_final(sha512_ctx *c, uint8_t out[64])
{
    uint64_t L = c->nbits;
    unsigned i;
    c->block[c->used++] = 0x80u;
    if (c->used > 112u) {
        while (c->used < 128u) {
            c->block[c->used++] = 0;
        }
        compress(c->h, c->block);
        c->used = 0;
    }
    while (c->used < 112u) {
        c->block[c->used++] = 0;
    }
    store_be64(c->block + 112, 0);
    store_be64(c->block + 120, L);
    compress(c->h, c->block);
    for (i = 0; i < 8; i++) {
        store_be64(out + 8u * i, c->h[i]);
    }
    atn_memzero(c, sizeof(*c));
    return ATN_OK;
}

int atn_sha512(const void *data, size_t n, uint8_t out[ATN_SHA512_LEN])
{
    sha512_ctx c;
    int rc;
    if (out == NULL || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    sha512_init(&c);
    rc = sha512_update(&c, (const uint8_t *)data, n);
    if (rc != ATN_OK) {
        return rc;
    }
    return sha512_final(&c, out);
}

int atn_hmac_sha512(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[ATN_HMAC_SHA512_LEN])
{
    uint8_t kpad[ATN_SHA512_BLOCK];
    uint8_t inner[ATN_SHA512_LEN];
    sha512_ctx c;
    size_t i;
    int rc;

    if (out == NULL || (key == NULL && key_len != 0) || (msg == NULL && msg_len != 0)) {
        return ATN_ERR_PARAM;
    }
    memset(kpad, 0, sizeof(kpad));
    if (key_len > ATN_SHA512_BLOCK) {
        rc = atn_sha512(key, key_len, kpad);
        if (rc != ATN_OK) {
            return rc;
        }
    } else if (key_len > 0) {
        memcpy(kpad, key, key_len);
    }
    for (i = 0; i < ATN_SHA512_BLOCK; i++) {
        kpad[i] ^= 0x36u;
    }
    sha512_init(&c);
    rc = sha512_update(&c, kpad, ATN_SHA512_BLOCK);
    if (rc == ATN_OK) {
        rc = sha512_update(&c, msg, msg_len);
    }
    if (rc == ATN_OK) {
        rc = sha512_final(&c, inner);
    }
    if (rc != ATN_OK) {
        atn_memzero(kpad, sizeof(kpad));
        return rc;
    }
    for (i = 0; i < ATN_SHA512_BLOCK; i++) {
        kpad[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    }
    sha512_init(&c);
    rc = sha512_update(&c, kpad, ATN_SHA512_BLOCK);
    if (rc == ATN_OK) {
        rc = sha512_update(&c, inner, ATN_SHA512_LEN);
    }
    if (rc == ATN_OK) {
        rc = sha512_final(&c, out);
    }
    atn_memzero(kpad, sizeof(kpad));
    atn_memzero(inner, sizeof(inner));
    return rc;
}

int atn_hkdf_sha512(const uint8_t *salt, size_t salt_len,
                    const uint8_t *ikm, size_t ikm_len,
                    const uint8_t *info, size_t info_len,
                    uint8_t *okm, size_t okm_len)
{
    uint8_t zeros[ATN_SHA512_LEN], prk[ATN_SHA512_LEN], t[ATN_SHA512_LEN];
    uint8_t kpad[ATN_SHA512_BLOCK];
    size_t t_len = 0, filled = 0;
    uint8_t i;
    unsigned n;
    int rc;
    const uint8_t *s = salt;
    size_t sl = salt_len;

    if ((okm == NULL && okm_len != 0) || (ikm == NULL && ikm_len != 0)) {
        return ATN_ERR_PARAM;
    }
    if (okm_len > 255u * ATN_SHA512_LEN) {
        return ATN_ERR_LEN;
    }
    if (salt == NULL || salt_len == 0) {
        atn_memzero(zeros, sizeof(zeros));
        s = zeros;
        sl = ATN_SHA512_LEN;
    }
    rc = atn_hmac_sha512(s, sl, ikm, ikm_len, prk);
    if (rc != ATN_OK) {
        return rc;
    }
    if (okm_len == 0) {
        atn_memzero(prk, sizeof(prk));
        return ATN_OK;
    }
    n = (unsigned)((okm_len + ATN_SHA512_LEN - 1u) / ATN_SHA512_LEN);
    for (i = 1; i <= (uint8_t)n; i++) {
        sha512_ctx c;
        size_t k;
        memset(kpad, 0, sizeof(kpad));
        memcpy(kpad, prk, ATN_SHA512_LEN);
        for (k = 0; k < ATN_SHA512_BLOCK; k++) {
            kpad[k] ^= 0x36u;
        }
        sha512_init(&c);
        rc = sha512_update(&c, kpad, ATN_SHA512_BLOCK);
        if (rc == ATN_OK && t_len > 0) {
            rc = sha512_update(&c, t, t_len);
        }
        if (rc == ATN_OK && info_len > 0) {
            rc = sha512_update(&c, info, info_len);
        }
        if (rc == ATN_OK) {
            rc = sha512_update(&c, &i, 1);
        }
        if (rc == ATN_OK) {
            rc = sha512_final(&c, t);
        }
        if (rc != ATN_OK) {
            break;
        }
        for (k = 0; k < ATN_SHA512_BLOCK; k++) {
            kpad[k] ^= (uint8_t)(0x36u ^ 0x5cu);
        }
        sha512_init(&c);
        rc = sha512_update(&c, kpad, ATN_SHA512_BLOCK);
        if (rc == ATN_OK) {
            rc = sha512_update(&c, t, ATN_SHA512_LEN);
        }
        if (rc == ATN_OK) {
            rc = sha512_final(&c, t);
        }
        if (rc != ATN_OK) {
            break;
        }
        t_len = ATN_SHA512_LEN;
        {
            size_t take = okm_len - filled;
            if (take > ATN_SHA512_LEN) {
                take = ATN_SHA512_LEN;
            }
            memcpy(okm + filled, t, take);
            filled += take;
        }
    }
    atn_memzero(prk, sizeof(prk));
    atn_memzero(t, sizeof(t));
    atn_memzero(kpad, sizeof(kpad));
    return rc;
}
