/*
 * Module: atn_tun.c
 * REQ:    REQ-1.2
 * Spec:   docs/TUNNEL.md, DEC-0007, FIPS 203, RFC 8439, RFC 5869
 */

#include "atn_tun.h"

#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET atn_sock;
#  define ATN_INV  INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <errno.h>
   typedef int atn_sock;
#  define ATN_INV  (-1)
#endif

static const char INFO_PREFIX[10] = {
    'a','t','n','-','t','u','n','-','v','1'
};

static int g_net;

int atn_net_init(void)
{
#if defined(ATN_OS_WINDOWS)
    WSADATA w;
    if (g_net) {
        return ATN_OK;
    }
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        return ATN_ERR_SOCK;
    }
#endif
    g_net = 1;
    return ATN_OK;
}

void atn_net_fini(void)
{
#if defined(ATN_OS_WINDOWS)
    if (g_net) {
        WSACleanup();
    }
#endif
    g_net = 0;
}

static atn_sock sock_of(const atn_tun *t)
{
    return (atn_sock)t->sock;
}

static void hdr_write(uint8_t h[16], uint8_t type, uint32_t len, uint64_t seq)
{
    h[0] = (uint8_t)ATN_TUN_VERSION;
    h[1] = type;
    h[2] = 0;
    h[3] = 0;
    h[4] = (uint8_t)len;
    h[5] = (uint8_t)(len >> 8);
    h[6] = (uint8_t)(len >> 16);
    h[7] = (uint8_t)(len >> 24);
    h[8]  = (uint8_t)seq;
    h[9]  = (uint8_t)(seq >> 8);
    h[10] = (uint8_t)(seq >> 16);
    h[11] = (uint8_t)(seq >> 24);
    h[12] = (uint8_t)(seq >> 32);
    h[13] = (uint8_t)(seq >> 40);
    h[14] = (uint8_t)(seq >> 48);
    h[15] = (uint8_t)(seq >> 56);
}

static int hdr_read(const uint8_t *h, size_t n, uint8_t *type, uint32_t *len, uint64_t *seq)
{
    if (n < ATN_TUN_HDR_LEN) {
        return ATN_ERR_LEN;
    }
    if (h[0] != ATN_TUN_VERSION || h[2] != 0 || h[3] != 0) {
        return ATN_ERR_PARAM;
    }
    *type = h[1];
    *len = (uint32_t)h[4] | ((uint32_t)h[5] << 8) |
           ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
    *seq = (uint64_t)h[8] | ((uint64_t)h[9] << 8) |
           ((uint64_t)h[10] << 16) | ((uint64_t)h[11] << 24) |
           ((uint64_t)h[12] << 32) | ((uint64_t)h[13] << 40) |
           ((uint64_t)h[14] << 48) | ((uint64_t)h[15] << 56);
    if (ATN_TUN_HDR_LEN + (size_t)*len != n) {
        return ATN_ERR_LEN;
    }
    return ATN_OK;
}

static int udp_send(atn_tun *t, const uint8_t *buf, size_t n)
{
    struct sockaddr_in sa;
    int r;
    if (!t->have_peer) {
        return ATN_ERR_STATE;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(t->peer_port);
    sa.sin_addr.s_addr = htonl(t->peer_addr);
    memcpy(t->last_wire, buf, n);
    t->last_wire_len = n;
#if defined(ATN_OS_WINDOWS)
    r = sendto(sock_of(t), (const char *)buf, (int)n, 0,
               (struct sockaddr *)&sa, sizeof(sa));
#else
    r = (int)sendto(sock_of(t), buf, n, 0,
                    (const struct sockaddr *)&sa, sizeof(sa));
#endif
    if (r < 0 || (size_t)r != n) {
        return ATN_ERR_SOCK;
    }
    return ATN_OK;
}

static int udp_recv(atn_tun *t, uint8_t *buf, size_t max, size_t *out, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    struct sockaddr_in sa;
    int r;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    FD_ZERO(&rfds);
    FD_SET(sock_of(t), &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    r = select((int)sock_of(t) + 1, &rfds, NULL, NULL, &tv);
    if (r == 0) {
        return ATN_ERR_STATE; /* timeout */
    }
    if (r < 0) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
#if defined(ATN_OS_WINDOWS)
    r = recvfrom(sock_of(t), (char *)buf, (int)max, 0,
                 (struct sockaddr *)&sa, &slen);
#else
    r = (int)recvfrom(sock_of(t), buf, max, 0,
                      (struct sockaddr *)&sa, &slen);
#endif
    if (r <= 0) {
        return ATN_ERR_SOCK;
    }
    if (!t->have_peer) {
        t->peer_addr = ntohl(sa.sin_addr.s_addr);
        t->peer_port = ntohs(sa.sin_port);
        t->have_peer = 1;
    }
    *out = (size_t)r;
    return ATN_OK;
}

static int derive_keys(atn_tun *t, const uint8_t ss[32],
                       const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                       const uint8_t kem_ct[ATN_MLKEM1024_CT_LEN])
{
    uint8_t salt[32], info[10 + ATN_MLKEM1024_CT_LEN], okm[96];
    int rc;
    atn_sha3_256(ek, ATN_MLKEM1024_EK_LEN, salt);
    memcpy(info, INFO_PREFIX, 10);
    memcpy(info + 10, kem_ct, ATN_MLKEM1024_CT_LEN);
    rc = atn_hkdf_sha512(salt, 32, ss, 32, info, sizeof(info), okm, 96);
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(t->k_ack, okm, 32);
    if (t->initiator) {
        memcpy(t->k_send, okm + 32, 32);
        memcpy(t->k_recv, okm + 64, 32);
    } else {
        memcpy(t->k_send, okm + 64, 32);
        memcpy(t->k_recv, okm + 32, 32);
    }
    atn_sha3_256(kem_ct, ATN_MLKEM1024_CT_LEN, t->confirm);
    atn_memzero(salt, sizeof(salt));
    atn_memzero(okm, sizeof(okm));
    atn_memzero(info, sizeof(info));
    return ATN_OK;
}

static int replay_ok(atn_tun *t, uint64_t seq)
{
    uint64_t off;
    if (seq == 0) {
        return 0;
    }
    if (seq > t->recv_max) {
        uint64_t shift = seq - t->recv_max;
        if (shift >= 64u) {
            t->recv_win = 0;
        } else {
            t->recv_win <<= shift;
        }
        t->recv_max = seq;
        t->recv_win |= 1ull;
        return 1;
    }
    off = t->recv_max - seq;
    if (off >= 64u) {
        return 0;
    }
    if (t->recv_win & (1ull << off)) {
        return 0;
    }
    t->recv_win |= (1ull << off);
    return 1;
}

static void tun_zero_keys(atn_tun *t)
{
    atn_memzero(t->k_ack, 32);
    atn_memzero(t->k_send, 32);
    atn_memzero(t->k_recv, 32);
    atn_memzero(t->own_dk, sizeof(t->own_dk));
}

void atn_tun_wipe(atn_tun *t)
{
    if (t == NULL) {
        return;
    }
    if (sock_of(t) != ATN_INV) {
#if defined(ATN_OS_WINDOWS)
        closesocket(sock_of(t));
#else
        close(sock_of(t));
#endif
    }
    tun_zero_keys(t);
    atn_memzero(t, sizeof(*t));
    t->sock = (intptr_t)ATN_INV;
    t->state = ATN_TUN_CLOSED;
}

static int tun_base_init(atn_tun *t)
{
    memset(t, 0, sizeof(*t));
    t->sock = (intptr_t)ATN_INV;
    t->state = ATN_TUN_CLOSED;
    t->send_seq = 1;
    return atn_net_init();
}

int atn_tun_init_initiator(atn_tun *t, const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN])
{
    int rc;
    if (t == NULL || peer_ek == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = tun_base_init(t);
    if (rc != ATN_OK) {
        return rc;
    }
    t->initiator = 1;
    memcpy(t->peer_ek, peer_ek, ATN_MLKEM1024_EK_LEN);
    return ATN_OK;
}

int atn_tun_init_responder(atn_tun *t, const uint8_t own_dk[ATN_MLKEM1024_DK_LEN])
{
    int rc;
    if (t == NULL || own_dk == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = tun_base_init(t);
    if (rc != ATN_OK) {
        return rc;
    }
    t->initiator = 0;
    memcpy(t->own_dk, own_dk, ATN_MLKEM1024_DK_LEN);
    return ATN_OK;
}

static int tun_bind_host(atn_tun *t, uint32_t ipv4_host, uint16_t port)
{
    atn_sock s;
    struct sockaddr_in sa;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    if (t == NULL) {
        return ATN_ERR_PARAM;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ipv4_host);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
#if defined(ATN_OS_WINDOWS)
        closesocket(s);
#else
        close(s);
#endif
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    if (getsockname(s, (struct sockaddr *)&sa, &slen) != 0) {
#if defined(ATN_OS_WINDOWS)
        closesocket(s);
#else
        close(s);
#endif
        return ATN_ERR_SOCK;
    }
    t->sock = (intptr_t)s;
    t->local_port = ntohs(sa.sin_port);
    return ATN_OK;
}

int atn_tun_bind(atn_tun *t, uint16_t port)
{
    return tun_bind_host(t, 0x7f000001u, port);
}

int atn_tun_bind_any(atn_tun *t, uint16_t port)
{
    return tun_bind_host(t, 0u, port); /* INADDR_ANY, DEC-0021 */
}

int atn_tun_set_peer(atn_tun *t, uint32_t ipv4_host, uint16_t port)
{
    if (t == NULL || port == 0) {
        return ATN_ERR_PARAM;
    }
    t->peer_addr = ipv4_host;
    t->peer_port = port;
    t->have_peer = 1;
    return ATN_OK;
}

int atn_tun_hs_send_init(atn_tun *t)
{
    uint8_t ss[32], pkt[ATN_TUN_HDR_LEN + ATN_MLKEM1024_CT_LEN];
    int rc;
    if (t == NULL || !t->initiator) {
        return ATN_ERR_PARAM;
    }
    if (t->state != ATN_TUN_CLOSED) {
        return ATN_ERR_STATE;
    }
    rc = atn_mlkem1024_encaps(t->peer_ek, ss, t->kem_ct);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = derive_keys(t, ss, t->peer_ek, t->kem_ct);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        return rc;
    }
    hdr_write(pkt, ATN_TUN_HS_INIT, ATN_MLKEM1024_CT_LEN, 0);
    memcpy(pkt + ATN_TUN_HDR_LEN, t->kem_ct, ATN_MLKEM1024_CT_LEN);
    rc = udp_send(t, pkt, sizeof(pkt));
    if (rc != ATN_OK) {
        return rc;
    }
    t->state = ATN_TUN_HANDSHAKE;
    return ATN_OK;
}

static int send_ack(atn_tun *t)
{
    uint8_t hdr[16], ct[32], tag[16], pkt[16 + 32 + 16];
    uint8_t nonce[12];
    int rc;
    hdr_write(hdr, ATN_TUN_HS_ACK, 32 + 16, 0);
    atn_nonce_format(nonce, 0, 0);
    rc = atn_aead_encrypt(t->k_ack, nonce, hdr, 16, t->confirm, 32, ct, tag);
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(pkt, hdr, 16);
    memcpy(pkt + 16, ct, 32);
    memcpy(pkt + 48, tag, 16);
    return udp_send(t, pkt, sizeof(pkt));
}

static int handle_init(atn_tun *t, const uint8_t *pl, uint32_t len)
{
    uint8_t ss[32];
    int rc;
    if (t->initiator || len != ATN_MLKEM1024_CT_LEN) {
        return ATN_ERR_PARAM;
    }
    memcpy(t->kem_ct, pl, ATN_MLKEM1024_CT_LEN);
    rc = atn_mlkem1024_decaps(t->own_dk, t->kem_ct, ss);
    if (rc != ATN_OK) {
        return rc;
    }
    /* ek is stored in dk at offset 384*k = 1536 for k=4 */
    rc = derive_keys(t, ss, t->own_dk + 1536, t->kem_ct);
    atn_memzero(ss, sizeof(ss));
    if (rc != ATN_OK) {
        return rc;
    }
    rc = send_ack(t);
    if (rc != ATN_OK) {
        return rc;
    }
    t->state = ATN_TUN_ESTABLISHED;
    return ATN_OK;
}

static int handle_ack(atn_tun *t, const uint8_t *dg, size_t n, uint32_t len, uint64_t seq)
{
    uint8_t hdr[16], nonce[12], pt[32];
    int rc;
    if (!t->initiator || t->state != ATN_TUN_HANDSHAKE) {
        return ATN_ERR_STATE;
    }
    if (len != 48 || seq != 0 || n != 16 + 48) {
        return ATN_ERR_LEN;
    }
    memcpy(hdr, dg, 16);
    atn_nonce_format(nonce, 0, 0);
    rc = atn_aead_decrypt(t->k_ack, nonce, hdr, 16, dg + 16, 32, dg + 48, pt);
    if (rc != ATN_OK) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        return ATN_ERR_AUTH;
    }
    if (!atn_ct_equal(pt, t->confirm, 32)) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        atn_memzero(pt, sizeof(pt));
        return ATN_ERR_AUTH;
    }
    atn_memzero(pt, sizeof(pt));
    t->state = ATN_TUN_ESTABLISHED;
    return ATN_OK;
}

static int handle_data(atn_tun *t, const uint8_t *dg, size_t n,
                       uint32_t len, uint64_t seq, uint8_t type,
                       uint8_t *pt, size_t *ptlen, size_t max)
{
    uint8_t hdr[16], nonce[12];
    uint32_t sender = t->initiator ? 2u : 1u;
    size_t clen;
    int rc;
    if (t->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    if (len < 16) {
        return ATN_ERR_LEN;
    }
    clen = (size_t)len - 16u;
    if (clen > max) {
        return ATN_ERR_LEN;
    }
    if (!replay_ok(t, seq)) {
        return ATN_ERR_NONCE;
    }
    memcpy(hdr, dg, 16);
    atn_nonce_format(nonce, sender, seq);
    rc = atn_aead_decrypt(t->k_recv, nonce, hdr, 16,
                          dg + 16, clen, dg + 16 + clen, pt);
    if (rc != ATN_OK) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        return ATN_ERR_AUTH;
    }
    (void)n;
    (void)type;
    *ptlen = clen;
    return ATN_OK;
}

int atn_tun_pump(atn_tun *t, int timeout_ms)
{
    uint8_t dg[ATN_TUN_MAX_DG];
    size_t n = 0;
    uint8_t type;
    uint32_t len;
    uint64_t seq;
    int rc;
    if (t == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = udp_recv(t, dg, sizeof(dg), &n, timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = hdr_read(dg, n, &type, &len, &seq);
    if (rc != ATN_OK) {
        return rc;
    }
    if (type == ATN_TUN_HS_INIT) {
        return handle_init(t, dg + ATN_TUN_HDR_LEN, len);
    }
    if (type == ATN_TUN_HS_ACK) {
        return handle_ack(t, dg, n, len, seq);
    }
    if (type == ATN_TUN_CLOSE) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        return ATN_OK;
    }
    /* DATA/KA consumed only via atn_tun_recv_data */
    return ATN_ERR_STATE;
}

static int tun_send_typed(atn_tun *t, uint8_t type, const uint8_t *pt, size_t n)
{
    uint8_t hdr[16], nonce[12], tag[16];
    uint8_t pkt[ATN_TUN_HDR_LEN + ATN_TUN_MAX_PT + 16];
    uint32_t sender;
    uint32_t plen;
    int rc;
    if (t == NULL || (pt == NULL && n != 0)) {
        return ATN_ERR_PARAM;
    }
    if (t->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    if (n > ATN_TUN_MAX_PT) {
        return ATN_ERR_LEN;
    }
    if (t->send_seq == UINT64_MAX) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        return ATN_ERR_NONCE;
    }
    sender = t->initiator ? 1u : 2u;
    plen = (uint32_t)(n + 16u);
    hdr_write(hdr, type, plen, t->send_seq);
    atn_nonce_format(nonce, sender, t->send_seq);
    rc = atn_aead_encrypt(t->k_send, nonce, hdr, 16, pt, n, pkt + 16, tag);
    if (rc != ATN_OK) {
        return rc;
    }
    memcpy(pkt, hdr, 16);
    memcpy(pkt + 16 + n, tag, 16);
    rc = udp_send(t, pkt, 16u + n + 16u);
    if (rc == ATN_OK) {
        t->send_seq++;
    }
    return rc;
}

int atn_tun_send(atn_tun *t, const uint8_t *pt, size_t n)
{
    return tun_send_typed(t, ATN_TUN_DATA, pt, n);
}

int atn_tun_recv_data(atn_tun *t, uint8_t *pt, size_t *n, size_t max, int timeout_ms)
{
    uint8_t dg[ATN_TUN_MAX_DG];
    size_t got = 0;
    uint8_t type;
    uint32_t len;
    uint64_t seq;
    int rc;
    if (t == NULL || pt == NULL || n == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = udp_recv(t, dg, sizeof(dg), &got, timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = hdr_read(dg, got, &type, &len, &seq);
    if (rc != ATN_OK) {
        return rc;
    }
    if (type == ATN_TUN_HS_INIT) {
        return handle_init(t, dg + ATN_TUN_HDR_LEN, len);
    }
    if (type == ATN_TUN_HS_ACK) {
        return handle_ack(t, dg, got, len, seq);
    }
    if (type == ATN_TUN_CLOSE) {
        t->state = ATN_TUN_CLOSED;
        tun_zero_keys(t);
        return ATN_ERR_STATE;
    }
    if (type != ATN_TUN_DATA && type != ATN_TUN_KA) {
        return ATN_ERR_PARAM;
    }
    return handle_data(t, dg, got, len, seq, type, pt, n, max);
}

int atn_tun_keepalive(atn_tun *t)
{
    return tun_send_typed(t, ATN_TUN_KA, NULL, 0);
}

int atn_tun_resend_last(atn_tun *t)
{
    if (t == NULL || t->last_wire_len == 0) {
        return ATN_ERR_PARAM;
    }
    return udp_send(t, t->last_wire, t->last_wire_len);
}

int atn_tun_close(atn_tun *t)
{
    uint8_t hdr[16], nonce[12], tag[16], pkt[16 + 16];
    uint32_t sender;
    int rc;
    if (t == NULL) {
        return ATN_ERR_PARAM;
    }
    if (t->state == ATN_TUN_ESTABLISHED) {
        sender = t->initiator ? 1u : 2u;
        hdr_write(hdr, ATN_TUN_CLOSE, 16, t->send_seq);
        atn_nonce_format(nonce, sender, t->send_seq);
        rc = atn_aead_encrypt(t->k_send, nonce, hdr, 16, NULL, 0, pkt + 16, tag);
        if (rc == ATN_OK) {
            memcpy(pkt, hdr, 16);
            memcpy(pkt + 16, tag, 16);
            (void)udp_send(t, pkt, sizeof(pkt));
        }
    }
    atn_tun_wipe(t);
    return ATN_OK;
}
