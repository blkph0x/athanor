/*
 * Voice contacts + hub latency ranking + P2P/relay dial helpers (DEC-0050).
 */
#include "atn_voice.h"

#include <stdio.h>
#include <string.h>

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int parse_hex(const char *s, uint8_t *out, size_t out_n)
{
    size_t i;
    if (s == NULL || out == NULL) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < out_n; i++) {
        int hi = hex_nibble(s[2u * i]);
        int lo = hex_nibble(s[2u * i + 1u]);
        if (hi < 0 || lo < 0) {
            return ATN_ERR_PARAM;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return ATN_OK;
}

static int parse_ipv4(const char *s, uint32_t *out)
{
    unsigned a, b, c, d;
    if (s == NULL || out == NULL) {
        return ATN_ERR_PARAM;
    }
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255u || b > 255u ||
        c > 255u || d > 255u) {
        return ATN_ERR_PARAM;
    }
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) |
           (uint32_t)d;
    return ATN_OK;
}

void atn_voice_roster_init(atn_voice_roster *r)
{
    if (r == NULL) {
        return;
    }
    atn_memzero(r, sizeof(*r));
}

int atn_voice_roster_add(atn_voice_roster *r, const atn_voice_contact *c)
{
    if (r == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    if (r->n >= ATN_VOICE_MAX_CONTACTS) {
        return ATN_ERR_LEN;
    }
    r->c[r->n] = *c;
    r->n++;
    return ATN_OK;
}

const atn_voice_contact *atn_voice_roster_find(const atn_voice_roster *r,
                                               const char *label)
{
    unsigned i;
    if (r == NULL || label == NULL) {
        return NULL;
    }
    for (i = 0; i < r->n; i++) {
        if (strcmp(r->c[i].label, label) == 0) {
            return &r->c[i];
        }
    }
    return NULL;
}

int atn_voice_roster_parse_line(const char *line, atn_voice_contact *c)
{
    char label[ATN_VOICE_LABEL_MAX];
    char ip[64];
    char ekhex[ATN_MLKEM1024_EK_LEN * 2u + 8u];
    unsigned port = 0;
    const char *p;
    int fields;
    if (line == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0' || *line == '#' || *line == '\n' || *line == '\r') {
        return ATN_ERR_STATE;
    }
    atn_memzero(c, sizeof(*c));
    atn_memzero(ekhex, sizeof(ekhex));
    if (sscanf(line, "%63s %63s %u %3136s", label, ip, &port, ekhex) < 4) {
        return ATN_ERR_PARAM;
    }
    if (port == 0u || port > 65535u) {
        return ATN_ERR_PARAM;
    }
    if (strlen(ekhex) != ATN_MLKEM1024_EK_LEN * 2u) {
        return ATN_ERR_LEN;
    }
    memcpy(c->label, label, sizeof(c->label) - 1u);
    if (parse_ipv4(ip, &c->ipv4_host) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    c->port = (uint16_t)port;
    if (parse_hex(ekhex, c->peer_ek, ATN_MLKEM1024_EK_LEN) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    c->have_ek = 1;
    p = line;
    fields = 0;
    while (*p && fields < 4) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '\n' || *p == '\r') {
            break;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            p++;
        }
        fields++;
    }
    while (c->nhubs < ATN_VOICE_MAX_HUBS) {
        char hip[64];
        unsigned hp = 0;
        int n;
        int t;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') {
            break;
        }
        n = sscanf(p, "%63s %u", hip, &hp);
        if (n < 2 || hp == 0u || hp > 65535u) {
            break;
        }
        if (parse_ipv4(hip, &c->hub_ipv4[c->nhubs]) != ATN_OK) {
            break;
        }
        c->hub_port[c->nhubs] = (uint16_t)hp;
        c->nhubs++;
        for (t = 0; t < 2; t++) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                p++;
            }
        }
    }
    return ATN_OK;
}

int atn_voice_roster_load_text(atn_voice_roster *r, const char *text, size_t n)
{
    size_t i = 0;
    if (r == NULL || (n > 0 && text == NULL)) {
        return ATN_ERR_PARAM;
    }
    atn_voice_roster_init(r);
    while (i < n) {
        char line[8192];
        size_t j = 0;
        atn_voice_contact c;
        while (i < n && text[i] != '\n' && j + 1u < sizeof(line)) {
            line[j++] = text[i++];
        }
        if (i < n && text[i] == '\n') {
            i++;
        }
        line[j] = '\0';
        if (atn_voice_roster_parse_line(line, &c) == ATN_OK) {
            if (atn_voice_roster_add(r, &c) != ATN_OK) {
                return ATN_ERR_LEN;
            }
        }
    }
    return ATN_OK;
}

void atn_voice_rank_hubs(atn_voice_hub_rtt *hubs, unsigned n)
{
    unsigned i, j;
    if (hubs == NULL || n < 2u) {
        return;
    }
    for (i = 0; i + 1u < n; i++) {
        for (j = 0; j + 1u < n - i; j++) {
            uint32_t aj = hubs[j].ok ? hubs[j].rtt_ms : UINT32_MAX;
            uint32_t bj = hubs[j + 1u].ok ? hubs[j + 1u].rtt_ms : UINT32_MAX;
            if (aj > bj) {
                atn_voice_hub_rtt tmp = hubs[j];
                hubs[j] = hubs[j + 1u];
                hubs[j + 1u] = tmp;
            }
        }
    }
}

int atn_voice_encode_dial_info(uint32_t call_id, uint32_t ipv4_host,
                               uint16_t port, uint8_t *out, size_t out_cap,
                               size_t *out_n)
{
    if (out == NULL || out_n == NULL || out_cap < 16u) {
        return ATN_ERR_PARAM;
    }
    out[0] = ATN_VOICE_WIRE;
    out[1] = ATN_VOICE_CTRL;
    out[2] = ATN_VOICE_OP_DIAL_INFO;
    out[3] = 0;
    put_be32(out + 4, call_id);
    put_be32(out + 8, ipv4_host);
    out[12] = (uint8_t)((port >> 8) & 0xffu);
    out[13] = (uint8_t)(port & 0xffu);
    out[14] = 0;
    out[15] = 0;
    *out_n = 16u;
    return ATN_OK;
}

int atn_voice_parse_dial_info(const uint8_t *msg, size_t n, uint32_t *call_id,
                              uint32_t *ipv4_host, uint16_t *port)
{
    if (msg == NULL || call_id == NULL || ipv4_host == NULL || port == NULL) {
        return ATN_ERR_PARAM;
    }
    if (n < 16u || msg[0] != ATN_VOICE_WIRE || msg[1] != ATN_VOICE_CTRL ||
        msg[2] != ATN_VOICE_OP_DIAL_INFO) {
        return ATN_ERR_PARAM;
    }
    *call_id = get_be32(msg + 4);
    *ipv4_host = get_be32(msg + 8);
    *port = (uint16_t)(((uint16_t)msg[12] << 8) | (uint16_t)msg[13]);
    return ATN_OK;
}

int atn_voice_try_p2p(atn_voice *v, atn_tun *p2p_tun,
                      const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN],
                      uint32_t ipv4_host, uint16_t port, uint32_t call_id,
                      uint8_t codec, int timeout_ms)
{
    int i;
    uint8_t wire[ATN_VOICE_CTRL_LEN];
    size_t wn = 0;
    int to = timeout_ms > 0 ? timeout_ms : 200;
    if (v == NULL || p2p_tun == NULL || peer_ek == NULL || call_id == 0) {
        return ATN_ERR_PARAM;
    }
    if (atn_tun_init_initiator(p2p_tun, peer_ek) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    if (atn_tun_bind(p2p_tun, 0) != ATN_OK) {
        return ATN_ERR_SOCK;
    }
    if (atn_tun_set_peer(p2p_tun, ipv4_host, port) != ATN_OK) {
        return ATN_ERR_SOCK;
    }
    if (atn_tun_hs_send_init(p2p_tun) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    for (i = 0; i < 32 && p2p_tun->state != ATN_TUN_ESTABLISHED; i++) {
        (void)atn_tun_pump(p2p_tun, to);
    }
    if (p2p_tun->state != ATN_TUN_ESTABLISHED) {
        atn_tun_wipe(p2p_tun);
        return ATN_ERR_STATE;
    }
    atn_voice_init(v, p2p_tun);
    v->call_id = call_id;
    v->codec = codec;
    v->path = ATN_VOICE_PATH_P2P;
    v->peer_ipv4 = ipv4_host;
    v->peer_port = port;
    v->state = ATN_VOICE_OUTGOING;
    if (atn_voice_encode_ctrl(ATN_VOICE_OP_OFFER, codec, call_id, wire,
                              sizeof(wire), &wn) == ATN_OK) {
        (void)atn_tun_send(p2p_tun, wire, wn);
    }
    return ATN_OK;
}

int atn_voice_start_relay(atn_voice *v, atn_tun *hub_tun,
                          const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN],
                          uint32_t call_id, uint8_t codec)
{
    unsigned i;
    int rc;
    uint8_t wire[ATN_VOICE_CTRL_LEN];
    size_t wn = 0;
    if (v == NULL || hub_tun == NULL || peer_ek == NULL || call_id == 0) {
        return ATN_ERR_PARAM;
    }
    if (hub_tun->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    atn_voice_init(v, hub_tun);
    v->call_id = call_id;
    v->codec = codec;
    v->path = ATN_VOICE_PATH_RELAY;
    v->state = ATN_VOICE_OUTGOING;
    rc = atn_voice_e2e_initiator(&v->e2e, peer_ek);
    if (rc != ATN_OK) {
        atn_voice_wipe(v);
        return rc;
    }
    if (atn_voice_encode_ctrl(ATN_VOICE_OP_OFFER, codec, call_id, wire,
                              sizeof(wire), &wn) != ATN_OK ||
        atn_tun_send(hub_tun, wire, wn) != ATN_OK) {
        atn_voice_wipe(v);
        return ATN_ERR_STATE;
    }
    for (i = 0; i < ATN_VOICE_E2E_CT_NCHUNKS; i++) {
        uint8_t chunk[ATN_TUN_MAX_PT];
        size_t cn = 0;
        rc = atn_voice_e2e_encode_ct_chunk(&v->e2e, call_id, i, chunk,
                                           sizeof(chunk), &cn);
        if (rc != ATN_OK || atn_tun_send(hub_tun, chunk, cn) != ATN_OK) {
            atn_voice_wipe(v);
            return ATN_ERR_STATE;
        }
        atn_memzero(chunk, cn);
    }
    return ATN_OK;
}

int atn_voice_accept_relay(atn_voice *v,
                           const uint8_t own_dk[ATN_MLKEM1024_DK_LEN])
{
    int rc;
    uint8_t wire[ATN_VOICE_CTRL_LEN];
    size_t wn = 0;
    uint8_t ct[ATN_MLKEM1024_CT_LEN];
    if (v == NULL || own_dk == NULL || v->tun == NULL) {
        return ATN_ERR_PARAM;
    }
    if (v->state != ATN_VOICE_RINGING && v->state != ATN_VOICE_CONNECTING) {
        return ATN_ERR_STATE;
    }
    memcpy(ct, v->e2e.ct, ATN_MLKEM1024_CT_LEN);
    rc = atn_voice_e2e_responder(&v->e2e, own_dk, ct);
    atn_memzero(ct, sizeof(ct));
    if (rc != ATN_OK) {
        return rc;
    }
    v->path = ATN_VOICE_PATH_RELAY;
    v->state = ATN_VOICE_CONNECTING;
    if (atn_voice_encode_ctrl(ATN_VOICE_OP_ACCEPT, v->codec, v->call_id, wire,
                              sizeof(wire), &wn) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    if (v->tun->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    rc = atn_tun_send(v->tun, wire, wn);
    if (rc == ATN_OK) {
        v->state = ATN_VOICE_ACTIVE;
    }
    return rc;
}
