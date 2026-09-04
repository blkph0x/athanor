/*
 * Module: atn_chacha20.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §§2.1–2.4 (IETF ChaCha20: 32-bit counter, 96-bit nonce)
 *
 * State words are 32-bit little-endian. "+" is mod 2^32. "<<<" is rotate left.
 * 20 rounds = 10 column+diagonal double-rounds (§2.3).
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

static uint32_t rotl32(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32u - n));
}

/* RFC 8439 §2.1 quarter round on four words of the state. */
static void quarterround(uint32_t s[16], unsigned a, unsigned b, unsigned c, unsigned d)
{
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl32(s[d], 16);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl32(s[b], 12);
    s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl32(s[d], 8);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl32(s[b], 7);
}

/* RFC 8439 §2.3.1 inner_block */
static void inner_block(uint32_t s[16])
{
    quarterround(s, 0, 4, 8, 12);
    quarterround(s, 1, 5, 9, 13);
    quarterround(s, 2, 6, 10, 14);
    quarterround(s, 3, 7, 11, 15);
    quarterround(s, 0, 5, 10, 15);
    quarterround(s, 1, 6, 11, 12);
    quarterround(s, 2, 7, 8, 13);
    quarterround(s, 3, 4, 9, 14);
}

void atn_chacha20_block(const uint8_t key[ATN_CHACHA20_KEY_LEN],
                        uint32_t counter,
                        const uint8_t nonce[ATN_CHACHA20_NONCE_LEN],
                        uint8_t out[ATN_CHACHA20_BLOCK])
{
    uint32_t s[16];
    uint32_t init[16];
    unsigned i;

    /* RFC 8439 §2.3: words 0-3 are the ASCII constants "expand 32-byte k". */
    s[0] = 0x61707865u;
    s[1] = 0x3320646eu;
    s[2] = 0x79622d32u;
    s[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) {
        s[4 + i] = load_le32(key + 4u * i);
    }
    s[12] = counter;
    s[13] = load_le32(nonce + 0);
    s[14] = load_le32(nonce + 4);
    s[15] = load_le32(nonce + 8);

    memcpy(init, s, sizeof(init));
    for (i = 0; i < 10; i++) {
        inner_block(s);
    }
    for (i = 0; i < 16; i++) {
        s[i] += init[i];
        store_le32(out + 4u * i, s[i]);
    }
}

int atn_chacha20_xor(const uint8_t key[ATN_CHACHA20_KEY_LEN],
                     uint32_t counter,
                     const uint8_t nonce[ATN_CHACHA20_NONCE_LEN],
                     const uint8_t *in, uint8_t *out, size_t n)
{
    uint8_t block[ATN_CHACHA20_BLOCK];
    size_t off = 0;

    if (n == 0) {
        return ATN_OK;
    }
    if (key == NULL || nonce == NULL || in == NULL || out == NULL) {
        return ATN_ERR_PARAM;
    }

    while (off < n) {
        size_t take;
        size_t j;
        /* RFC 8439 §2.4: successive blocks increment the counter. 32-bit,
         * so a single invocation is limited to 2^32 blocks (~256 GiB). */
        if (counter == 0xffffffffu && off >= ATN_CHACHA20_BLOCK) {
            atn_memzero(block, sizeof(block));
            return ATN_ERR_LEN;
        }
        atn_chacha20_block(key, counter, nonce, block);
        take = n - off;
        if (take > ATN_CHACHA20_BLOCK) {
            take = ATN_CHACHA20_BLOCK;
        }
        for (j = 0; j < take; j++) {
            out[off + j] = (uint8_t)(in[off + j] ^ block[j]);
        }
        off += take;
        if (off < n) {
            if (counter == 0xffffffffu) {
                atn_memzero(block, sizeof(block));
                return ATN_ERR_LEN;
            }
            counter++;
        }
    }
    atn_memzero(block, sizeof(block));
    return ATN_OK;
}
