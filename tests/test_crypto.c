/*
 * REQ-1.1 known-answer driver.
 * Vectors transcribed from the documents named in docs/SPEC_INDEX.md.
 * A FAIL line is a SoT gate failure. Do not "fix" a vector to match our output.
 */

#include "atn_crypto.h"
#include "kat_mlkem1024.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("ok   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fail++;
    }
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v = 0;
        /* sscanf return is required; a truncated vector is a test bug. */
        if (sscanf(hex + 2u * i, "%2x", &v) != 1) {
            out[i] = 0;
            g_fail++;
            return;
        }
        out[i] = (uint8_t)v;
    }
}

/* ----- SHA-256: FIPS 180-4 / RFC 6234 classic fixtures (ISS-0002) ----- */

static void test_sha256(void)
{
    uint8_t d[32];
    static const uint8_t empty[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
    };
    static const uint8_t abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const uint8_t two_block[32] = {
        0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8,0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
        0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67,0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1
    };
    const char *tb = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    check("sha256 empty rc", atn_sha256("", 0, d) == ATN_OK);
    check("sha256 empty", bytes_eq(d, empty, 32));
    check("sha256 abc rc", atn_sha256("abc", 3, d) == ATN_OK);
    check("sha256 abc", bytes_eq(d, abc, 32));
    check("sha256 two-block rc", atn_sha256(tb, strlen(tb), d) == ATN_OK);
    check("sha256 two-block", bytes_eq(d, two_block, 32));
}

/* ----- HMAC-SHA-256: RFC 4231 §§4.2–4.8 ----- */

static void test_hmac(void)
{
    uint8_t out[32];
    uint8_t key[131];
    uint8_t data[152];
    uint8_t expect[32];

    /* §4.2 Test Case 1 */
    memset(key, 0x0b, 20);
    hex_to_bytes("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", expect, 32);
    check("hmac tc1 rc", atn_hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, out) == ATN_OK);
    check("hmac tc1", bytes_eq(out, expect, 32));

    /* §4.3 Test Case 2 */
    hex_to_bytes("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", expect, 32);
    check("hmac tc2 rc", atn_hmac_sha256((const uint8_t *)"Jefe", 4,
        (const uint8_t *)"what do ya want for nothing?", 28, out) == ATN_OK);
    check("hmac tc2", bytes_eq(out, expect, 32));

    /* §4.4 Test Case 3 */
    memset(key, 0xaa, 20);
    memset(data, 0xdd, 50);
    hex_to_bytes("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", expect, 32);
    check("hmac tc3 rc", atn_hmac_sha256(key, 20, data, 50, out) == ATN_OK);
    check("hmac tc3", bytes_eq(out, expect, 32));

    /* §4.5 Test Case 4 */
    {
        unsigned i;
        for (i = 0; i < 25; i++) {
            key[i] = (uint8_t)(i + 1);
        }
    }
    memset(data, 0xcd, 50);
    hex_to_bytes("82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b", expect, 32);
    check("hmac tc4 rc", atn_hmac_sha256(key, 25, data, 50, out) == ATN_OK);
    check("hmac tc4", bytes_eq(out, expect, 32));

    /* §4.6 Test Case 5 truncated 128-bit */
    memset(key, 0x0c, 20);
    hex_to_bytes("a3b6167473100ee06e0c796c2955552b", expect, 16);
    check("hmac tc5 rc", atn_hmac_sha256(key, 20,
        (const uint8_t *)"Test With Truncation", 20, out) == ATN_OK);
    check("hmac tc5 trunc128", bytes_eq(out, expect, 16));

    /* §4.7 Test Case 6 — 131-byte key */
    memset(key, 0xaa, 131);
    hex_to_bytes("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", expect, 32);
    check("hmac tc6 rc", atn_hmac_sha256(key, 131,
        (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54, out) == ATN_OK);
    check("hmac tc6", bytes_eq(out, expect, 32));

    /* §4.8 Test Case 7 */
    hex_to_bytes("9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2", expect, 32);
    check("hmac tc7 rc", atn_hmac_sha256(key, 131,
        (const uint8_t *)"This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", 152, out) == ATN_OK);
    check("hmac tc7", bytes_eq(out, expect, 32));
}

/* ----- HKDF-SHA-256: RFC 5869 Appendix A.1–A.3 ----- */

static void test_hkdf(void)
{
    uint8_t ikm[80], salt[80], info[80], prk[32], okm[82], expect_prk[32], expect_okm[82];

    /* A.1 */
    memset(ikm, 0x0b, 22);
    hex_to_bytes("000102030405060708090a0b0c", salt, 13);
    hex_to_bytes("f0f1f2f3f4f5f6f7f8f9", info, 10);
    hex_to_bytes("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", expect_prk, 32);
    hex_to_bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", expect_okm, 42);
    check("hkdf a1 extract", atn_hkdf_extract(salt, 13, ikm, 22, prk) == ATN_OK && bytes_eq(prk, expect_prk, 32));
    check("hkdf a1 expand", atn_hkdf_expand(prk, 32, info, 10, okm, 42) == ATN_OK && bytes_eq(okm, expect_okm, 42));

    /* A.2 */
    hex_to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f", ikm, 80);
    hex_to_bytes("606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf", salt, 80);
    hex_to_bytes("b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", info, 80);
    hex_to_bytes("06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244", expect_prk, 32);
    hex_to_bytes("b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87", expect_okm, 82);
    check("hkdf a2 extract", atn_hkdf_extract(salt, 80, ikm, 80, prk) == ATN_OK && bytes_eq(prk, expect_prk, 32));
    check("hkdf a2 expand", atn_hkdf_expand(prk, 32, info, 80, okm, 82) == ATN_OK && bytes_eq(okm, expect_okm, 82));

    /* A.3 zero-length salt/info */
    memset(ikm, 0x0b, 22);
    hex_to_bytes("19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04", expect_prk, 32);
    hex_to_bytes("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8", expect_okm, 42);
    check("hkdf a3 extract", atn_hkdf_extract(NULL, 0, ikm, 22, prk) == ATN_OK && bytes_eq(prk, expect_prk, 32));
    check("hkdf a3 expand", atn_hkdf_expand(prk, 32, NULL, 0, okm, 42) == ATN_OK && bytes_eq(okm, expect_okm, 42));
}

/* ----- ChaCha20: RFC 8439 §2.3.2 block and §2.4.2 sunscreen ----- */

static void test_chacha20(void)
{
    uint8_t key[32], nonce[12], block[64], expect[128], ct[114];
    unsigned i;
    const char *pt =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";

    for (i = 0; i < 32; i++) {
        key[i] = (uint8_t)i;
    }
    hex_to_bytes("000000090000004a00000000", nonce, 12);
    hex_to_bytes(
        "10f1e7e4d13b5915500fdd1fa32071c4"
        "c7d1f4c733c068030422aa9ac3d46c4e"
        "d2826446079faa0914c2d705d98b02a2"
        "b5129cd1de164eb9cbd083e8a2503c4e", expect, 64);
    atn_chacha20_block(key, 1, nonce, block);
    check("chacha20 block §2.3.2", bytes_eq(block, expect, 64));

    hex_to_bytes("000000000000004a00000000", nonce, 12);
    hex_to_bytes(
        "6e2e359a2568f98041ba0728dd0d6981"
        "e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b357"
        "1639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e"
        "52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42"
        "874d", expect, 114);
    check("chacha20 xor rc", atn_chacha20_xor(key, 1, nonce, (const uint8_t *)pt, ct, 114) == ATN_OK);
    check("chacha20 sunscreen §2.4.2", bytes_eq(ct, expect, 114));
}

/* ----- Poly1305: RFC 8439 §2.5.2 ----- */

static void test_poly1305(void)
{
    uint8_t key[32], tag[16], expect[16];
    const char *msg = "Cryptographic Forum Research Group";

    hex_to_bytes("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", key, 32);
    hex_to_bytes("a8061dc1305136c6c22b8baf0c0127a9", expect, 16);
    check("poly1305 rc", atn_poly1305(key, (const uint8_t *)msg, 34, tag) == ATN_OK);
    check("poly1305 §2.5.2", bytes_eq(tag, expect, 16));
}

/* ----- AEAD: RFC 8439 §2.8.2 ----- */

static void test_aead(void)
{
    uint8_t key[32], nonce[12], aad[12], tag[16], expect_tag[16], ct[114], pt[114], round[114];
    const char *plain =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";

    hex_to_bytes("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key, 32);
    hex_to_bytes("070000004041424344454647", nonce, 12);
    hex_to_bytes("50515253c0c1c2c3c4c5c6c7", aad, 12);
    hex_to_bytes(
        "d31a8d34648e60db7b86afbc53ef7ec2"
        "a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b"
        "1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58"
        "fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b"
        "6116", ct, 114);
    hex_to_bytes("1ae10b594f09e26a7e902ecbd0600691", expect_tag, 16);

    check("aead enc rc", atn_aead_encrypt(key, nonce, aad, 12,
        (const uint8_t *)plain, 114, round, tag) == ATN_OK);
    check("aead enc ct", bytes_eq(round, ct, 114));
    check("aead enc tag", bytes_eq(tag, expect_tag, 16));

    memset(pt, 0xaa, sizeof(pt));
    check("aead dec rc", atn_aead_decrypt(key, nonce, aad, 12, ct, 114, expect_tag, pt) == ATN_OK);
    check("aead dec pt", bytes_eq(pt, (const uint8_t *)plain, 114));

    tag[0] ^= 1;
    memset(pt, 0x11, sizeof(pt));
    check("aead bad tag rc", atn_aead_decrypt(key, nonce, aad, 12, ct, 114, tag, pt) == ATN_ERR_AUTH);
    {
        size_t i;
        int wiped = 1;
        for (i = 0; i < 114; i++) {
            if (pt[i] != 0) {
                wiped = 0;
            }
        }
        check("aead bad tag wipes pt", wiped);
    }
}

/* ----- helpers / nonce / random ----- */

static void test_helpers(void)
{
    uint8_t a[16], b[16], r[32];
    atn_nonce_state st;
    uint8_t n1[12], n2[12], n3[12];

    memset(a, 0x5a, 16);
    memcpy(b, a, 16);
    check("ct_equal yes", atn_ct_equal(a, b, 16) == 1);
    b[15] ^= 1;
    check("ct_equal no", atn_ct_equal(a, b, 16) == 0);

    atn_memzero(a, 16);
    {
        int z = 1;
        size_t i;
        for (i = 0; i < 16; i++) {
            if (a[i] != 0) {
                z = 0;
            }
        }
        check("memzero", z);
    }

    check("random rc", atn_random_bytes(r, 32) == ATN_OK);

    memset(&st, 0, sizeof(st));
    check("nonce next 0", atn_nonce_next(&st, 7, n1) == ATN_OK);
    check("nonce next 1", atn_nonce_next(&st, 7, n2) == ATN_OK);
    check("nonce distinct", atn_ct_equal(n1, n2, 12) == 0);
    check("nonce wrong sender", atn_nonce_next(&st, 8, n3) == ATN_ERR_NONCE);

    memset(&st, 0, sizeof(st));
    check("nonce accept first", atn_nonce_accept(&st, n1) == ATN_OK);
    check("nonce reject replay", atn_nonce_accept(&st, n1) == ATN_ERR_NONCE);
    check("nonce accept later", atn_nonce_accept(&st, n2) == ATN_OK);

    /* Cause/effect REQ-1.1 gate: random-payload roundtrip, not only RFC fixtures. */
    {
        uint8_t key[32], nonce[12], aad[19], pt[200], ct[200], back[200], tag[16];
        int i, ok = 1;
        check("random key", atn_random_bytes(key, 32) == ATN_OK);
        check("random pt", atn_random_bytes(pt, 200) == ATN_OK);
        check("random aad", atn_random_bytes(aad, 19) == ATN_OK);
        memset(&st, 0, sizeof(st));
        for (i = 0; i < 8; i++) {
            if (atn_nonce_next(&st, 1, nonce) != ATN_OK) {
                ok = 0;
                break;
            }
            if (atn_aead_encrypt(key, nonce, aad, 19, pt, 200, ct, tag) != ATN_OK) {
                ok = 0;
                break;
            }
            memset(back, 0x3c, sizeof(back));
            if (atn_aead_decrypt(key, nonce, aad, 19, ct, 200, tag, back) != ATN_OK
                || !bytes_eq(back, pt, 200)) {
                ok = 0;
                break;
            }
            tag[i] ^= 1;
            if (atn_aead_decrypt(key, nonce, aad, 19, ct, 200, tag, back) != ATN_ERR_AUTH) {
                ok = 0;
                break;
            }
        }
        check("aead random roundtrip x8", ok);
    }
}

static void test_sha3_shake(void)
{
    uint8_t d[64];
    static const uint8_t sha3_256_empty[32] = {
        0xa7,0xff,0xc6,0xf8,0xbf,0x1e,0xd7,0x66,0x51,0xc1,0x47,0x56,0xa0,0x61,0xd6,0x62,
        0xf5,0x80,0xff,0x4d,0xe4,0x3b,0x49,0xfa,0x82,0xd8,0x0a,0x4b,0x80,0xf8,0x43,0x4a
    };
    static const uint8_t sha3_512_empty[64] = {
        0xa6,0x9f,0x73,0xcc,0xa2,0x3a,0x9a,0xc5,0xc8,0xb5,0x67,0xdc,0x18,0x5a,0x75,0x6e,
        0x97,0xc9,0x82,0x16,0x4f,0xe2,0x58,0x59,0xe0,0xd1,0xdc,0xc1,0x47,0x5c,0x80,0xa6,
        0x15,0xb2,0x12,0x3a,0xf1,0xf5,0xf9,0x4c,0x11,0xe3,0xe9,0x40,0x2c,0x3a,0xc5,0x58,
        0xf5,0x00,0x19,0x9d,0x95,0xb6,0xd3,0xe3,0x01,0x75,0x85,0x86,0x28,0x1d,0xcd,0x26
    };
    static const uint8_t shake256_empty_32[32] = {
        0x46,0xb9,0xdd,0x2b,0x0b,0xa8,0x8d,0x13,0x23,0x3b,0x3f,0xeb,0x74,0x3e,0xeb,0x24,
        0x3f,0xcd,0x52,0xea,0x62,0xb8,0x1b,0x82,0xb5,0x0c,0x27,0x64,0x6e,0xd5,0x76,0x2f
    };
    static const uint8_t shake128_empty_32[32] = {
        0x7f,0x9c,0x2b,0xa4,0xe8,0x8f,0x82,0x7d,0x61,0x60,0x45,0x50,0x76,0x05,0x85,0x3e,
        0xd7,0x3b,0x80,0x93,0xf6,0xef,0xbc,0x88,0xeb,0x1a,0x6e,0xac,0xfa,0x66,0xef,0x26
    };
    check("sha3-256 empty", atn_sha3_256("", 0, d) == ATN_OK && bytes_eq(d, sha3_256_empty, 32));
    check("sha3-512 empty", atn_sha3_512("", 0, d) == ATN_OK && bytes_eq(d, sha3_512_empty, 64));
    check("shake256 empty 32", atn_shake256("", 0, d, 32) == ATN_OK && bytes_eq(d, shake256_empty_32, 32));
    check("shake128 empty 32", atn_shake128("", 0, d, 32) == ATN_OK && bytes_eq(d, shake128_empty_32, 32));
    /* Incremental SHAKE256 must match one-shot when input spans >1 rate (136). */
    {
        uint8_t longin[200], one[32], inc[32];
        atn_shake256_ctx ctx;
        memset(longin, 0xa5, sizeof(longin));
        check("shake256 long one-shot", atn_shake256(longin, sizeof(longin), one, 32) == ATN_OK);
        atn_shake256_init(&ctx);
        atn_shake256_absorb(&ctx, longin, 64);
        atn_shake256_absorb(&ctx, longin + 64, sizeof(longin) - 64);
        atn_shake256_finalize(&ctx);
        atn_shake256_squeeze(&ctx, inc, 32);
        check("shake256 long incremental", bytes_eq(one, inc, 32));
    }
}

static void test_sha512(void)
{
    uint8_t d[64];
    static const uint8_t empty[64] = {
        0xcf,0x83,0xe1,0x35,0x7e,0xef,0xb8,0xbd,0xf1,0x54,0x28,0x50,0xd6,0x6d,0x80,0x07,
        0xd6,0x20,0xe4,0x05,0x0b,0x57,0x15,0xdc,0x83,0xf4,0xa9,0x21,0xd3,0x6c,0xe9,0xce,
        0x47,0xd0,0xd1,0x3c,0x5d,0x85,0xf2,0xb0,0xff,0x83,0x18,0xd2,0x87,0x7e,0xec,0x2f,
        0x63,0xb9,0x31,0xbd,0x47,0x41,0x7a,0x81,0xa5,0x38,0x32,0x7a,0xf9,0x27,0xda,0x3e
    };
    static const uint8_t abc[64] = {
        0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
        0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
        0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
        0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f
    };
    uint8_t key[20], expect[64], out[64];
    check("sha512 empty", atn_sha512("", 0, d) == ATN_OK && bytes_eq(d, empty, 64));
    check("sha512 abc", atn_sha512("abc", 3, d) == ATN_OK && bytes_eq(d, abc, 64));
    memset(key, 0x0b, 20);
    hex_to_bytes("87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
                 "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854", expect, 64);
    check("hmac-sha512 tc1", atn_hmac_sha512(key, 20, (const uint8_t *)"Hi There", 8, out) == ATN_OK
          && bytes_eq(out, expect, 64));
}

static void test_mlkem(void)
{
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t ss[ATN_MLKEM1024_SS_LEN], ct[ATN_MLKEM1024_CT_LEN], ss2[ATN_MLKEM1024_SS_LEN];

    check("mlkem keygen_internal",
          atn_mlkem1024_keygen_internal(kat_d, kat_z, ek, dk) == ATN_OK);
    check("mlkem ek KAT", bytes_eq(ek, kat_pk, ATN_MLKEM1024_EK_LEN));
    check("mlkem dk KAT", bytes_eq(dk, kat_sk, ATN_MLKEM1024_DK_LEN));
    check("mlkem encaps_internal",
          atn_mlkem1024_encaps_internal(ek, kat_m, ss, ct) == ATN_OK);
    check("mlkem ct KAT", bytes_eq(ct, kat_ct, ATN_MLKEM1024_CT_LEN));
    check("mlkem ss KAT", bytes_eq(ss, kat_ss, ATN_MLKEM1024_SS_LEN));
    check("mlkem decaps KAT",
          atn_mlkem1024_decaps(dk, ct, ss2) == ATN_OK && bytes_eq(ss2, kat_ss, 32));

    check("mlkem random keygen", atn_mlkem1024_keygen(ek, dk) == ATN_OK);
    check("mlkem random encaps", atn_mlkem1024_encaps(ek, ss, ct) == ATN_OK);
    check("mlkem random decaps", atn_mlkem1024_decaps(dk, ct, ss2) == ATN_OK
          && bytes_eq(ss, ss2, 32));
    ct[0] ^= 1;
    check("mlkem implicit reject",
          atn_mlkem1024_decaps(dk, ct, ss2) == ATN_OK && !bytes_eq(ss, ss2, 32));
}

int main(void)
{
    printf("athanor crypto KATs  platform=%s  sizeof(void*)=%u\n",
           atn_platform_id(), (unsigned)sizeof(void *));
    test_sha256();
    test_hmac();
    test_hkdf();
    test_chacha20();
    test_poly1305();
    test_aead();
    test_sha3_shake();
    test_sha512();
    test_mlkem();
    test_helpers();
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
