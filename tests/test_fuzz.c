/*
 * In-house parser mutator (REQ-6.1 scaffolding / DEC-0023).
 * Deterministic: SHA-256 of the counter. Not N-hour fuzz.
 */
#include "atn_cfg.h"
#include "atn_dns.h"
#include "atn_http.h"
#include "atn_platform.h"

#include <stdio.h>
#include <string.h>

#define HTTP_ITERS 4096u
#define DNS_ITERS  4096u
#define CFG_ITERS  1024u

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

static void wr32(uint8_t p[4], uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void mutate(uint8_t *buf, size_t *n, size_t cap, uint32_t i)
{
    uint8_t seed[32], ctr[4];
    size_t pos;
    wr32(ctr, i);
    atn_sha256(ctr, 4, seed);
    if (*n == 0) {
        if (cap > 0) {
            buf[0] = seed[0];
            *n = 1;
        }
        return;
    }
    pos = (size_t)seed[1] % *n;
    buf[pos] ^= seed[0];
    if ((seed[2] & 3u) == 0u && *n < cap) {
        buf[*n] = seed[3];
        *n += 1u;
    } else if ((seed[2] & 3u) == 1u && *n > 1u) {
        *n -= 1u;
    }
}

static unsigned fuzz_http(void)
{
    static const char good[] =
        "GET / HTTP/1.1\r\nHost: atn.test\r\n\r\n";
    uint8_t buf[512];
    atn_http_req req;
    unsigned i, ok = 0, bad = 0;
    size_t n;

    for (i = 0; i < HTTP_ITERS; i++) {
        n = sizeof(good) - 1u;
        memcpy(buf, good, n);
        mutate(buf, &n, sizeof(buf), i);
        if (atn_http_parse_request(buf, n, &req) == ATN_OK) {
            ok++;
        } else {
            bad++;
        }
    }
    return ok + bad;
}

static unsigned fuzz_dns(void)
{
    /* RFC 1035 header + root query IN A (12 + 1 + 4 = 17). */
    static const uint8_t good[17] = {
        0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01
    };
    uint8_t buf[128];
    atn_dns_q q;
    unsigned i, ran = 0;
    size_t n;

    for (i = 0; i < DNS_ITERS; i++) {
        n = sizeof(good);
        memcpy(buf, good, n);
        mutate(buf, &n, sizeof(buf), i + 100000u);
        (void)atn_dns_parse_query(buf, n, &q);
        ran++;
    }
    return ran;
}

static unsigned fuzz_cfg(void)
{
    static const char good[] = "peer_port=2402\n# x\n";
    uint8_t buf[256];
    atn_cfg c;
    unsigned i, ran = 0;
    size_t n;

    for (i = 0; i < CFG_ITERS; i++) {
        n = sizeof(good) - 1u;
        memcpy(buf, good, n);
        mutate(buf, &n, sizeof(buf), i + 200000u);
        (void)atn_cfg_parse((const char *)buf, n, &c);
        ran++;
    }
    return ran;
}

int main(void)
{
    unsigned h, d, c;

    printf("athanor fuzz  platform=%s\n", atn_platform_id());
    h = fuzz_http();
    d = fuzz_dns();
    c = fuzz_cfg();
    check("http iters", h == HTTP_ITERS);
    check("dns iters", d == DNS_ITERS);
    check("cfg iters", c == CFG_ITERS);
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
