/*
 * Module: atn_sha256.c
 * REQ:    REQ-1.1
 * Spec:   RFC 6234 §§3, 4.1, 5.1, 6.1, 6.2 (SHA-256 only)
 *
 * Endian: message words and the digest are big-endian, per RFC 6234 §2.
 * Length L is in bits and occupies the last 64 bits of padding (§4.1.c).
 */

#include "atn_crypto.h"

#include <string.h>

/* RFC 6234 §3.d: ROTR^n(x) = (x>>n) OR (x<<(32-n)) on 32-bit words. */
static uint32_t rotr32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

/* RFC 6234 §5.1 */
static uint32_t ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ ((~x) & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t bsig0(uint32_t x)
{
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static uint32_t bsig1(uint32_t x)
{
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static uint32_t ssig0(uint32_t x)
{
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static uint32_t ssig1(uint32_t x)
{
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

/*
 * RFC 6234 §5.1: K0..K63 are the first 32 bits of the fractional parts of
 * the cube roots of the first sixty-four primes. Transcribed from that table.
 */
static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* RFC 6234 §6.2 steps 1–4 on one 512-bit block. */
static void sha256_compress(atn_sha256_ctx *ctx, const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned t;

    for (t = 0; t < 16; t++) {
        W[t] = load_be32(block + 4u * t);
    }
    for (t = 16; t < 64; t++) {
        W[t] = ssig1(W[t - 2]) + W[t - 7] + ssig0(W[t - 15]) + W[t - 16];
    }

    a = ctx->h[0];
    b = ctx->h[1];
    c = ctx->h[2];
    d = ctx->h[3];
    e = ctx->h[4];
    f = ctx->h[5];
    g = ctx->h[6];
    h = ctx->h[7];

    for (t = 0; t < 64; t++) {
        uint32_t T1 = h + bsig1(e) + ch(e, f, g) + K[t] + W[t];
        uint32_t T2 = bsig0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

void atn_sha256_init(atn_sha256_ctx *ctx)
{
    /* RFC 6234 §6.1 SHA-256 H(0): first 32 bits of frac(sqrt(first 8 primes)). */
    ctx->h[0] = 0x6a09e667u;
    ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u;
    ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu;
    ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu;
    ctx->h[7] = 0x5be0cd19u;
    ctx->nbits = 0;
    ctx->used = 0;
}

int atn_sha256_update(atn_sha256_ctx *ctx, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;

    if (ctx == NULL) {
        return ATN_ERR_PARAM;
    }
    if (n == 0) {
        return ATN_OK;
    }
    if (p == NULL) {
        return ATN_ERR_PARAM;
    }
    /* RFC 6234 §4.1: L < 2^64 bits. We count bits in nbits. */
    if (n > (((uint64_t)1 << 61) - 1u) - (ctx->nbits / 8u)) {
        return ATN_ERR_LEN;
    }

    ctx->nbits += (uint64_t)n * 8u;

    while (n > 0) {
        size_t take = 64u - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(ctx->block + ctx->used, p, take);
        ctx->used += take;
        p += take;
        n -= take;
        if (ctx->used == 64u) {
            sha256_compress(ctx, ctx->block);
            ctx->used = 0;
        }
    }
    return ATN_OK;
}

int atn_sha256_final(atn_sha256_ctx *ctx, uint8_t out[ATN_SHA256_LEN])
{
    uint64_t L;
    unsigned i;

    if (ctx == NULL || out == NULL) {
        return ATN_ERR_PARAM;
    }

    L = ctx->nbits;

    /* RFC 6234 §4.1.a: append 1 bit. We only hash whole bytes, so 0x80. */
    ctx->block[ctx->used++] = 0x80u;

    /* §4.1.b: zeros until length field fits: (L+1+K) mod 512 = 448 bits
     * i.e. 56 bytes used before the 8-byte length. */
    if (ctx->used > 56u) {
        while (ctx->used < 64u) {
            ctx->block[ctx->used++] = 0;
        }
        sha256_compress(ctx, ctx->block);
        ctx->used = 0;
    }
    while (ctx->used < 56u) {
        ctx->block[ctx->used++] = 0;
    }

    /* §4.1.c: 64-bit big-endian bit length. */
    for (i = 0; i < 8; i++) {
        ctx->block[56 + i] = (uint8_t)(L >> (56u - 8u * i));
    }
    sha256_compress(ctx, ctx->block);

    for (i = 0; i < 8; i++) {
        store_be32(out + 4u * i, ctx->h[i]);
    }

    atn_memzero(ctx, sizeof(*ctx));
    return ATN_OK;
}

int atn_sha256(const void *data, size_t n, uint8_t out[ATN_SHA256_LEN])
{
    atn_sha256_ctx ctx;
    int rc;

    atn_sha256_init(&ctx);
    rc = atn_sha256_update(&ctx, data, n);
    if (rc != ATN_OK) {
        atn_memzero(&ctx, sizeof(ctx));
        return rc;
    }
    return atn_sha256_final(&ctx, out);
}
