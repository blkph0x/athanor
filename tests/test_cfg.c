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

    {
        FILE *tf = fopen("atn-cfg.tmp", "wb");
        check("tmp write", tf != NULL);
        if (tf != NULL) {
            fwrite("peer_port=9\n", 1, 12, tf);
            fclose(tf);
        }
        check("load file",
              atn_cfg_load_file("atn-cfg.tmp", &c) == ATN_OK && c.port == 9 &&
              !atn_cfg_ready(&c));
        remove("atn-cfg.tmp");
    }

    /* DEC-0027 / 0028 / 0029 */
    {
        char big[200 + ATN_MLKEM1024_EK_LEN * 4u];
        size_t bn = 0;
        uint32_t ip = 0;
        uint16_t pt = 0;
        uint8_t ekout[ATN_MLKEM1024_EK_LEN];
        static const char pfx[] =
            "peer_ipv4=127.0.0.1\npeer_port=2402\npeer_ek=";
        static const char mid[] =
            "\nhub2_ipv4=10.0.0.2\nhub2_port=2403\nhub2_ek=";
        static const char pol[] =
            "\ndiag=1\noutage_class=blackout\n";
        memcpy(big + bn, pfx, sizeof(pfx) - 1u);
        bn += sizeof(pfx) - 1u;
        for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
            big[bn++] = 'a';
        }
        memcpy(big + bn, mid, sizeof(mid) - 1u);
        bn += sizeof(mid) - 1u;
        for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
            big[bn++] = 'b';
        }
        memcpy(big + bn, pol, sizeof(pol) - 1u);
        bn += sizeof(pol) - 1u;
        check("hubs parse", atn_cfg_parse(big, bn, &c) == ATN_OK);
        check("hubs ready", atn_cfg_ready(&c));
        check("hub count", atn_cfg_hub_count(&c) == 2);
        check("diag default log", c.diag == 1 &&
              c.flush_mode == ATN_CFG_FLUSH_LOG_ONLY);
        check("outage blackout", c.outage_class == ATN_CFG_OUTAGE_BLACKOUT);
        check("hub0 get",
              atn_cfg_hub_get(&c, 0, &ip, &pt, ekout) == ATN_OK &&
              ip == 0x7f000001u && pt == 2402 && ekout[0] == 0xaa);
        check("hub1 get",
              atn_cfg_hub_get(&c, 1, &ip, &pt, ekout) == ATN_OK &&
              ip == 0x0a000002u && pt == 2403 && ekout[0] == 0xbb);
        check("log_only needs diag",
              atn_cfg_parse("flush_mode=log_only\n", 19, &c) == ATN_ERR_PARAM);
    }

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
