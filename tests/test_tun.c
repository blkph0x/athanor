/*
 * REQ-1.2 verification: two tunnel endpoints on 127.0.0.1, handshake, echo,
 * no plaintext on the wire, replay drop, MAC failure closes.
 */
#include "atn_tun.h"

#include <stdio.h>
#include <string.h>

static int g_fail;
static int memmem_absent(const uint8_t *hay, size_t hn, const uint8_t *nd, size_t nn);

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
    atn_tun a, b;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t hello[11];
    memcpy(hello, "hello-plain", 11);
    uint8_t back[64];
    size_t n = 0;
    int rc;

    printf("athanor tunnel  platform=%s\n", atn_platform_id());
    check("net init", atn_net_init() == ATN_OK);
    check("kem keygen", atn_mlkem1024_keygen(ek, dk) == ATN_OK);
    check("init A", atn_tun_init_initiator(&a, ek) == ATN_OK);
    check("init B", atn_tun_init_responder(&b, dk) == ATN_OK);
    check("bind A", atn_tun_bind(&a, 0) == ATN_OK && a.local_port != 0);
    check("bind B", atn_tun_bind(&b, 0) == ATN_OK && b.local_port != 0);
    check("peer A", atn_tun_set_peer(&a, 0x7f000001u, b.local_port) == ATN_OK);
    check("peer B", atn_tun_set_peer(&b, 0x7f000001u, a.local_port) == ATN_OK);
    check("hs init", atn_tun_hs_send_init(&a) == ATN_OK);
    check("hs retry", atn_tun_hs_retry(&a) == ATN_OK);
    check("A handshake", a.state == ATN_TUN_HANDSHAKE);
    rc = atn_tun_pump(&b, 3000);
    check("B pump INIT", rc == ATN_OK && b.state == ATN_TUN_ESTABLISHED);
    (void)atn_tun_pump(&b, 200); /* drain HS_INIT retry */
    rc = atn_tun_pump(&a, 3000);
    check("A pump ACK", rc == ATN_OK && a.state == ATN_TUN_ESTABLISHED);
    check("send", atn_tun_send(&a, hello, 11) == ATN_OK);
    check("wire hides plaintext",
          a.last_wire_len > 11 && memcmp(a.last_wire, hello, 11) != 0 &&
          memmem_absent(a.last_wire, a.last_wire_len, hello, 11));
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
    check("echo recv", rc == ATN_OK && n == 11 && memcmp(back, hello, 11) == 0);

    rc = atn_tun_resend_last(&a);
    check("resend", rc == ATN_OK);
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
    check("replay dropped", rc == ATN_ERR_NONCE);

    check("stray send", atn_tun_test_stray(&b) == ATN_OK);
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 200);
    check("stray ignored",
          rc == ATN_ERR_STATE && b.state == ATN_TUN_ESTABLISHED);
    check("send after stray", atn_tun_send(&a, hello, 11) == ATN_OK);
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
    check("recv after stray",
          rc == ATN_OK && n == 11 && memcmp(back, hello, 11) == 0);
    check("ka", atn_tun_keepalive(&a) == ATN_OK);
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
    check("ka recv", rc == ATN_OK && n == 0 && b.state == ATN_TUN_ESTABLISHED);

    /* DEC-0035: PQ rekey then echo again under new DATA keys. */
    check("rekey send", atn_tun_rekey_send(&a) == ATN_OK && a.rekey_pending);
    rc = atn_tun_pump(&b, 3000);
    check("B rekey INIT", rc == ATN_OK && b.state == ATN_TUN_ESTABLISHED &&
          b.send_seq == 1);
    rc = atn_tun_pump(&a, 3000);
    check("A rekey ACK", rc == ATN_OK && a.state == ATN_TUN_ESTABLISHED &&
          !a.rekey_pending && a.send_seq == 1);
    check("post-rekey send", atn_tun_send(&a, hello, 11) == ATN_OK);
    rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
    check("post-rekey recv",
          rc == ATN_OK && n == 11 && memcmp(back, hello, 11) == 0);

    {
        /* New seq + flipped ciphertext: window accepts, AEAD must fail and close. */
        if (a.last_wire_len > 24) {
            a.last_wire[8] = (uint8_t)(a.last_wire[8] + 3);
            a.last_wire[24] ^= 0xff;
            (void)atn_tun_resend_last(&a);
            rc = atn_tun_recv_data(&b, back, &n, sizeof(back), 3000);
            if (!(rc == ATN_ERR_AUTH && b.state == ATN_TUN_CLOSED)) {
                printf("  (bad-mac rc=%d state=%d)\n", rc, b.state);
            }
            check("bad mac closes", rc == ATN_ERR_AUTH && b.state == ATN_TUN_CLOSED);
        }
    }

    atn_tun_wipe(&a);
    atn_tun_wipe(&b);

    /* DEC-0023: encapsulate to the wrong ek; initiator must not ESTABLISH. */
    {
        atn_tun wa, wb;
        uint8_t ek_b[ATN_MLKEM1024_EK_LEN], dk_b[ATN_MLKEM1024_DK_LEN];
        uint8_t ek_w[ATN_MLKEM1024_EK_LEN], dk_w[ATN_MLKEM1024_DK_LEN];
        check("bad-ek kem",
              atn_mlkem1024_keygen(ek_b, dk_b) == ATN_OK &&
              atn_mlkem1024_keygen(ek_w, dk_w) == ATN_OK);
        check("bad-ek init",
              atn_tun_init_initiator(&wa, ek_w) == ATN_OK &&
              atn_tun_init_responder(&wb, dk_b) == ATN_OK);
        check("bad-ek bind",
              atn_tun_bind(&wa, 0) == ATN_OK && atn_tun_bind(&wb, 0) == ATN_OK);
        check("bad-ek peer",
              atn_tun_set_peer(&wa, 0x7f000001u, wb.local_port) == ATN_OK &&
              atn_tun_set_peer(&wb, 0x7f000001u, wa.local_port) == ATN_OK);
        check("bad-ek hs", atn_tun_hs_send_init(&wa) == ATN_OK);
        rc = atn_tun_pump(&wb, 3000);
        check("bad-ek B INIT", rc == ATN_OK);
        rc = atn_tun_pump(&wa, 3000);
        check("bad-ek A not established",
              rc == ATN_ERR_AUTH && wa.state == ATN_TUN_CLOSED);
        atn_tun_wipe(&wa);
        atn_tun_wipe(&wb);
        atn_memzero(dk_b, sizeof(dk_b));
        atn_memzero(dk_w, sizeof(dk_w));
    }

    atn_net_fini();
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}

static int memmem_absent(const uint8_t *hay, size_t hn, const uint8_t *nd, size_t nn)
{
    size_t i;
    if (nn == 0 || hn < nn) {
        return 1;
    }
    for (i = 0; i + nn <= hn; i++) {
        if (memcmp(hay + i, nd, nn) == 0) {
            return 0;
        }
    }
    return 1;
}
