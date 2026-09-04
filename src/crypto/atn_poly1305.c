/*
 * Module: atn_poly1305.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §2.5 (Poly1305), clamp as given there.
 *
 * Representation: five 26-bit limbs so h and r live in 130 bits without a
 * bignum library (RFC 8439 §3 forbids generic bigint for this). p = 2^130-5.
 * Limb multiply uses the identity 2^130 ≡ 5 (mod p) for the wrap terms.
 *
 * ISS-0003: inner loops have no secret-dependent branches; we have not yet
 * timed this on target CPUs. Do not treat "constant-time" as measured.
 */

#include "atn_crypto.h"

#include <string.h>

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * Process one 16-byte (or shorter, zero-padded) block.
 * hibit = 1 adds 2^128 (full block, RFC: "add one bit beyond the octets").
 * hibit = 0 means the 0x01 byte was already placed in the buffer at offset
 * equal to the original length (short final block).
 */
static void poly_block(atn_poly1305_ctx *st, const uint8_t m[16], unsigned hibit)
{
    const uint32_t *r = st->r;
    uint32_t s1 = r[1] * 5u;
    uint32_t s2 = r[2] * 5u;
    uint32_t s3 = r[3] * 5u;
    uint32_t s4 = r[4] * 5u;
    uint32_t t0 = load_le32(m + 0);
    uint32_t t1 = load_le32(m + 4);
    uint32_t t2 = load_le32(m + 8);
    uint32_t t3 = load_le32(m + 12);
    uint64_t h0, h1, h2, h3, h4;
    uint64_t d0, d1, d2, d3, d4, c;

    h0 = st->h[0] + (t0 & 0x3ffffffu);
    h1 = st->h[1] + (((t0 >> 26) | (t1 << 6)) & 0x3ffffffu);
    h2 = st->h[2] + (((t1 >> 20) | (t2 << 12)) & 0x3ffffffu);
    h3 = st->h[3] + (((t2 >> 14) | (t3 << 18)) & 0x3ffffffu);
    h4 = st->h[4] + ((t3 >> 8) | (hibit << 24));

    /* (h + n) * r  with 2^130 ≡ 5 (mod p) wrapping the high limbs. */
    d0 = h0 * r[0] + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
    d1 = h0 * r[1] + h1 * r[0] + h2 * s4 + h3 * s3 + h4 * s2;
    d2 = h0 * r[2] + h1 * r[1] + h2 * r[0] + h3 * s4 + h4 * s3;
    d3 = h0 * r[3] + h1 * r[2] + h2 * r[1] + h3 * r[0] + h4 * s4;
    d4 = h0 * r[4] + h1 * r[3] + h2 * r[2] + h3 * r[1] + h4 * r[0];

    c = d0 >> 26; st->h[0] = (uint32_t)(d0 & 0x3ffffffu); d1 += c;
    c = d1 >> 26; st->h[1] = (uint32_t)(d1 & 0x3ffffffu); d2 += c;
    c = d2 >> 26; st->h[2] = (uint32_t)(d2 & 0x3ffffffu); d3 += c;
    c = d3 >> 26; st->h[3] = (uint32_t)(d3 & 0x3ffffffu); d4 += c;
    c = d4 >> 26; st->h[4] = (uint32_t)(d4 & 0x3ffffffu);
    st->h[0] += (uint32_t)(c * 5u);
    c = st->h[0] >> 26; st->h[0] &= 0x3ffffffu;
    st->h[1] += (uint32_t)c;
}

int atn_poly1305_init(atn_poly1305_ctx *ctx, const uint8_t key[ATN_POLY1305_KEY_LEN])
{
    uint8_t rbytes[16];
    uint32_t t0, t1, t2, t3;

    if (ctx == NULL || key == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_memzero(ctx, sizeof(*ctx));

    memcpy(rbytes, key, 16);
    /* RFC 8439 §2.5 clamp. */
    rbytes[3]  &= 15;
    rbytes[7]  &= 15;
    rbytes[11] &= 15;
    rbytes[15] &= 15;
    rbytes[4]  &= 252;
    rbytes[8]  &= 252;
    rbytes[12] &= 252;

    t0 = load_le32(rbytes + 0);
    t1 = load_le32(rbytes + 4);
    t2 = load_le32(rbytes + 8);
    t3 = load_le32(rbytes + 12);
    ctx->r[0] = t0 & 0x3ffffffu;
    ctx->r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3ffffffu;
    ctx->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffffffu;
    ctx->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3ffffffu;
    ctx->r[4] = (t3 >> 8) & 0x3ffffffu;

    ctx->s[0] = load_le32(key + 16);
    ctx->s[1] = load_le32(key + 20);
    ctx->s[2] = load_le32(key + 24);
    ctx->s[3] = load_le32(key + 28);

    atn_memzero(rbytes, sizeof(rbytes));
    return ATN_OK;
}

int atn_poly1305_update(atn_poly1305_ctx *ctx, const uint8_t *msg, size_t n)
{
    if (ctx == NULL || ctx->finalized) {
        return ATN_ERR_PARAM;
    }
    if (n == 0) {
        return ATN_OK;
    }
    if (msg == NULL) {
        return ATN_ERR_PARAM;
    }

    while (n > 0) {
        size_t take = 16u - ctx->used;
        if (take > n) {
            take = n;
        }
        memcpy(ctx->buf + ctx->used, msg, take);
        ctx->used += take;
        msg += take;
        n -= take;
        if (ctx->used == 16u) {
            poly_block(ctx, ctx->buf, 1);
            ctx->used = 0;
        }
    }
    return ATN_OK;
}

int atn_poly1305_final(atn_poly1305_ctx *ctx, uint8_t tag[ATN_POLY1305_TAG_LEN])
{
    uint64_t h0, h1, h2, h3, h4;
    uint64_t g0, g1, g2, g3, g4;
    uint64_t f0, f1, c;
    uint32_t mask;

    if (ctx == NULL || tag == NULL || ctx->finalized) {
        return ATN_ERR_PARAM;
    }

    if (ctx->used > 0) {
        uint8_t last[16];
        memset(last, 0, sizeof(last));
        memcpy(last, ctx->buf, ctx->used);
        /* RFC 8439 §2.5: one bit beyond the number of octets in the short block. */
        last[ctx->used] = 1;
        poly_block(ctx, last, 0);
        atn_memzero(last, sizeof(last));
    }

    /* Finish carrying so each of h0..h3 is < 2^26. */
    h0 = ctx->h[0];
    h1 = ctx->h[1];
    h2 = ctx->h[2];
    h3 = ctx->h[3];
    h4 = ctx->h[4];
    c = h1 >> 26; h1 &= 0x3ffffffu; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffffu; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffffu; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffffu; h0 += c * 5u;
    c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;

    /* If h >= p = 2^130-5, replace h with h-p. Equivalent: h+5 >= 2^130. */
    g0 = h0 + 5u;
    g1 = h1 + (g0 >> 26); g0 &= 0x3ffffffu;
    g2 = h2 + (g1 >> 26); g1 &= 0x3ffffffu;
    g3 = h3 + (g2 >> 26); g2 &= 0x3ffffffu;
    g4 = h4 + (g3 >> 26); g3 &= 0x3ffffffu;
    /* g4 >> 26 is 1 iff we crossed 2^130. Select g in that case, else h. */
    mask = (uint32_t)(g4 >> 26) - 1u; /* 0 if select g, 0xffffffff if select h */
    h0 = (h0 & mask) | (g0 & ~mask);
    h1 = (h1 & mask) | (g1 & ~mask);
    h2 = (h2 & mask) | (g2 & ~mask);
    h3 = (h3 & mask) | (g3 & ~mask);
    h4 = (h4 & mask) | ((g4 & 0x3ffffffu) & ~mask);

    /* Serialize 128 least-significant bits, then add s, mod 2^128 (§2.5 last paragraph). */
    f0 = h0 | (h1 << 26) | (h2 << 52);
    f1 = (h2 >> 12) | (h3 << 14) | ((h4 & 0xffffffu) << 40);
    f0 += (uint64_t)ctx->s[0] | ((uint64_t)ctx->s[1] << 32);
    c = f0 < ((uint64_t)ctx->s[0] | ((uint64_t)ctx->s[1] << 32));
    f1 += ((uint64_t)ctx->s[2] | ((uint64_t)ctx->s[3] << 32)) + c;

    store_le32(tag + 0, (uint32_t)f0);
    store_le32(tag + 4, (uint32_t)(f0 >> 32));
    store_le32(tag + 8, (uint32_t)f1);
    store_le32(tag + 12, (uint32_t)(f1 >> 32));

    ctx->finalized = 1;
    atn_memzero(ctx->r, sizeof(ctx->r));
    atn_memzero(ctx->s, sizeof(ctx->s));
    atn_memzero(ctx->h, sizeof(ctx->h));
    atn_memzero(ctx->buf, sizeof(ctx->buf));
    return ATN_OK;
}

int atn_poly1305(const uint8_t key[ATN_POLY1305_KEY_LEN],
                 const uint8_t *msg, size_t n,
                 uint8_t tag[ATN_POLY1305_TAG_LEN])
{
    atn_poly1305_ctx ctx;
    int rc = atn_poly1305_init(&ctx, key);
    if (rc == ATN_OK) {
        rc = atn_poly1305_update(&ctx, msg, n);
    }
    if (rc == ATN_OK) {
        rc = atn_poly1305_final(&ctx, tag);
    }
    atn_memzero(&ctx, sizeof(ctx));
    return rc;
}
