/*
 * REQ-3.3 / DIAG D-08: multi-hub wire failover (DEC-0028 / 0031).
 *
 * Proves:
 *   1) Dark hub0 (no listener) → connect hub1 → ESTABLISHED + echo
 *   2) Wrong-ek hub0 AUTH close → connect hub1 → ESTABLISHED
 *   3) atn_dmon_tun_failover over all-dark hubs → ATN_ERR_STATE, keys kept
 */
#include "atn_cfg.h"
#include "atn_dmon.h"
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

static void append_hex(char *dst, size_t *n, const uint8_t *p, size_t len)
{
    size_t i;
    static const char hex[] = "0123456789abcdef";
    for (i = 0; i < len; i++) {
        dst[(*n)++] = hex[p[i] >> 4];
        dst[(*n)++] = hex[p[i] & 15u];
    }
}

static int build_two_hub_conf(char *text, size_t cap, size_t *out_n,
                              uint16_t port0, const uint8_t *ek0,
                              uint16_t port1, const uint8_t *ek1)
{
    char hdr[96];
    int hn;
    size_t n = 0;

    hn = sprintf(hdr, "peer_ipv4=127.0.0.1\npeer_port=%u\npeer_ek=",
                 (unsigned)port0);
    if (hn < 0 || n + (size_t)hn + ATN_MLKEM1024_EK_LEN * 4u + 128u >= cap) {
        return ATN_ERR_LEN;
    }
    memcpy(text + n, hdr, (size_t)hn);
    n += (size_t)hn;
    append_hex(text, &n, ek0, ATN_MLKEM1024_EK_LEN);
    hn = sprintf(hdr, "\nhub2_ipv4=127.0.0.1\nhub2_port=%u\nhub2_ek=",
                 (unsigned)port1);
    if (hn < 0) {
        return ATN_ERR_LEN;
    }
    memcpy(text + n, hdr, (size_t)hn);
    n += (size_t)hn;
    append_hex(text, &n, ek1, ATN_MLKEM1024_EK_LEN);
    text[n++] = '\n';
    *out_n = n;
    return ATN_OK;
}

/* After INIT from phone, pump responder then phone to ESTABLISHED. */
static int hs_complete(atn_dmon *phone, atn_tun *hub)
{
    int i;
    int rc = ATN_ERR_STATE;
    for (i = 0; i < 16 && hub->state != ATN_TUN_ESTABLISHED; i++) {
        rc = atn_tun_pump(hub, 3000);
    }
    if (hub->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    for (i = 0; i < 8 && atn_dmon_tun_state(phone) != ATN_TUN_ESTABLISHED; i++) {
        rc = atn_dmon_tun_pump(phone, 3000);
    }
    if (atn_dmon_tun_state(phone) != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    (void)rc;
    return ATN_OK;
}

static int echo_lab(atn_dmon *phone, atn_tun *hub)
{
    uint8_t hello[4], back[64];
    size_t nrecv = 0;
    memcpy(hello, "lab!", 4);
    if (atn_dmon_tun_send(phone, hello, 4) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    if (atn_tun_recv_data(hub, back, &nrecv, sizeof(back), 3000) != ATN_OK ||
        nrecv != 4 || memcmp(back, hello, 4) != 0) {
        return ATN_ERR_STATE;
    }
    return ATN_OK;
}

int main(void)
{
    atn_dmon phone;
    atn_tun hub1;
    atn_cfg cfg;
    uint8_t dk[32], ck[32];
    uint8_t ek0[ATN_MLKEM1024_EK_LEN], dk0[ATN_MLKEM1024_DK_LEN];
    uint8_t ek1[ATN_MLKEM1024_EK_LEN], dk1[ATN_MLKEM1024_DK_LEN];
    uint8_t ek_wrong[ATN_MLKEM1024_EK_LEN], dk_wrong[ATN_MLKEM1024_DK_LEN];
    char text[256 + ATN_MLKEM1024_EK_LEN * 4u];
    size_t tn = 0;
    int rc;
    unsigned i;

    printf("athanor hub_failover  platform=%s\n", atn_platform_id());
    check("rng", atn_random_bytes(dk, 32) == ATN_OK &&
          atn_random_bytes(ck, 32) == ATN_OK);
    check("kem",
          atn_mlkem1024_keygen(ek0, dk0) == ATN_OK &&
          atn_mlkem1024_keygen(ek1, dk1) == ATN_OK &&
          atn_mlkem1024_keygen(ek_wrong, dk_wrong) == ATN_OK);

    /* --- D-08 path A: dark hub0 → live hub1 --- */
    check("hub1 init", atn_tun_init_responder(&hub1, dk1) == ATN_OK);
    check("hub1 bind", atn_tun_bind(&hub1, 0) == ATN_OK && hub1.local_port != 0);
    check("conf dark0",
          build_two_hub_conf(text, sizeof(text), &tn, 64999, ek0,
                             hub1.local_port, ek1) == ATN_OK);
    check("parse dark0", atn_cfg_parse(text, tn, &cfg) == ATN_OK &&
          atn_cfg_ready(&cfg) && atn_cfg_hub_count(&cfg) == 2);

    atn_dmon_init(&phone);
    check("load", atn_dmon_load(&phone, dk, ck) == ATN_OK);
    check("connect dark0",
          atn_dmon_tun_connect_hub(&phone, &cfg, 0) == ATN_OK);
    check("hub_idx 0", atn_dmon_hub_idx(&phone) == 0);
    for (i = 0; i < ATN_DMON_HUB_HS_ATTEMPTS; i++) {
        rc = atn_dmon_tun_pump(&phone, 50);
        /* Timeout → STATE; Windows ICMP to a closed port → SOCK. */
        check("dark0 handshake",
              phone.tun.state == ATN_TUN_HANDSHAKE &&
              phone.tun.state != ATN_TUN_ESTABLISHED &&
              (rc == ATN_ERR_STATE || rc == ATN_OK || rc == ATN_ERR_SOCK));
        (void)atn_dmon_tun_hs_retry(&phone);
    }
    check("connect hub1",
          atn_dmon_tun_connect_hub(&phone, &cfg, 1) == ATN_OK);
    check("hub_idx 1", atn_dmon_hub_idx(&phone) == 1);
    check("hs hub1", hs_complete(&phone, &hub1) == ATN_OK);
    check("echo hub1", echo_lab(&phone, &hub1) == ATN_OK);
    atn_tun_wipe(&hub1);
    atn_memzero(dk1, sizeof(dk1));
    atn_dmon_flush(&phone);

    /* --- Path B: wrong-ek AUTH on hub0 → hub1 --- */
    {
        atn_tun bad, good;
        uint8_t ek_g[ATN_MLKEM1024_EK_LEN], dk_g[ATN_MLKEM1024_DK_LEN];
        uint8_t ek_b[ATN_MLKEM1024_EK_LEN], dk_b[ATN_MLKEM1024_DK_LEN];
        atn_dmon p2;
        atn_cfg c2;

        check("kem B",
              atn_mlkem1024_keygen(ek_g, dk_g) == ATN_OK &&
              atn_mlkem1024_keygen(ek_b, dk_b) == ATN_OK);
        check("bad/good init",
              atn_tun_init_responder(&bad, dk_b) == ATN_OK &&
              atn_tun_init_responder(&good, dk_g) == ATN_OK);
        check("bad/good bind",
              atn_tun_bind(&bad, 0) == ATN_OK &&
              atn_tun_bind(&good, 0) == ATN_OK);
        /* Conf lists wrong ek for hub0's live port. */
        check("conf auth",
              build_two_hub_conf(text, sizeof(text), &tn, bad.local_port,
                                 ek_wrong, good.local_port, ek_g) == ATN_OK);
        check("parse auth",
              atn_cfg_parse(text, tn, &c2) == ATN_OK &&
              atn_cfg_hub_count(&c2) == 2);

        atn_dmon_init(&p2);
        check("load2", atn_dmon_load(&p2, dk, ck) == ATN_OK);
        check("conn wrong ek",
              atn_dmon_tun_connect_hub(&p2, &c2, 0) == ATN_OK);
        {
            int i;
            for (i = 0; i < 16 && bad.state != ATN_TUN_ESTABLISHED; i++) {
                (void)atn_tun_pump(&bad, 3000);
            }
        }
        check("bad INIT", bad.state == ATN_TUN_ESTABLISHED);
        rc = atn_dmon_tun_pump(&p2, 3000);
        check("AUTH close",
              rc == ATN_ERR_AUTH && p2.tun.state == ATN_TUN_CLOSED);
        check("advance hub1",
              atn_dmon_tun_connect_hub(&p2, &c2, 1) == ATN_OK &&
              atn_dmon_hub_idx(&p2) == 1);
        check("hs good", hs_complete(&p2, &good) == ATN_OK);
        check("echo good", echo_lab(&p2, &good) == ATN_OK);

        atn_tun_wipe(&bad);
        atn_tun_wipe(&good);
        atn_memzero(dk_g, sizeof(dk_g));
        atn_memzero(dk_b, sizeof(dk_b));
        atn_dmon_flush(&p2);
    }

    /* --- Path C: blocking failover API, all hubs dark --- */
    {
        atn_cfg dark;
        atn_dmon pd;
        check("dark conf",
              build_two_hub_conf(text, sizeof(text), &tn, 64998, ek0,
                                 64997, ek1) == ATN_OK);
        check("dark parse", atn_cfg_parse(text, tn, &dark) == ATN_OK);
        atn_dmon_init(&pd);
        check("dark load", atn_dmon_load(&pd, dk, ck) == ATN_OK);
        check("failover param",
              atn_dmon_tun_failover(&pd, &dark, 2, 30) == ATN_ERR_PARAM);
        check("all dark failover",
              atn_dmon_tun_failover(&pd, &dark, 0, 30) == ATN_ERR_STATE);
        check("keys kept", atn_dmon_require(&pd) == ATN_OK);
        check("tun detached", !pd.tun_ready);
        atn_dmon_flush(&pd);
    }

    atn_memzero(dk0, sizeof(dk0));
    atn_memzero(dk_wrong, sizeof(dk_wrong));
    atn_net_fini();

    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
