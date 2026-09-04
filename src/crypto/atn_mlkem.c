/*
 * Module: atn_mlkem.c
 * REQ:    REQ-1.1-PQ (DEC-0005)
 * Spec:   FIPS 203 ML-KEM-1024 (NIST security category 5)
 *
 * Parameters (Table 2): n=256, q=3329, k=4, η1=2, η2=2, du=11, dv=5.
 * NTT zetas computed from FIPS 203 §4.3: ζ=17, BitRev7(i).
 * Hashes: H=SHA3-256, G=SHA3-512, J=SHAKE256, PRF=SHAKE256, XOF=SHAKE128.
 */

#include "atn_crypto.h"

#include <string.h>

#define Q      3329
#define N      256
#define K      4
#define ETA1   2
#define ETA2   2
#define DU     11
#define DV     5
#define POLYBYTES  384  /* ByteEncode12 */
#define EK_PKE     (POLYBYTES * K + 32)
#define DK_PKE     (POLYBYTES * K)

typedef int16_t poly[N];

/* FIPS 203 §4.3: ζ=17; zetas[i] = 17^{BitRev7(i)} mod q. Generated, not guessed. */
static const int16_t ZETAS[128] = {
    1, 1729, 2580, 3289, 2642, 630, 1897, 848,
    1062, 1919, 193, 797, 2786, 3260, 569, 1746,
    296, 2447, 1339, 1476, 3046, 56, 2240, 1333,
    1426, 2094, 535, 2882, 2393, 2879, 1974, 821,
    289, 331, 3253, 1756, 1197, 2304, 2277, 2055,
    650, 1977, 2513, 632, 2865, 33, 1320, 1915,
    2319, 1435, 807, 452, 1438, 2868, 1534, 2402,
    2647, 2617, 1481, 648, 2474, 3110, 1227, 910,
    17, 2761, 583, 2649, 1637, 723, 2288, 1100,
    1409, 2662, 3281, 233, 756, 2156, 3015, 3050,
    1703, 1651, 2789, 1789, 1847, 952, 1461, 2687,
    939, 2308, 2437, 2388, 733, 2337, 268, 641,
    1584, 2298, 2037, 3220, 375, 2549, 2090, 1645,
    1063, 319, 2773, 757, 2099, 561, 2466, 2594,
    2804, 1092, 403, 1026, 1143, 2150, 2775, 886,
    1722, 1212, 1874, 1029, 2110, 2935, 885, 2154
};

/* γ_i = 17^{2·BitRev7(i)+1} mod q  (Algorithm 11) */
static const int16_t GAMMAS[128] = {
    17, 3312, 2761, 568, 583, 2746, 2649, 680,
    1637, 1692, 723, 2606, 2288, 1041, 1100, 2229,
    1409, 1920, 2662, 667, 3281, 48, 233, 3096,
    756, 2573, 2156, 1173, 3015, 314, 3050, 279,
    1703, 1626, 1651, 1678, 2789, 540, 1789, 1540,
    1847, 1482, 952, 2377, 1461, 1868, 2687, 642,
    939, 2390, 2308, 1021, 2437, 892, 2388, 941,
    733, 2596, 2337, 992, 268, 3061, 641, 2688,
    1584, 1745, 2298, 1031, 2037, 1292, 3220, 109,
    375, 2954, 2549, 780, 2090, 1239, 1645, 1684,
    1063, 2266, 319, 3010, 2773, 556, 757, 2572,
    2099, 1230, 561, 2768, 2466, 863, 2594, 735,
    2804, 525, 1092, 2237, 403, 2926, 1026, 2303,
    1143, 2186, 2150, 1179, 2775, 554, 886, 2443,
    1722, 1607, 1212, 2117, 1874, 1455, 1029, 2300,
    2110, 1219, 2935, 394, 885, 2444, 2154, 1175
};

static int16_t fqred(int32_t a)
{
    a %= Q;
    if (a < 0) {
        a += Q;
    }
    return (int16_t)a;
}

static int16_t fqadd(int16_t a, int16_t b)
{
    return fqred((int32_t)a + b);
}

static int16_t fqsub(int16_t a, int16_t b)
{
    return fqred((int32_t)a - b);
}

static int16_t fqmul(int16_t a, int16_t b)
{
    return fqred((int32_t)a * b);
}

static void ntt(poly f)
{
    unsigned i = 1;
    unsigned len, start, j;
    for (len = 128; len >= 2; len /= 2) {
        for (start = 0; start < N; start += 2u * len) {
            int16_t zeta = ZETAS[i++];
            for (j = start; j < start + len; j++) {
                int16_t t = fqmul(zeta, f[j + len]);
                f[j + len] = fqsub(f[j], t);
                f[j] = fqadd(f[j], t);
            }
        }
    }
}

static void invntt(poly f)
{
    unsigned i = 127;
    unsigned len, start, j;
    for (len = 2; len <= 128; len *= 2) {
        for (start = 0; start < N; start += 2u * len) {
            int16_t zeta = ZETAS[i--];
            for (j = start; j < start + len; j++) {
                int16_t t = f[j];
                f[j] = fqadd(t, f[j + len]);
                f[j + len] = fqmul(zeta, fqsub(f[j + len], t));
            }
        }
    }
    for (j = 0; j < N; j++) {
        f[j] = fqmul(f[j], 3303); /* 128^{-1} mod q, Algorithm 10 step 14 */
    }
}

static void basemul(int16_t r[2], int16_t a0, int16_t a1, int16_t b0, int16_t b1, int16_t gamma)
{
    r[0] = fqadd(fqmul(a0, b0), fqmul(fqmul(a1, b1), gamma));
    r[1] = fqadd(fqmul(a0, b1), fqmul(a1, b0));
}

static void poly_mul_ntt(poly r, const poly a, const poly b)
{
    unsigned i;
    for (i = 0; i < 128; i++) {
        int16_t c[2];
        basemul(c, a[2 * i], a[2 * i + 1], b[2 * i], b[2 * i + 1], GAMMAS[i]);
        r[2 * i] = c[0];
        r[2 * i + 1] = c[1];
    }
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

/* Compress_d: round((2^d / q) * x) mod 2^d  — integer form, no float. */
static uint16_t compress(uint16_t x, unsigned d)
{
    uint32_t t = ((((uint32_t)x << d) + 1664u) / (uint32_t)Q);
    return (uint16_t)(t & ((1u << d) - 1u));
}

static uint16_t decompress(uint16_t y, unsigned d)
{
    return (uint16_t)(((uint32_t)Q * y + (1u << (d - 1u))) >> d);
}

static void byte_encode(uint8_t *out, const poly f, unsigned d)
{
    unsigned i, bit = 0;
    memset(out, 0, 32u * d);
    for (i = 0; i < N; i++) {
        uint32_t a = (uint32_t)f[i];
        unsigned j;
        for (j = 0; j < d; j++) {
            if (a & 1u) {
                out[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
            }
            a >>= 1;
            bit++;
        }
    }
}

static void byte_decode(poly f, const uint8_t *in, unsigned d)
{
    unsigned i, bit = 0;
    uint32_t m = (d == 12u) ? (uint32_t)Q : (1u << d);
    for (i = 0; i < N; i++) {
        uint32_t v = 0;
        unsigned j;
        for (j = 0; j < d; j++) {
            unsigned b = (in[bit / 8u] >> (bit % 8u)) & 1u;
            v |= (uint32_t)b << j;
            bit++;
        }
        f[i] = (int16_t)(v % m);
    }
}

static void sample_ntt(poly ahat, const uint8_t rho[32], uint8_t j, uint8_t i)
{
    atn_shake128_ctx xof;
    uint8_t seed[34];
    unsigned pos = 0;
    memcpy(seed, rho, 32);
    seed[32] = j;
    seed[33] = i;
    atn_shake128_init(&xof);
    atn_shake128_absorb(&xof, seed, 34);
    atn_shake128_finalize(&xof);
    while (pos < N) {
        uint8_t C[3];
        unsigned d1, d2;
        atn_shake128_squeeze(&xof, C, 3);
        d1 = (unsigned)C[0] + 256u * ((unsigned)C[1] & 15u);
        d2 = ((unsigned)C[1] >> 4) + 16u * (unsigned)C[2];
        if (d1 < (unsigned)Q) {
            ahat[pos++] = (int16_t)d1;
        }
        if (d2 < (unsigned)Q && pos < N) {
            ahat[pos++] = (int16_t)d2;
        }
    }
}

static void sample_cbd(poly f, const uint8_t *B, unsigned eta)
{
    unsigned i, j;
    for (i = 0; i < N; i++) {
        unsigned x = 0, y = 0;
        for (j = 0; j < eta; j++) {
            unsigned bitx = 2u * i * eta + j;
            unsigned bity = 2u * i * eta + eta + j;
            x += (B[bitx / 8u] >> (bitx % 8u)) & 1u;
            y += (B[bity / 8u] >> (bity % 8u)) & 1u;
        }
        f[i] = fqred((int32_t)x - (int32_t)y);
    }
}

static void prf_cbd(poly f, const uint8_t s[32], uint8_t n, unsigned eta)
{
    uint8_t inp[33];
    uint8_t out[64 * 3]; /* eta is 2 or 3 → 128 or 192 bytes */
    memcpy(inp, s, 32);
    inp[32] = n;
    atn_shake256(inp, 33, out, 64u * eta);
    sample_cbd(f, out, eta);
    atn_memzero(inp, sizeof(inp));
    atn_memzero(out, sizeof(out));
}

static void kpke_keygen(const uint8_t d[32], uint8_t ek[EK_PKE], uint8_t dk[DK_PKE])
{
    uint8_t g_in[33], g_out[64];
    uint8_t rho[32], sigma[32];
    poly A[K][K], s[K], e[K], shat[K], ehat[K], that[K];
    unsigned i, j;
    uint8_t Nctr = 0;

    memcpy(g_in, d, 32);
    g_in[32] = (uint8_t)K; /* FIPS 203 Alg 13: G(d ‖ k) */
    atn_sha3_512(g_in, 33, g_out);
    memcpy(rho, g_out, 32);
    memcpy(sigma, g_out + 32, 32);

    for (i = 0; i < K; i++) {
        for (j = 0; j < K; j++) {
            sample_ntt(A[i][j], rho, (uint8_t)j, (uint8_t)i);
        }
    }
    for (i = 0; i < K; i++) {
        prf_cbd(s[i], sigma, Nctr++, ETA1);
    }
    for (i = 0; i < K; i++) {
        prf_cbd(e[i], sigma, Nctr++, ETA1);
    }
    for (i = 0; i < K; i++) {
        memcpy(shat[i], s[i], sizeof(poly));
        ntt(shat[i]);
        memcpy(ehat[i], e[i], sizeof(poly));
        ntt(ehat[i]);
    }
    for (i = 0; i < K; i++) {
        poly acc;
        memset(acc, 0, sizeof(acc));
        for (j = 0; j < K; j++) {
            poly t;
            poly_mul_ntt(t, A[i][j], shat[j]);
            poly_add(acc, acc, t);
        }
        poly_add(that[i], acc, ehat[i]);
    }
    for (i = 0; i < K; i++) {
        byte_encode(ek + POLYBYTES * i, that[i], 12);
        byte_encode(dk + POLYBYTES * i, shat[i], 12);
    }
    memcpy(ek + POLYBYTES * K, rho, 32);

    atn_memzero(g_in, sizeof(g_in));
    atn_memzero(g_out, sizeof(g_out));
    atn_memzero(sigma, sizeof(sigma));
    atn_memzero(s, sizeof(s));
    atn_memzero(e, sizeof(e));
    atn_memzero(shat, sizeof(shat));
    atn_memzero(ehat, sizeof(ehat));
}

static void kpke_encrypt(const uint8_t ek[EK_PKE], const uint8_t m[32],
                         const uint8_t r[32], uint8_t c[ATN_MLKEM1024_CT_LEN])
{
    poly that[K], A[K][K], y[K], yhat[K], e1[K], e2, mu, u[K], v;
    uint8_t rho[32];
    unsigned i, j;
    uint8_t Nctr = 0;
    const unsigned c1len = 32u * DU * K;

    for (i = 0; i < K; i++) {
        byte_decode(that[i], ek + POLYBYTES * i, 12);
    }
    memcpy(rho, ek + POLYBYTES * K, 32);
    for (i = 0; i < K; i++) {
        for (j = 0; j < K; j++) {
            sample_ntt(A[i][j], rho, (uint8_t)j, (uint8_t)i);
        }
    }
    for (i = 0; i < K; i++) {
        prf_cbd(y[i], r, Nctr++, ETA1);
    }
    for (i = 0; i < K; i++) {
        prf_cbd(e1[i], r, Nctr++, ETA2);
    }
    prf_cbd(e2, r, Nctr, ETA2);

    for (i = 0; i < K; i++) {
        memcpy(yhat[i], y[i], sizeof(poly));
        ntt(yhat[i]);
    }

    /* u = NTT^{-1}(A^T ∘ ŷ) + e1 */
    for (i = 0; i < K; i++) {
        poly acc;
        memset(acc, 0, sizeof(acc));
        for (j = 0; j < K; j++) {
            poly t;
            poly_mul_ntt(t, A[j][i], yhat[j]); /* A^T [i,j] = A[j][i] */
            poly_add(acc, acc, t);
        }
        invntt(acc);
        poly_add(u[i], acc, e1[i]);
    }

    /* μ = Decompress_1(ByteDecode_1(m)) */
    {
        poly mb;
        byte_decode(mb, m, 1);
        for (i = 0; i < N; i++) {
            mu[i] = (int16_t)decompress((uint16_t)mb[i], 1);
        }
    }

    /* v = NTT^{-1}(t̂^T ∘ ŷ) + e2 + μ */
    {
        poly acc;
        memset(acc, 0, sizeof(acc));
        for (j = 0; j < K; j++) {
            poly t;
            poly_mul_ntt(t, that[j], yhat[j]);
            poly_add(acc, acc, t);
        }
        invntt(acc);
        poly_add(v, acc, e2);
        poly_add(v, v, mu);
    }

    for (i = 0; i < K; i++) {
        poly uc;
        unsigned t;
        for (t = 0; t < N; t++) {
            uc[t] = (int16_t)compress((uint16_t)u[i][t], DU);
        }
        byte_encode(c + (32u * DU) * i, uc, DU);
    }
    {
        poly vc;
        unsigned t;
        for (t = 0; t < N; t++) {
            vc[t] = (int16_t)compress((uint16_t)v[t], DV);
        }
        byte_encode(c + c1len, vc, DV);
    }

    atn_memzero(y, sizeof(y));
    atn_memzero(yhat, sizeof(yhat));
    atn_memzero(e1, sizeof(e1));
    atn_memzero(e2, sizeof(e2));
}

static void kpke_decrypt(const uint8_t dk[DK_PKE], const uint8_t c[ATN_MLKEM1024_CT_LEN],
                         uint8_t m[32])
{
    const unsigned c1len = 32u * DU * K;
    poly u[K], v, shat[K], w;
    unsigned i, t;

    for (i = 0; i < K; i++) {
        poly uc;
        byte_decode(uc, c + (32u * DU) * i, DU);
        for (t = 0; t < N; t++) {
            u[i][t] = (int16_t)decompress((uint16_t)uc[t], DU);
        }
    }
    {
        poly vc;
        byte_decode(vc, c + c1len, DV);
        for (t = 0; t < N; t++) {
            v[t] = (int16_t)decompress((uint16_t)vc[t], DV);
        }
    }
    for (i = 0; i < K; i++) {
        byte_decode(shat[i], dk + POLYBYTES * i, 12);
    }

    {
        poly uhat[K], acc;
        for (i = 0; i < K; i++) {
            memcpy(uhat[i], u[i], sizeof(poly));
            ntt(uhat[i]);
        }
        memset(acc, 0, sizeof(acc));
        for (i = 0; i < K; i++) {
            poly p;
            poly_mul_ntt(p, shat[i], uhat[i]);
            poly_add(acc, acc, p);
        }
        invntt(acc);
        poly_sub(w, v, acc);
    }
    {
        poly wc;
        for (t = 0; t < N; t++) {
            wc[t] = (int16_t)compress((uint16_t)w[t], 1);
        }
        byte_encode(m, wc, 1);
    }
}

int atn_mlkem1024_keygen_internal(const uint8_t d[32], const uint8_t z[32],
                                  uint8_t ek[ATN_MLKEM1024_EK_LEN],
                                  uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    uint8_t dk_pke[DK_PKE];
    uint8_t h[32];
    if (d == NULL || z == NULL || ek == NULL || dk == NULL) {
        return ATN_ERR_PARAM;
    }
    kpke_keygen(d, ek, dk_pke);
    atn_sha3_256(ek, ATN_MLKEM1024_EK_LEN, h);
    memcpy(dk, dk_pke, DK_PKE);
    memcpy(dk + DK_PKE, ek, ATN_MLKEM1024_EK_LEN);
    memcpy(dk + DK_PKE + ATN_MLKEM1024_EK_LEN, h, 32);
    memcpy(dk + DK_PKE + ATN_MLKEM1024_EK_LEN + 32, z, 32);
    atn_memzero(dk_pke, sizeof(dk_pke));
    return ATN_OK;
}

int atn_mlkem1024_encaps_internal(const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                                  const uint8_t m[32],
                                  uint8_t ss[ATN_MLKEM1024_SS_LEN],
                                  uint8_t ct[ATN_MLKEM1024_CT_LEN])
{
    uint8_t hek[32], gin[64], gout[64];
    if (ek == NULL || m == NULL || ss == NULL || ct == NULL) {
        return ATN_ERR_PARAM;
    }
    atn_sha3_256(ek, ATN_MLKEM1024_EK_LEN, hek);
    memcpy(gin, m, 32);
    memcpy(gin + 32, hek, 32);
    atn_sha3_512(gin, 64, gout); /* (K, r) ← G(m ‖ H(ek)) */
    memcpy(ss, gout, 32);
    kpke_encrypt(ek, m, gout + 32, ct);
    atn_memzero(gin, sizeof(gin));
    atn_memzero(gout, sizeof(gout));
    return ATN_OK;
}

int atn_mlkem1024_decaps(const uint8_t dk[ATN_MLKEM1024_DK_LEN],
                         const uint8_t ct[ATN_MLKEM1024_CT_LEN],
                         uint8_t ss[ATN_MLKEM1024_SS_LEN])
{
    const uint8_t *dk_pke = dk;
    const uint8_t *ek = dk + DK_PKE;
    const uint8_t *h = dk + DK_PKE + ATN_MLKEM1024_EK_LEN;
    const uint8_t *z = dk + DK_PKE + ATN_MLKEM1024_EK_LEN + 32;
    uint8_t mp[32], gin[64], gout[64], ct2[ATN_MLKEM1024_CT_LEN], kbar[32];
    uint8_t j_in[32 + ATN_MLKEM1024_CT_LEN];
    int match;
    unsigned i;

    if (dk == NULL || ct == NULL || ss == NULL) {
        return ATN_ERR_PARAM;
    }

    kpke_decrypt(dk_pke, ct, mp);
    memcpy(gin, mp, 32);
    memcpy(gin + 32, h, 32);
    atn_sha3_512(gin, 64, gout); /* (K', r') */
    memcpy(j_in, z, 32);
    memcpy(j_in + 32, ct, ATN_MLKEM1024_CT_LEN);
    atn_shake256(j_in, sizeof(j_in), kbar, 32); /* K̄ ← J(z ‖ c) */
    kpke_encrypt(ek, mp, gout + 32, ct2);
    match = atn_ct_equal(ct, ct2, ATN_MLKEM1024_CT_LEN);
    /* match=1 → keep K'; match=0 → implicit reject K̄. Constant-time select. */
    {
        uint8_t mask = (uint8_t)(0u - (unsigned)(match == 0));
        for (i = 0; i < 32; i++) {
            ss[i] = (uint8_t)((gout[i] & (uint8_t)~mask) | (kbar[i] & mask));
        }
    }
    atn_memzero(mp, sizeof(mp));
    atn_memzero(gin, sizeof(gin));
    atn_memzero(gout, sizeof(gout));
    atn_memzero(ct2, sizeof(ct2));
    atn_memzero(kbar, sizeof(kbar));
    atn_memzero(j_in, sizeof(j_in));
    return ATN_OK;
}

int atn_mlkem1024_keygen(uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    uint8_t d[32], z[32];
    int rc;
    if (atn_random_bytes(d, 32) != ATN_OK || atn_random_bytes(z, 32) != ATN_OK) {
        atn_memzero(d, 32);
        atn_memzero(z, 32);
        return ATN_ERR_ENTROPY;
    }
    rc = atn_mlkem1024_keygen_internal(d, z, ek, dk);
    atn_memzero(d, 32);
    atn_memzero(z, 32);
    return rc;
}

int atn_mlkem1024_encaps(const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         uint8_t ss[ATN_MLKEM1024_SS_LEN],
                         uint8_t ct[ATN_MLKEM1024_CT_LEN])
{
    uint8_t m[32];
    int rc;
    if (atn_random_bytes(m, 32) != ATN_OK) {
        return ATN_ERR_ENTROPY;
    }
    rc = atn_mlkem1024_encaps_internal(ek, m, ss, ct);
    atn_memzero(m, 32);
    return rc;
}
