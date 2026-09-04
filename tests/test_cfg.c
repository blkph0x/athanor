/*
 * DEC-0021 lab config parser.
 */
#include "atn_cfg.h"
#include "atn_platform.h"

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

int main(void)
{
    atn_cfg c;
    char text[80 + ATN_MLKEM1024_EK_LEN * 2u];
    size_t n;
    unsigned i;

    printf("athanor cfg  platform=%s\n", atn_platform_id());
    {
        static const char hdr[] = "peer_ipv4=127.0.0.1\npeer_port=2402\npeer_ek=";
        memcpy(text, hdr, sizeof(hdr) - 1u);
        n = sizeof(hdr) - 1u;
    }
    for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
        text[n++] = 'a';
    }
    text[n] = 0;
    check("parse", atn_cfg_parse(text, n, &c) == ATN_OK);
    check("ready", atn_cfg_ready(&c));
    check("ipv4", c.ipv4_host == 0x7f000001u);
    check("port", c.port == 2402);
    check("ek0", c.ek[0] == 0xaa);

    check("bad ip", atn_cfg_parse("peer_ipv4=127.0.0\n", 18, &c) == ATN_ERR_PARAM);
    check("bad port", atn_cfg_parse("peer_port=0\n", 12, &c) == ATN_ERR_PARAM);
    check("unknown", atn_cfg_parse("foo=bar\n", 8, &c) == ATN_ERR_PARAM);
    check("comment ok",
          atn_cfg_parse("# x\npeer_port=1\n", 16, &c) == ATN_OK &&
          c.have_port && !atn_cfg_ready(&c));
    check("crlf",
          atn_cfg_parse("peer_port=9\r\n", 13, &c) == ATN_OK && c.port == 9);

    {
        static const char hdr[] = "peer_ipv4=10.1.2.3\npeer_port=9\npeer_ek=";
        memcpy(text, hdr, sizeof(hdr) - 1u);
        n = sizeof(hdr) - 1u;
    }
    for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
        text[n++] = (char)((i & 1u) ? 'A' : 'B');
    }
    check("upper hex", atn_cfg_parse(text, n, &c) == ATN_OK &&
          atn_cfg_ready(&c) && c.ipv4_host == 0x0a010203u &&
          c.ek[0] == 0xba);

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
