/*
 * DEC-0021 lab config parser.
 */
#include "atn_cfg.h"
#include "atn_compromise.h"
#include "atn_policy.h"
#include "atn_update.h"
#include "atn_tun.h"
#include "atn_repl.h"
#include "atn_platform.h"

#include <stdio.h>
#include <stdlib.h>
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
        /* DEC-0032: hub17 rejected; consecutive hub2..hub10 accepted. */
        {
            char line[96];
            char *big;
            size_t cap, hn = 0;
            unsigned h;
            int ln;
            check("caps match",
                  ATN_CFG_MAX_HUBS == 16u && ATN_REPL_MAX_NODES == 16u &&
                  ATN_CFG_MAX_HUBS == ATN_REPL_MAX_NODES);
            ln = sprintf(line,
                         "peer_ipv4=127.0.0.1\npeer_port=1\npeer_ek=");
            for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
                /* size probe only */
            }
            cap = 64u + (size_t)ln + ATN_MLKEM1024_EK_LEN * 2u +
                  15u * (48u + ATN_MLKEM1024_EK_LEN * 2u);
            big = (char *)malloc(cap);
            check("hub10 alloc", big != NULL);
            if (big != NULL) {
                memcpy(big + hn, line, (size_t)ln);
                hn += (size_t)ln;
                for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
                    big[hn++] = 'a';
                }
                for (h = 2; h <= 10u; h++) {
                    ln = sprintf(line,
                                 "\nhub%u_ipv4=10.0.0.%u\nhub%u_port=%u\nhub%u_ek=",
                                 h, h, h, 2400u + h, h);
                    memcpy(big + hn, line, (size_t)ln);
                    hn += (size_t)ln;
                    for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
                        big[hn++] = (char)('a' + (h % 6));
                    }
                }
                check("hub2..10 parse", atn_cfg_parse(big, hn, &c) == ATN_OK);
                check("hub2..10 count", atn_cfg_hub_count(&c) == 10);
                check("hub10 get",
                      atn_cfg_hub_get(&c, 9, &ip, &pt, ekout) == ATN_OK &&
                      ip == 0x0a00000au && pt == 2410);
                ln = sprintf(line,
                             "\nhub17_ipv4=10.0.0.17\nhub17_port=2417\nhub17_ek=");
                memcpy(big + hn, line, (size_t)ln);
                hn += (size_t)ln;
                for (i = 0; i < ATN_MLKEM1024_EK_LEN * 2u; i++) {
                    big[hn++] = 'f';
                }
                check("hub17 reject",
                      atn_cfg_parse(big, hn, &c) == ATN_ERR_PARAM);
                free(big);
            }
        }
    }

    /* DEC-0045 org network policy */
    {
        atn_policy p, p2;
        char text[ATN_POLICY_MAX_TEXT];
        uint8_t wire[ATN_POLICY_MAX_WIRE];
        size_t tn = 0, wn = 0;
        static const char body[] =
            "policy_ver=3\ndiag=1\nflush_mode=log_only\n"
            "wipe_armed=0\noutage_class=maintenance\n";
        check("policy parse",
              atn_policy_parse(body, sizeof(body) - 1u, &p) == ATN_OK);
        check("policy ver", p.ver == 3u);
        check("policy diag", p.diag == 1u);
        check("policy flush", p.flush_mode == ATN_CFG_FLUSH_LOG_ONLY);
        check("policy outage", p.outage_class == ATN_CFG_OUTAGE_MAINTENANCE);
        check("policy log_only needs diag",
              atn_policy_parse("flush_mode=log_only\n", 19, &p) ==
                  ATN_ERR_PARAM);
        check("policy unknown fail",
              atn_policy_parse("peer_ipv4=1.2.3.4\n", 17, &p) ==
                  ATN_ERR_PARAM);
        atn_policy_init(&p);
        p.ver = 7;
        p.diag = 1;
        p.flush_mode = ATN_CFG_FLUSH_LOG_ONLY;
        p.outage_class = ATN_CFG_OUTAGE_BLACKOUT;
        p.boom_silence_s = 45;
        p.password_fail_max = 4;
        p.biometric_allowed = 0;
        p.password_min_len = 14;
        p.usb_data_block = 1;
        p.pwd_deny_check = 1;
        check("policy encode",
              atn_policy_encode(&p, text, sizeof(text), &tn) == ATN_OK);
        check("policy roundtrip",
              atn_policy_parse(text, tn, &p2) == ATN_OK && p2.ver == 7u &&
                  p2.outage_class == ATN_CFG_OUTAGE_BLACKOUT &&
                  p2.boom_silence_s == 45u && p2.password_fail_max == 4u &&
                  p2.password_min_len == 14u);
        check("policy wire",
              atn_policy_encode_wire(&p, wire, sizeof(wire), &wn) == ATN_OK &&
                  wn > 1u && wire[0] == ATN_POLICY_WIRE);
        check("policy wire parse",
              atn_policy_parse_wire(wire, wn, &p2) == ATN_OK && p2.ver == 7u);
        wire[0] = ATN_POLICY_WIRE;
        check("policy req lone P",
              atn_policy_parse_wire(wire, 1, &p2) == ATN_ERR_STATE);
        wire[1] = (uint8_t)'?';
        check("policy req P?",
              atn_policy_parse_wire(wire, 2, &p2) == ATN_ERR_STATE);
        atn_cfg_init(&c);
        atn_policy_to_cfg(&p, &c);
        check("policy to cfg", c.diag == 1 && c.have_diag &&
              c.outage_class == ATN_CFG_OUTAGE_BLACKOUT);
    }

    /* DEC-0047 compromise vote encode/parse */
    {
        atn_compromise c, c2;
        char text[ATN_COMP_MAX_TEXT];
        uint8_t wire[ATN_COMP_MAX_WIRE];
        size_t tn = 0, wn = 0;
        static const char body[] =
            "vote_id=9\ntarget_label=+61400000000\nopened_unix=1000\n"
            "timeout_s=120\nyes=0\nno=0\nquorum=2\nstate=open\n";
        check("comp parse",
              atn_compromise_parse(body, sizeof(body) - 1u, &c) == ATN_OK);
        check("comp vote_id", c.vote_id == 9u);
        check("comp label", strcmp(c.target_label, "+61400000000") == 0);
        check("comp timeout", c.timeout_s == 120u);
        check("comp quorum", c.quorum == 2u);
        check("comp state open", c.state == ATN_COMP_STATE_OPEN);
        check("comp quorum not met", atn_compromise_quorum_met(&c) == 0);
        c.yes = 2;
        check("comp quorum met", atn_compromise_quorum_met(&c) == 1);
        check("comp not timed out",
              atn_compromise_timed_out(&c, 1000u + 119u) == 0);
        check("comp timed out",
              atn_compromise_timed_out(&c, 1000u + 120u) == 1);
        check("comp bad timeout",
              atn_compromise_parse("timeout_s=10\n", 13, &c) == ATN_ERR_PARAM);
        check("comp unknown fail",
              atn_compromise_parse("foo=1\n", 6, &c) == ATN_ERR_PARAM);
        atn_compromise_init(&c);
        c.vote_id = 3;
        memcpy(c.target_label, "+61111", 7);
        c.opened_unix = 50;
        c.timeout_s = 60;
        c.yes = 1;
        c.no = 0;
        c.quorum = 1;
        c.state = ATN_COMP_STATE_OPEN;
        check("comp encode",
              atn_compromise_encode(&c, text, sizeof(text), &tn) == ATN_OK);
        check("comp roundtrip",
              atn_compromise_parse(text, tn, &c2) == ATN_OK &&
                  c2.vote_id == 3u && c2.yes == 1u &&
                  c2.state == ATN_COMP_STATE_OPEN &&
                  strcmp(c2.target_label, "+61111") == 0);
        check("comp wire",
              atn_compromise_encode_wire(&c, ATN_COMP_ACT_BOOM, wire,
                                        sizeof(wire), &wn) == ATN_OK &&
                  wn > 1u && wire[0] == ATN_COMP_WIRE);
        check("comp wire parse",
              atn_compromise_parse_wire(wire, wn, &c2) == ATN_OK &&
                  c2.action == ATN_COMP_ACT_BOOM && c2.vote_id == 3u);
    }

    /* DEC-0048 mesh update announce + chunk wire (tunnel-only SoT) */
    {
        atn_update u, u2;
        char text[ATN_UPD_MAX_TEXT];
        uint8_t wire[ATN_TUN_MAX_PT];
        uint8_t chunk[16];
        size_t tn = 0, wn = 0;
        uint32_t off = 0;
        uint16_t clen = 0;
        const uint8_t *cdata = NULL;
        static const char body[] =
            "update_id=4\nkind=apk\nversion=1.2.3\n"
            "sha256=0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef\n"
            "size=100\nchunk_size=900\n"
            "payload_path=lab/updates/payload.bin\n";
        check("upd parse",
              atn_update_parse(body, sizeof(body) - 1u, &u) == ATN_OK);
        check("upd id", u.update_id == 4u);
        check("upd kind apk", u.kind == ATN_UPD_KIND_APK);
        check("upd size", u.size == 100u);
        check("upd unknown fail",
              atn_update_parse("foo=1\n", 6, &u) == ATN_ERR_PARAM);
        atn_update_init(&u);
        u.update_id = 5;
        u.kind = ATN_UPD_KIND_SITE;
        memcpy(u.version, "9.9", 4);
        memcpy(u.sha256_hex,
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
               65);
        u.size = 32;
        check("upd encode",
              atn_update_encode(&u, text, sizeof(text), &tn) == ATN_OK);
        check("upd roundtrip",
              atn_update_parse(text, tn, &u2) == ATN_OK &&
                  u2.update_id == 5u && u2.kind == ATN_UPD_KIND_SITE &&
                  u2.size == 32u);
        check("upd announce wire",
              atn_update_encode_announce_wire(&u, wire, sizeof(wire), &wn) ==
                      ATN_OK &&
                  wn > 1u && wire[0] == ATN_UPD_WIRE);
        check("upd announce parse",
              atn_update_parse_wire(wire, wn, &u2, &off, &clen, &cdata) ==
                      ATN_OK &&
                  u2.action == ATN_UPD_ACT_ANNOUNCE && u2.update_id == 5u);
        memcpy(chunk, "hello-update-pad!", 16);
        check("upd chunk wire",
              atn_update_encode_chunk_wire(5u, 0u, chunk, 16u, wire,
                                          sizeof(wire), &wn) == ATN_OK &&
                  wire[0] == ATN_UPD_WIRE && wire[1] == (uint8_t)'C');
        check("upd chunk parse",
              atn_update_parse_wire(wire, wn, &u2, &off, &clen, &cdata) ==
                      ATN_OK &&
                  u2.action == ATN_UPD_ACT_CHUNK && off == 0u && clen == 16u &&
                  cdata != NULL && memcmp(cdata, chunk, 16) == 0);
        wire[0] = ATN_UPD_WIRE;
        wire[1] = (uint8_t)'?';
        check("upd req U?",
              atn_update_parse_wire(wire, 2, &u2, &off, &clen, &cdata) ==
                  ATN_ERR_STATE);
    }

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
