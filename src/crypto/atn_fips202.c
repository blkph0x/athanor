/*
 * Module: atn_fips202.c
 * REQ:    REQ-1.1-PQ (DEC-0005)
 * Spec:   FIPS 202 — SHA-3 and SHAKE (Keccak-p[1600,24])
 *
 * Domain suffixes (FIPS 202 §6.1–6.2):
 *   SHA3-*:  0x06
 *   SHAKE*:  0x1F
 * Rate (bytes): SHA3-256=136, SHA3-512=72, SHAKE128=168, SHAKE256=136.
 */

#include "atn_crypto.h"

#include <string.h>

/* FIPS 202 Table 2 — Keccak-p[1600] round constants, transcribed. */
static const uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static uint64_t rotl64(uint64_t x, unsigned n)
{
    return (x << n) | (x >> (64u - n));
}

/* FIPS 202 §3.2 Keccak-f[1600] = Keccak-p[1600,24] */
static void keccak_f1600(uint64_t s[25])
{
    unsigned round;
    for (round = 0; round < 24; round++) {
        uint64_t C[5], D[5], B[25];
        unsigned x, y;

        /* theta */
        for (x = 0; x < 5; x++) {
            C[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];
        }
        for (x = 0; x < 5; x++) {
            D[x] = C[(x + 4) % 5] ^ rotl64(C[(x + 1) % 5], 1);
        }
        for (x = 0; x < 5; x++) {
            for (y = 0; y < 5; y++) {
                s[x + 5 * y] ^= D[x];
            }
        }

        /* rho + pi  (FIPS 202 §3.2.2–3.2.3) */
        {
            uint64_t t = s[1];
            unsigned i;
            /* (x,y) walk: (1,0) then (y, (2x+3y) mod 5) */
            static const unsigned rholane[24] = {
                1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
            };
            unsigned x0 = 1, y0 = 0;
            for (i = 0; i < 24; i++) {
                unsigned x1 = y0;
                unsigned y1 = (2u * x0 + 3u * y0) % 5u;
                uint64_t tmp = s[x1 + 5 * y1];
                s[x1 + 5 * y1] = rotl64(t, rholane[i]);
                t = tmp;
                x0 = x1;
                y0 = y1;
            }
        }

        /* chi */
        for (y = 0; y < 5; y++) {
            for (x = 0; x < 5; x++) {
                B[x + 5 * y] = s[x + 5 * y];
            }
            for (x = 0; x < 5; x++) {
                s[x + 5 * y] = B[x + 5 * y]
                    ^ ((~B[((x + 1) % 5) + 5 * y]) & B[((x + 2) % 5) + 5 * y]);
            }
        }

        /* iota */
        s[0] ^= RC[round];
    }
}

static uint64_t load_le64(const uint8_t *p)
{
    unsigned i;
    uint64_t v = 0;
    for (i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8u * i);
    }
    return v;
}

static void store_le64(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static void sponge(const uint8_t *in, size_t inlen,
                   uint8_t suffix, size_t rate,
                   uint8_t *out, size_t outlen)
{
    uint64_t s[25];
    size_t i;
    memset(s, 0, sizeof(s));

    while (inlen >= rate) {
        for (i = 0; i < rate / 8u; i++) {
            s[i] ^= load_le64(in + 8u * i);
        }
        keccak_f1600(s);
        in += rate;
        inlen -= rate;
    }

    {
        uint8_t block[200];
        memset(block, 0, sizeof(block));
        if (inlen > 0) {
            memcpy(block, in, inlen);
        }
        block[inlen] ^= suffix;
        block[rate - 1u] ^= 0x80u;
        for (i = 0; i < rate / 8u; i++) {
            s[i] ^= load_le64(block + 8u * i);
        }
        keccak_f1600(s);
        atn_memzero(block, sizeof(block));
    }

    while (outlen > 0) {
        size_t take = outlen < rate ? outlen : rate;
        uint8_t tmp[200];
        for (i = 0; i < rate / 8u; i++) {
            store_le64(tmp + 8u * i, s[i]);
        }
        memcpy(out, tmp, take);
        out += take;
        outlen -= take;
        if (outlen > 0) {
            keccak_f1600(s);
        }
        atn_memzero(tmp, sizeof(tmp));
    }
    atn_memzero(s, sizeof(s));
}

int atn_sha3_256(const void *data, size_t n, uint8_t out[32])
{
    if (out == NULL || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    sponge((const uint8_t *)data, n, 0x06u, 136u, out, 32);
    return ATN_OK;
}

int atn_sha3_512(const void *data, size_t n, uint8_t out[64])
{
    if (out == NULL || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    sponge((const uint8_t *)data, n, 0x06u, 72u, out, 64);
    return ATN_OK;
}

int atn_shake128(const void *data, size_t n, uint8_t *out, size_t outlen)
{
    if ((out == NULL && outlen != 0) || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    sponge((const uint8_t *)data, n, 0x1Fu, 168u, out, outlen);
    return ATN_OK;
}

int atn_shake256(const void *data, size_t n, uint8_t *out, size_t outlen)
{
    if ((out == NULL && outlen != 0) || (data == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    sponge((const uint8_t *)data, n, 0x1Fu, 136u, out, outlen);
    return ATN_OK;
}

/* Incremental SHAKE128 for FIPS 203 SampleNTT (XOF.Init/Absorb/Squeeze). */
void atn_shake128_init(atn_shake128_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void atn_shake128_absorb(atn_shake128_ctx *ctx, const uint8_t *in, size_t n)
{
    const size_t rate = 168u;
    while (n > 0) {
        size_t take = rate - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(ctx->buf + ctx->used, in, take);
        ctx->used += take;
        in += take;
        n -= take;
        if (ctx->used == rate) {
            size_t i;
            for (i = 0; i < rate / 8u; i++) {
                ctx->s[i] ^= load_le64(ctx->buf + 8u * i);
            }
            keccak_f1600(ctx->s);
            ctx->used = 0;
        }
    }
}

void atn_shake128_finalize(atn_shake128_ctx *ctx)
{
    const size_t rate = 168u;
    size_t i;
    /* Unused tail must be zero; leftover from a previous full rate-block
     * would otherwise enter the domain padding (FIPS 202 §B.1). */
    memset(ctx->buf + ctx->used, 0, rate - ctx->used);
    ctx->buf[ctx->used] ^= 0x1Fu;
    ctx->buf[rate - 1u] ^= 0x80u;
    for (i = 0; i < rate / 8u; i++) {
        ctx->s[i] ^= load_le64(ctx->buf + 8u * i);
    }
    keccak_f1600(ctx->s);
    ctx->used = 0;
    ctx->squeezing = 1;
}

void atn_shake128_squeeze(atn_shake128_ctx *ctx, uint8_t *out, size_t n)
{
    const size_t rate = 168u;
    while (n > 0) {
        size_t i, take;
        /* After finalize, s is the first rate-block. Serialize once per block. */
        if (ctx->used == 0) {
            for (i = 0; i < rate / 8u; i++) {
                store_le64(ctx->buf + 8u * i, ctx->s[i]);
            }
        }
        take = rate - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(out, ctx->buf + ctx->used, take);
        ctx->used += take;
        if (ctx->used == rate) {
            keccak_f1600(ctx->s);
            ctx->used = 0;
        }
        out += take;
        n -= take;
    }
}

/* Incremental SHAKE256 for FIPS 204 H.Init/Absorb/Squeeze (rate 136). */
void atn_shake256_init(atn_shake256_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void atn_shake256_absorb(atn_shake256_ctx *ctx, const uint8_t *in, size_t n)
{
    const size_t rate = 136u;
    while (n > 0) {
        size_t take = rate - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(ctx->buf + ctx->used, in, take);
        ctx->used += take;
        in += take;
        n -= take;
        if (ctx->used == rate) {
            size_t i;
            for (i = 0; i < rate / 8u; i++) {
                ctx->s[i] ^= load_le64(ctx->buf + 8u * i);
            }
            keccak_f1600(ctx->s);
            ctx->used = 0;
        }
    }
}

void atn_shake256_finalize(atn_shake256_ctx *ctx)
{
    const size_t rate = 136u;
    size_t i;
    memset(ctx->buf + ctx->used, 0, rate - ctx->used);
    ctx->buf[ctx->used] ^= 0x1Fu;
    ctx->buf[rate - 1u] ^= 0x80u;
    for (i = 0; i < rate / 8u; i++) {
        ctx->s[i] ^= load_le64(ctx->buf + 8u * i);
    }
    keccak_f1600(ctx->s);
    ctx->used = 0;
    ctx->squeezing = 1;
}

void atn_shake256_squeeze(atn_shake256_ctx *ctx, uint8_t *out, size_t n)
{
    const size_t rate = 136u;
    while (n > 0) {
        size_t i, take;
        if (ctx->used == 0) {
            for (i = 0; i < rate / 8u; i++) {
                store_le64(ctx->buf + 8u * i, ctx->s[i]);
            }
        }
        take = rate - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(out, ctx->buf + ctx->used, take);
        ctx->used += take;
        if (ctx->used == rate) {
            keccak_f1600(ctx->s);
            ctx->used = 0;
        }
        out += take;
        n -= take;
    }
}
