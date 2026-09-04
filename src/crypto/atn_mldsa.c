/*
 * Module: atn_mldsa.c
 * REQ:    ISS-0005 / REQ-5.1 (DEC-0018)
 * Spec:   FIPS 204 ML-DSA-87 (NIST category 5)
 *
 * Table 1: q=8380417, ζ=1753, d=13, τ=60, λ=256, γ1=2^19,
 * γ2=(q-1)/32, (k,ℓ)=(8,7), η=2, β=120, ω=75.
 * Algorithms 6–8, 14–42. Zetas: Appendix B.
 */

#include "atn_crypto.h"

#include <string.h>

#define Q          8380417
#define N          256
#define D          13
#define TAU        60
#define GAMMA1     (1 << 19)
#define GAMMA2     ((Q - 1) / 32)
#define K          8
#define LVEC       7
#define ETA        2
#define BETA       120
#define OMEGA      75
#define CTILDE     64
#define SEEDBYTES  32

typedef int32_t poly[N];

/* FIPS 204 Appendix B: zetas[m] = ζ^{BitRev8(m)} mod q, m=0..255. */
static const int32_t ZETAS[256] = {
    0, 4808194, 3765607, 3761513, 5178923, 5496691, 5234739, 5178987,
    7778734, 3542485, 2682288, 2129892, 3764867, 7375178, 557458, 7159240,
    5010068, 4317364, 2663378, 6705802, 4855975, 7946292, 676590, 7044481,
    5152541, 1714295, 2453983, 1460718, 7737789, 4795319, 2815639, 2283733,
    3602218, 3182878, 2740543, 4793971, 5269599, 2101410, 3704823, 1159875,
    394148, 928749, 1095468, 4874037, 2071829, 4361428, 3241972, 2156050,
    3415069, 1759347, 7562881, 4805951, 3756790, 6444618, 6663429, 4430364,
    5483103, 3192354, 556856, 3870317, 2917338, 1853806, 3345963, 1858416,
    3073009, 1277625, 5744944, 3852015, 4183372, 5157610, 5258977, 8106357,
    2508980, 2028118, 1937570, 4564692, 2811291, 5396636, 7270901, 4158088,
    1528066, 482649, 1148858, 5418153, 7814814, 169688, 2462444, 5046034,
    4213992, 4892034, 1987814, 5183169, 1736313, 235407, 5130263, 3258457,
    5801164, 1787943, 5989328, 6125690, 3482206, 4197502, 7080401, 6018354,
    7062739, 2461387, 3035980, 621164, 3901472, 7153756, 2925816, 3374250,
    1356448, 5604662, 2683270, 5601629, 4912752, 2312838, 7727142, 7921254,
    348812, 8052569, 1011223, 6026202, 4561790, 6458164, 6143691, 1744507,
    1753, 6444997, 5720892, 6924527, 2660408, 6600190, 8321269, 2772600,
    1182243, 87208, 636927, 4415111, 4423672, 6084020, 5095502, 4663471,
    8352605, 822541, 1009365, 5926272, 6400920, 1596822, 4423473, 4620952,
    6695264, 4969849, 2678278, 4611469, 4829411, 635956, 8129971, 5925040,
    4234153, 6607829, 2192938, 6653329, 2387513, 4768667, 8111961, 5199961,
    3747250, 2296099, 1239911, 4541938, 3195676, 2642980, 1254190, 8368000,
    2998219, 141835, 8291116, 2513018, 7025525, 613238, 7070156, 6161950,
    7921677, 6458423, 4040196, 4908348, 2039144, 6500539, 7561656, 6201452,
    6757063, 2105286, 6006015, 6346610, 586241, 7200804, 527981, 5637006,
    6903432, 1994046, 2491325, 6987258, 507927, 7192532, 7655613, 6545891,
    5346675, 8041997, 2647994, 3009748, 5767564, 4148469, 749577, 4357667,
    3980599, 2569011, 6764887, 1723229, 1665318, 2028038, 1163598, 5011144,
    3994671, 8368538, 7009900, 3020393, 3363542, 214880, 545376, 7609976,
    3105558, 7277073, 508145, 7826699, 860144, 3430436, 140244, 6866265,
    6195333, 3123762, 2358373, 6187330, 5365997, 6663603, 2926054, 7987710,
    8077412, 3531229, 4405932, 4606686, 1900052, 7598542, 1054478, 7648983
};

static int32_t fqred(int64_t a)
{
    a %= Q;
    if (a < 0) {
        a += Q;
    }
    return (int32_t)a;
}

static int32_t fqadd(int32_t a, int32_t b)
{
    return fqred((int64_t)a + b);
}

static int32_t fqsub(int32_t a, int32_t b)
{
    return fqred((int64_t)a - b);
}

static int32_t fqmul(int32_t a, int32_t b)
{
    return fqred((int64_t)a * b);
}

/* FIPS 204 §2.3: m mod± α in (−⌈α/2⌉, ⌊α/2⌋]. */
static int32_t modpm(int32_t a, int32_t alpha)
{
    int32_t t = a % alpha;
    if (t < 0) {
        t += alpha;
    }
    if (t > alpha / 2) {
        t -= alpha;
    }
    return t;
}

static int32_t center_q(int32_t a)
{
    a = fqred(a);
    if (a > (Q - 1) / 2) {
        a -= Q;
    }
    return a;
}

static void poly_zero(poly r)
{
    memset(r, 0, sizeof(poly));
}

static void poly_add(poly r, const poly a, const poly b)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        r[i] = fqadd(a[i], b[i]);
    }
}

static void poly_sub(poly r, const poly a, const poly b)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        r[i] = fqsub(a[i], b[i]);
    }
}

static void poly_pointwise(poly r, const poly a, const poly b)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        r[i] = fqmul(a[i], b[i]);
    }
}

static int32_t poly_infnorm(const poly w)
{
    unsigned i;
    int32_t m = 0;
    for (i = 0; i < N; i++) {
        int32_t t = center_q(w[i]);
        if (t < 0) {
            t = -t;
        }
        if (t > m) {
            m = t;
        }
    }
    return m;
}

/* Algorithm 41 */
static void ntt(poly w)
{
    unsigned m = 0;
    unsigned len = 128;
    unsigned start, j;
    while (len >= 1) {
        start = 0;
        while (start < N) {
            int32_t z;
            m++;
            z = ZETAS[m];
            for (j = start; j < start + len; j++) {
                int32_t t = fqmul(z, w[j + len]);
                w[j + len] = fqsub(w[j], t);
                w[j] = fqadd(w[j], t);
            }
            start += 2u * len;
        }
        len /= 2u;
    }
}

/* Algorithm 42. f = 256^{-1} mod q = 8347681. */
static void invntt(poly w)
{
    unsigned m = 256;
    unsigned len = 1;
    unsigned start, j;
    while (len < N) {
        start = 0;
        while (start < N) {
            int32_t z;
            m--;
            z = fqred(-(int64_t)ZETAS[m]);
            for (j = start; j < start + len; j++) {
                int32_t t = w[j];
                w[j] = fqadd(t, w[j + len]);
                w[j + len] = fqmul(z, fqsub(t, w[j + len]));
            }
            start += 2u * len;
        }
        len *= 2u;
    }
    for (j = 0; j < N; j++) {
        w[j] = fqmul(w[j], 8347681);
    }
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static unsigned bitlen_u(uint32_t a)
{
    unsigned n = 0;
    while (a > 0) {
        n++;
        a >>= 1;
    }
    return n;
}

static void bits_push(uint8_t *out, size_t *bitpos, uint32_t val, unsigned nbits)
{
    unsigned i;
    for (i = 0; i < nbits; i++) {
        size_t p = *bitpos;
        if (val & 1u) {
            out[p / 8u] |= (uint8_t)(1u << (p % 8u));
        }
        val >>= 1;
        (*bitpos)++;
    }
}

static uint32_t bits_pull(const uint8_t *in, size_t *bitpos, unsigned nbits)
{
    unsigned i;
    uint32_t v = 0;
    for (i = 0; i < nbits; i++) {
        size_t p = *bitpos;
        if ((in[p / 8u] >> (p % 8u)) & 1u) {
            v |= (1u << i);
        }
        (*bitpos)++;
    }
    return v;
}

/* Algorithms 16 / 18 */
static void simple_bitpack(uint8_t *out, const poly w, uint32_t b)
{
    unsigned bits = bitlen_u(b);
    size_t pos = 0;
    unsigned i;
    memset(out, 0, 32u * bits);
    for (i = 0; i < N; i++) {
        bits_push(out, &pos, (uint32_t)w[i], bits);
    }
}

static void simple_bitunpack(poly w, const uint8_t *in, uint32_t b)
{
    unsigned bits = bitlen_u(b);
    size_t pos = 0;
    unsigned i;
    for (i = 0; i < N; i++) {
        w[i] = (int32_t)bits_pull(in, &pos, bits);
    }
}

/* Algorithms 17 / 19 */
static void bitpack(uint8_t *out, const poly w, int32_t a, int32_t b)
{
    unsigned bits = bitlen_u((uint32_t)(a + b));
    size_t pos = 0;
    unsigned i;
    memset(out, 0, 32u * bits);
    for (i = 0; i < N; i++) {
        bits_push(out, &pos, (uint32_t)(b - w[i]), bits);
    }
}

static void bitunpack(poly w, const uint8_t *in, int32_t a, int32_t b)
{
    unsigned bits = bitlen_u((uint32_t)(a + b));
    size_t pos = 0;
    unsigned i;
    for (i = 0; i < N; i++) {
        w[i] = b - (int32_t)bits_pull(in, &pos, bits);
    }
}

/* Algorithm 20 */
static void hint_bitpack(uint8_t *y, const poly h[K])
{
    unsigned i, j, index = 0;
    memset(y, 0, OMEGA + K);
    for (i = 0; i < K; i++) {
        for (j = 0; j < N; j++) {
            if (h[i][j] != 0) {
                y[index] = (uint8_t)j;
                index++;
            }
        }
        y[OMEGA + i] = (uint8_t)index;
    }
}

/* Algorithm 21. Returns 0 ok, -1 malformed. */
static int hint_bitunpack(poly h[K], const uint8_t *y)
{
    unsigned i, index = 0;
    for (i = 0; i < K; i++) {
        poly_zero(h[i]);
    }
    for (i = 0; i < K; i++) {
        unsigned first;
        if (y[OMEGA + i] < index || y[OMEGA + i] > OMEGA) {
            return -1;
        }
        first = index;
        while (index < y[OMEGA + i]) {
            if (index > first && y[index - 1] >= y[index]) {
                return -1;
            }
            h[i][y[index]] = 1;
            index++;
        }
    }
    for (i = index; i < OMEGA; i++) {
        if (y[i] != 0) {
            return -1;
        }
    }
    return 0;
}

/* Algorithm 14 */
static int coeff_from_three(uint8_t b0, uint8_t b1, uint8_t b2, int32_t *z)
{
    uint32_t v;
    if (b2 > 127) {
        b2 = (uint8_t)(b2 - 128);
    }
    v = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16);
    if (v < (uint32_t)Q) {
        *z = (int32_t)v;
        return 0;
    }
    return -1;
}

/* Algorithm 15, η=2 */
static int coeff_from_half(unsigned b, int32_t *z)
{
    if (b < 15u) {
        *z = 2 - (int32_t)(b % 5u);
        return 0;
    }
    return -1;
}

/* Algorithm 30 */
static void rej_ntt_poly(poly a, const uint8_t rho[34])
{
    atn_shake128_ctx ctx;
    unsigned j = 0;
    atn_shake128_init(&ctx);
    atn_shake128_absorb(&ctx, rho, 34);
    atn_shake128_finalize(&ctx);
    while (j < N) {
        uint8_t s[3];
        int32_t z;
        atn_shake128_squeeze(&ctx, s, 3);
        if (coeff_from_three(s[0], s[1], s[2], &z) == 0) {
            a[j++] = z;
        }
    }
}

/* Algorithm 31 */
static void rej_bounded_poly(poly a, const uint8_t rho[66])
{
    atn_shake256_ctx ctx;
    unsigned j = 0;
    atn_shake256_init(&ctx);
    atn_shake256_absorb(&ctx, rho, 66);
    atn_shake256_finalize(&ctx);
    while (j < N) {
        uint8_t z;
        int32_t z0, z1;
        atn_shake256_squeeze(&ctx, &z, 1);
        if (coeff_from_half(z & 0x0fu, &z0) == 0) {
            a[j++] = z0;
        }
        if (j < N && coeff_from_half(z >> 4, &z1) == 0) {
            a[j++] = z1;
        }
    }
}

/* Algorithm 32 */
static void expand_a(poly a[K * LVEC], const uint8_t rho[32])
{
    unsigned r, s;
    for (r = 0; r < K; r++) {
        for (s = 0; s < LVEC; s++) {
            uint8_t seed[34];
            memcpy(seed, rho, 32);
            seed[32] = (uint8_t)s;
            seed[33] = (uint8_t)r;
            rej_ntt_poly(a[r * LVEC + s], seed);
        }
    }
}

/* Algorithm 33 */
static void expand_s(poly s1[LVEC], poly s2[K], const uint8_t rho[64])
{
    unsigned r;
    uint8_t seed[66];
    memcpy(seed, rho, 64);
    for (r = 0; r < LVEC; r++) {
        wr16(seed + 64, r);
        rej_bounded_poly(s1[r], seed);
    }
    for (r = 0; r < K; r++) {
        wr16(seed + 64, r + LVEC);
        rej_bounded_poly(s2[r], seed);
    }
}

/* Algorithm 34 */
static void expand_mask(poly y[LVEC], const uint8_t rho[64], unsigned mu)
{
    unsigned r;
    unsigned c = 1u + bitlen_u((uint32_t)(GAMMA1 - 1));
    uint8_t seed[66];
    uint8_t v[32 * 20];
    memcpy(seed, rho, 64);
    for (r = 0; r < LVEC; r++) {
        wr16(seed + 64, mu + r);
        atn_shake256(seed, 66, v, 32u * c);
        bitunpack(y[r], v, GAMMA1 - 1, GAMMA1);
    }
}

/* Algorithm 29 */
static void sample_in_ball(poly c, const uint8_t rho[CTILDE])
{
    atn_shake256_ctx ctx;
    uint8_t s[8], j;
    unsigned i, hbit;
    atn_shake256_init(&ctx);
    atn_shake256_absorb(&ctx, rho, CTILDE);
    atn_shake256_finalize(&ctx);
    atn_shake256_squeeze(&ctx, s, 8);
    poly_zero(c);
    for (i = 256 - TAU; i < 256; i++) {
        atn_shake256_squeeze(&ctx, &j, 1);
        while (j > i) {
            atn_shake256_squeeze(&ctx, &j, 1);
        }
        c[i] = c[j];
        hbit = (s[(i + TAU - 256) / 8u] >> ((i + TAU - 256) % 8u)) & 1u;
        c[j] = hbit ? -1 : 1;
    }
}

/* Algorithms 35–40 */
static void power2round(int32_t r, int32_t *r1, int32_t *r0)
{
    int32_t rp = fqred(r);
    *r0 = modpm(rp, 1 << D);
    *r1 = (rp - *r0) >> D;
}

static void decompose(int32_t r, int32_t *r1, int32_t *r0)
{
    int32_t rp = fqred(r);
    *r0 = modpm(rp, 2 * GAMMA2);
    if (rp - *r0 == Q - 1) {
        *r1 = 0;
        *r0 = *r0 - 1;
    } else {
        *r1 = (rp - *r0) / (2 * GAMMA2);
    }
}

static int32_t highbits(int32_t r)
{
    int32_t r1, r0;
    decompose(r, &r1, &r0);
    return r1;
}

static int32_t lowbits(int32_t r)
{
    int32_t r1, r0;
    decompose(r, &r1, &r0);
    return r0;
}

static int makehint(int32_t z, int32_t r)
{
    return highbits(r) != highbits(fqadd(r, z));
}

static int32_t usehint(int h, int32_t r)
{
    int32_t m = (Q - 1) / (2 * GAMMA2);
    int32_t r1, r0;
    decompose(r, &r1, &r0);
    if (h == 1 && r0 > 0) {
        return (r1 + 1) % m;
    }
    if (h == 1 && r0 <= 0) {
        return (r1 - 1 + m) % m;
    }
    return r1;
}

static void poly_highbits(poly r1, const poly r)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        r1[i] = highbits(r[i]);
    }
}

static void poly_lowbits(poly r0, const poly r)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        r0[i] = lowbits(r[i]);
    }
}

static void poly_power2round(poly t1, poly t0, const poly t)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        power2round(t[i], &t1[i], &t0[i]);
    }
}

static void matrix_vector_ntt(poly w[K], const poly a[K * LVEC], const poly v[LVEC])
{
    unsigned i, j;
    poly acc, tmp;
    for (i = 0; i < K; i++) {
        poly_zero(acc);
        for (j = 0; j < LVEC; j++) {
            poly_pointwise(tmp, a[i * LVEC + j], v[j]);
            poly_add(acc, acc, tmp);
        }
        memcpy(w[i], acc, sizeof(poly));
    }
}

static void w1_encode(uint8_t *out, const poly w1[K])
{
    unsigned i;
    unsigned bits = bitlen_u((uint32_t)((Q - 1) / (2 * GAMMA2) - 1));
    for (i = 0; i < K; i++) {
        simple_bitpack(out + i * 32u * bits, w1[i],
                       (uint32_t)((Q - 1) / (2 * GAMMA2) - 1));
    }
}

static unsigned hint_ones(const poly h[K])
{
    unsigned i, j, n = 0;
    for (i = 0; i < K; i++) {
        for (j = 0; j < N; j++) {
            if (h[i][j] != 0) {
                n++;
            }
        }
    }
    return n;
}

int atn_mldsa87_keygen_internal(const uint8_t xi[32],
                                uint8_t pk[ATN_MLDSA87_PK_LEN],
                                uint8_t sk[ATN_MLDSA87_SK_LEN])
{
    uint8_t xin[34], exp[128];
    const uint8_t *rho, *rhop, *Kseed;
    poly a[K * LVEC], s1[LVEC], s2[K], t[K], t1[K], t0[K], s1h[LVEC];
    unsigned i;
    uint8_t tr[64];

    if (xi == NULL || pk == NULL || sk == NULL) {
        return ATN_ERR_PARAM;
    }
    memcpy(xin, xi, 32);
    xin[32] = (uint8_t)K;
    xin[33] = (uint8_t)LVEC;
    atn_shake256(xin, 34, exp, 128);
    rho = exp;
    rhop = exp + 32;
    Kseed = exp + 96;

    expand_a(a, rho);
    expand_s(s1, s2, rhop);
    for (i = 0; i < LVEC; i++) {
        memcpy(s1h[i], s1[i], sizeof(poly));
        ntt(s1h[i]);
    }
    matrix_vector_ntt(t, a, s1h);
    for (i = 0; i < K; i++) {
        invntt(t[i]);
        poly_add(t[i], t[i], s2[i]);
        poly_power2round(t1[i], t0[i], t[i]);
    }

    memcpy(pk, rho, 32);
    for (i = 0; i < K; i++) {
        simple_bitpack(pk + 32 + i * 320, t1[i], (1u << 10) - 1u);
    }
    atn_shake256(pk, ATN_MLDSA87_PK_LEN, tr, 64);

    memcpy(sk, rho, 32);
    memcpy(sk + 32, Kseed, 32);
    memcpy(sk + 64, tr, 64);
    for (i = 0; i < LVEC; i++) {
        bitpack(sk + 128 + i * 96, s1[i], ETA, ETA);
    }
    for (i = 0; i < K; i++) {
        bitpack(sk + 128 + LVEC * 96 + i * 96, s2[i], ETA, ETA);
    }
    for (i = 0; i < K; i++) {
        bitpack(sk + 128 + (LVEC + K) * 96 + i * 416, t0[i],
                (1 << (D - 1)) - 1, 1 << (D - 1));
    }
    atn_memzero(exp, sizeof(exp));
    atn_memzero(xin, sizeof(xin));
    return ATN_OK;
}

static void sk_decode(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                      uint8_t rho[32], uint8_t Kseed[32], uint8_t tr[64],
                      poly s1[LVEC], poly s2[K], poly t0[K])
{
    unsigned i;
    memcpy(rho, sk, 32);
    memcpy(Kseed, sk + 32, 32);
    memcpy(tr, sk + 64, 64);
    for (i = 0; i < LVEC; i++) {
        bitunpack(s1[i], sk + 128 + i * 96, ETA, ETA);
    }
    for (i = 0; i < K; i++) {
        bitunpack(s2[i], sk + 128 + LVEC * 96 + i * 96, ETA, ETA);
    }
    for (i = 0; i < K; i++) {
        bitunpack(t0[i], sk + 128 + (LVEC + K) * 96 + i * 416,
                  (1 << (D - 1)) - 1, 1 << (D - 1));
    }
}

int atn_mldsa87_sign_internal(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                              const uint8_t *mp, size_t mp_len,
                              const uint8_t rnd[32],
                              uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    uint8_t rho[32], Kseed[32], tr[64], mu[64], rhopp[64];
    uint8_t w1b[K * 128], ctilde[CTILDE];
    poly a[K * LVEC], s1[LVEC], s2[K], t0[K];
    poly s1h[LVEC], s2h[K], t0h[K];
    poly y[LVEC], yh[LVEC], w[K], w1[K], c, ch;
    poly cs1[LVEC], cs2[K], z[LVEC], r0[K], ct0[K], h[K], tmp[K];
    unsigned i, kappa = 0, loops = 0;
    int ok = 0;
    atn_shake256_ctx hctx;

    if (sk == NULL || sig == NULL || rnd == NULL || (mp == NULL && mp_len != 0)) {
        return ATN_ERR_PARAM;
    }
    sk_decode(sk, rho, Kseed, tr, s1, s2, t0);
    for (i = 0; i < LVEC; i++) {
        memcpy(s1h[i], s1[i], sizeof(poly));
        ntt(s1h[i]);
    }
    for (i = 0; i < K; i++) {
        memcpy(s2h[i], s2[i], sizeof(poly));
        ntt(s2h[i]);
        memcpy(t0h[i], t0[i], sizeof(poly));
        ntt(t0h[i]);
    }
    expand_a(a, rho);

    atn_shake256_init(&hctx);
    atn_shake256_absorb(&hctx, tr, 64);
    atn_shake256_absorb(&hctx, mp, mp_len);
    atn_shake256_finalize(&hctx);
    atn_shake256_squeeze(&hctx, mu, 64);

    atn_shake256_init(&hctx);
    atn_shake256_absorb(&hctx, Kseed, 32);
    atn_shake256_absorb(&hctx, rnd, 32);
    atn_shake256_absorb(&hctx, mu, 64);
    atn_shake256_finalize(&hctx);
    atn_shake256_squeeze(&hctx, rhopp, 64);

    while (!ok) {
        if (++loops > 814) {
            return ATN_ERR_STATE;
        }
        expand_mask(y, rhopp, kappa);
        for (i = 0; i < LVEC; i++) {
            memcpy(yh[i], y[i], sizeof(poly));
            ntt(yh[i]);
        }
        matrix_vector_ntt(w, a, yh);
        for (i = 0; i < K; i++) {
            invntt(w[i]);
            poly_highbits(w1[i], w[i]);
        }
        w1_encode(w1b, w1);

        atn_shake256_init(&hctx);
        atn_shake256_absorb(&hctx, mu, 64);
        atn_shake256_absorb(&hctx, w1b, sizeof(w1b));
        atn_shake256_finalize(&hctx);
        atn_shake256_squeeze(&hctx, ctilde, CTILDE);

        sample_in_ball(c, ctilde);
        memcpy(ch, c, sizeof(poly));
        ntt(ch);
        for (i = 0; i < LVEC; i++) {
            poly_pointwise(cs1[i], ch, s1h[i]);
            invntt(cs1[i]);
            poly_add(z[i], y[i], cs1[i]);
        }
        for (i = 0; i < K; i++) {
            poly_pointwise(cs2[i], ch, s2h[i]);
            invntt(cs2[i]);
            poly_sub(tmp[i], w[i], cs2[i]);
            poly_lowbits(r0[i], tmp[i]);
        }
        kappa += LVEC;
        if (poly_infnorm(z[0]) >= GAMMA1 - BETA) {
            continue;
        }
        for (i = 1; i < LVEC; i++) {
            if (poly_infnorm(z[i]) >= GAMMA1 - BETA) {
                goto next;
            }
        }
        for (i = 0; i < K; i++) {
            if (poly_infnorm(r0[i]) >= GAMMA2 - BETA) {
                goto next;
            }
        }
        for (i = 0; i < K; i++) {
            poly_pointwise(ct0[i], ch, t0h[i]);
            invntt(ct0[i]);
        }
        for (i = 0; i < K; i++) {
            unsigned j;
            poly_zero(h[i]);
            for (j = 0; j < N; j++) {
                int32_t mct = fqred(-(int64_t)ct0[i][j]);
                int32_t rhs = fqadd(fqsub(w[i][j], cs2[i][j]), ct0[i][j]);
                h[i][j] = makehint(mct, rhs);
            }
            if (poly_infnorm(ct0[i]) >= GAMMA2) {
                goto next;
            }
        }
        if (hint_ones(h) > OMEGA) {
            goto next;
        }
        ok = 1;
        break;
next:
        continue;
    }

    memcpy(sig, ctilde, CTILDE);
    for (i = 0; i < LVEC; i++) {
        unsigned j;
        for (j = 0; j < N; j++) {
            z[i][j] = center_q(z[i][j]);
        }
        bitpack(sig + CTILDE + i * 640, z[i], GAMMA1 - 1, GAMMA1);
    }
    hint_bitpack(sig + CTILDE + LVEC * 640, h);
    return ATN_OK;
}

int atn_mldsa87_verify_internal(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                                const uint8_t *mp, size_t mp_len,
                                const uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    uint8_t rho[32], tr[64], mu[64], ctilde[CTILDE], c2[CTILDE], w1b[K * 128];
    poly t1[K], z[LVEC], zh[LVEC], c, ch, a[K * LVEC];
    poly wapprox[K], w1p[K], h[K], t1h[K], ct1[K];
    unsigned i;
    atn_shake256_ctx hctx;

    if (pk == NULL || sig == NULL || (mp == NULL && mp_len != 0)) {
        return ATN_ERR_PARAM;
    }
    memcpy(rho, pk, 32);
    for (i = 0; i < K; i++) {
        simple_bitunpack(t1[i], pk + 32 + i * 320, (1u << 10) - 1u);
    }
    memcpy(ctilde, sig, CTILDE);
    for (i = 0; i < LVEC; i++) {
        bitunpack(z[i], sig + CTILDE + i * 640, GAMMA1 - 1, GAMMA1);
    }
    if (hint_bitunpack(h, sig + CTILDE + LVEC * 640) != 0) {
        return ATN_ERR_AUTH;
    }
    expand_a(a, rho);
    atn_shake256(pk, ATN_MLDSA87_PK_LEN, tr, 64);

    atn_shake256_init(&hctx);
    atn_shake256_absorb(&hctx, tr, 64);
    atn_shake256_absorb(&hctx, mp, mp_len);
    atn_shake256_finalize(&hctx);
    atn_shake256_squeeze(&hctx, mu, 64);

    sample_in_ball(c, ctilde);
    memcpy(ch, c, sizeof(poly));
    ntt(ch);
    for (i = 0; i < LVEC; i++) {
        memcpy(zh[i], z[i], sizeof(poly));
        ntt(zh[i]);
    }
    /* w'approx = Aẑ - ĉ ∘ NTT(t1 · 2^d) */
    matrix_vector_ntt(wapprox, a, zh);
    for (i = 0; i < K; i++) {
        unsigned j;
        for (j = 0; j < N; j++) {
            t1h[i][j] = t1[i][j] << D;
        }
        ntt(t1h[i]);
        poly_pointwise(ct1[i], ch, t1h[i]);
        poly_sub(wapprox[i], wapprox[i], ct1[i]);
        invntt(wapprox[i]);
        for (j = 0; j < N; j++) {
            w1p[i][j] = usehint((int)h[i][j], wapprox[i][j]);
        }
    }
    w1_encode(w1b, w1p);
    atn_shake256_init(&hctx);
    atn_shake256_absorb(&hctx, mu, 64);
    atn_shake256_absorb(&hctx, w1b, sizeof(w1b));
    atn_shake256_finalize(&hctx);
    atn_shake256_squeeze(&hctx, c2, CTILDE);

    for (i = 0; i < LVEC; i++) {
        if (poly_infnorm(z[i]) >= GAMMA1 - BETA) {
            return ATN_ERR_AUTH;
        }
    }
    if (!atn_ct_equal(ctilde, c2, CTILDE)) {
        return ATN_ERR_AUTH;
    }
    return ATN_OK;
}

static int encode_mprime(uint8_t *out, size_t *out_len, size_t cap,
                         const uint8_t *msg, size_t n,
                         const uint8_t *ctx, size_t ctx_len)
{
    if (ctx_len > 255) {
        return ATN_ERR_PARAM;
    }
    if (ctx == NULL && ctx_len != 0) {
        return ATN_ERR_PARAM;
    }
    if (1u + 1u + ctx_len + n > cap) {
        return ATN_ERR_LEN;
    }
    out[0] = 0;
    out[1] = (uint8_t)ctx_len;
    if (ctx_len) {
        memcpy(out + 2, ctx, ctx_len);
    }
    if (n) {
        memcpy(out + 2 + ctx_len, msg, n);
    }
    *out_len = 2u + ctx_len + n;
    return ATN_OK;
}

int atn_mldsa87_keygen(uint8_t pk[ATN_MLDSA87_PK_LEN],
                       uint8_t sk[ATN_MLDSA87_SK_LEN])
{
    uint8_t xi[32];
    int rc;
    if (atn_random_bytes(xi, 32) != ATN_OK) {
        return ATN_ERR_ENTROPY;
    }
    rc = atn_mldsa87_keygen_internal(xi, pk, sk);
    atn_memzero(xi, 32);
    return rc;
}

int atn_mldsa87_sign(const uint8_t sk[ATN_MLDSA87_SK_LEN],
                     const uint8_t *msg, size_t n,
                     const uint8_t *ctx, size_t ctx_len,
                     uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    uint8_t rnd[32];
    uint8_t mp[2 + 255 + 4096];
    size_t mp_len = 0;
    int rc;
    if (n > 4096) {
        return ATN_ERR_LEN;
    }
    rc = encode_mprime(mp, &mp_len, sizeof(mp), msg, n, ctx, ctx_len);
    if (rc != ATN_OK) {
        return rc;
    }
    if (atn_random_bytes(rnd, 32) != ATN_OK) {
        return ATN_ERR_ENTROPY;
    }
    rc = atn_mldsa87_sign_internal(sk, mp, mp_len, rnd, sig);
    atn_memzero(rnd, 32);
    return rc;
}

int atn_mldsa87_verify(const uint8_t pk[ATN_MLDSA87_PK_LEN],
                       const uint8_t *msg, size_t n,
                       const uint8_t *ctx, size_t ctx_len,
                       const uint8_t sig[ATN_MLDSA87_SIG_LEN])
{
    uint8_t mp[2 + 255 + 4096];
    size_t mp_len = 0;
    int rc;
    if (n > 4096) {
        return ATN_ERR_LEN;
    }
    rc = encode_mprime(mp, &mp_len, sizeof(mp), msg, n, ctx, ctx_len);
    if (rc != ATN_OK) {
        return rc;
    }
    return atn_mldsa87_verify_internal(pk, mp, mp_len, sig);
}
