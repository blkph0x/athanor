/*
 * Lab node responder (REQ-4.1 / DEC-0021 / 0031 / 0032).
 *   atnnode demo
 *   atnnode listen [port]
 *   atnnode connect <atn-node.conf>
 *
 * connect walks hub2..hub16 on AUTH/timeout (DEC-0031). listen is one hub.
 * Prints peer_port + peer_ek. Operator fills peer_ipv4. No LAN guess.
 */
#include "atn_cfg.h"
#include "atn_compromise.h"
#include "atn_crypto.h"
#include "atn_dmon.h"
#include "atn_platform.h"
#include "atn_policy.h"
#include "atn_tun.h"
#include "atn_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Pace UC chunks so phone 1Hz drain / WAN UDP can keep up (DEC-0048). */
static void hub_upd_pace(void)
{
#if defined(_WIN32)
    Sleep(20);
#else
    usleep(20000);
#endif
}

static void print_hex(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
}

static int cmd_demo(void)
{
    atn_tun resp, init;
    atn_cfg c;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t hello[4], back[64];
    char text[80 + ATN_MLKEM1024_EK_LEN * 2u];
    char hdr[64];
    size_t n, i, nrecv = 0;
    int rc;

    if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
        return 1;
    }
    if (atn_tun_init_responder(&resp, dk) != ATN_OK) {
        return 1;
    }
    atn_memzero(dk, sizeof(dk));
    if (atn_tun_bind_any(&resp, 0) != ATN_OK || resp.local_port == 0) {
        atn_tun_wipe(&resp);
        return 1;
    }
    {
        int hn = sprintf(hdr, "peer_ipv4=127.0.0.1\npeer_port=%u\npeer_ek=",
                         (unsigned)resp.local_port);
        if (hn < 0) {
            atn_tun_wipe(&resp);
            return 1;
        }
        memcpy(text, hdr, (size_t)hn);
        n = (size_t)hn;
    }
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        static const char hex[] = "0123456789abcdef";
        text[n++] = hex[ek[i] >> 4];
        text[n++] = hex[ek[i] & 15u];
    }
    if (atn_cfg_parse(text, n, &c) != ATN_OK || !atn_cfg_ready(&c) ||
        c.port != resp.local_port || c.ipv4_host != 0x7f000001u) {
        fprintf(stderr, "cfg roundtrip failed\n");
        atn_tun_wipe(&resp);
        return 1;
    }
    if (atn_tun_init_initiator(&init, c.ek) != ATN_OK ||
        atn_tun_bind(&init, 0) != ATN_OK ||
        atn_tun_set_peer(&init, c.ipv4_host, c.port) != ATN_OK) {
        atn_tun_wipe(&init);
        atn_tun_wipe(&resp);
        return 1;
    }
    if (atn_tun_hs_send_init(&init) != ATN_OK) {
        atn_tun_wipe(&init);
        atn_tun_wipe(&resp);
        return 1;
    }
    {
        int i;
        rc = ATN_ERR_STATE;
        for (i = 0; i < 16 && resp.state != ATN_TUN_ESTABLISHED; i++) {
            rc = atn_tun_pump(&resp, 3000);
        }
    }
    if (rc != ATN_OK || atn_tun_pump(&init, 3000) != ATN_OK ||
        init.state != ATN_TUN_ESTABLISHED ||
        resp.state != ATN_TUN_ESTABLISHED) {
        fprintf(stderr, "handshake failed\n");
        atn_tun_wipe(&init);
        atn_tun_wipe(&resp);
        return 1;
    }
    memcpy(hello, "lab!", 4);
    if (atn_tun_send(&init, hello, 4) != ATN_OK ||
        atn_tun_recv_data(&resp, back, &nrecv, sizeof(back), 3000) != ATN_OK ||
        nrecv != 4 || memcmp(back, hello, 4) != 0) {
        fprintf(stderr, "echo failed\n");
        atn_tun_wipe(&init);
        atn_tun_wipe(&resp);
        return 1;
    }
    atn_tun_wipe(&init);
    atn_tun_wipe(&resp);
    printf("atnnode demo: conf handshake + echo OK (DEC-0023)\n");
    return 0;
}

/*
 * Purpose:  Hub listen: HS + echo LAB + push org policy (DEC-0045) +
 *           compromise vote boom (DEC-0047) + mesh update (DEC-0048).
 * Spec:     Policy file lab/org-policy.conf (override: ATN_ORG_POLICY).
 *           Compromise file lab/compromise-vote.conf (ATN_COMPROMISE).
 *           Update announce lab/updates/announce.conf (ATN_UPDATE_ANNOUNCE).
 *           Push on ESTABLISHED, on phone 'P'/'P?'/'U?' request, and when
 *           file contents change (admin website writes the file).
 *           Update bytes ONLY on tunnel DATA ('U' / 'U''C') — never HTTP.
 */
static const char *org_policy_path(void)
{
    const char *e = getenv("ATN_ORG_POLICY");
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return "lab/org-policy.conf";
}

static const char *compromise_path(void)
{
    const char *e = getenv("ATN_COMPROMISE");
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return "lab/compromise-vote.conf";
}

static const char *update_announce_path(void)
{
    const char *e = getenv("ATN_UPDATE_ANNOUNCE");
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return "lab/updates/announce.conf";
}

static const char *hub_peers_path(void)
{
    const char *e = getenv("ATN_HUB_PEERS");
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return "lab/hub-peers.conf";
}

/*
 * Purpose:  Persist hub ML-KEM1024 ek/dk across process restarts.
 * Spec:     ATN_HUB_KEYS or lab/hub-mlkem.keys (gitignored). Text lines
 *           ek=<3136 hex> dk=<6336 hex>. First start keygens + saves.
 */
static const char *hub_keys_path(void)
{
    const char *e = getenv("ATN_HUB_KEYS");
    if (e != NULL && e[0] != '\0') {
        return e;
    }
    return "lab/hub-mlkem.keys";
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

static int parse_hex_buf(const char *hex, size_t hex_len, uint8_t *out,
                         size_t out_len)
{
    size_t i;
    if (hex == NULL || out == NULL || hex_len != out_len * 2u) {
        return ATN_ERR_PARAM;
    }
    for (i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return ATN_ERR_PARAM;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return ATN_OK;
}

static int hub_keys_load(uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    FILE *f;
    char line[8192];
    int got_ek = 0;
    int got_dk = 0;
    const char *path = hub_keys_path();

    f = fopen(path, "rb");
    if (f == NULL) {
        return ATN_ERR_SOCK;
    }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1u] == '\n' || line[n - 1u] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0 || line[0] == '#') {
            continue;
        }
        if (n > 3 && line[0] == 'e' && line[1] == 'k' && line[2] == '=') {
            if (parse_hex_buf(line + 3, n - 3u, ek, ATN_MLKEM1024_EK_LEN) !=
                ATN_OK) {
                fclose(f);
                return ATN_ERR_PARAM;
            }
            got_ek = 1;
        } else if (n > 3 && line[0] == 'd' && line[1] == 'k' &&
                   line[2] == '=') {
            if (parse_hex_buf(line + 3, n - 3u, dk, ATN_MLKEM1024_DK_LEN) !=
                ATN_OK) {
                fclose(f);
                return ATN_ERR_PARAM;
            }
            got_dk = 1;
        }
    }
    fclose(f);
    if (!got_ek || !got_dk) {
        return ATN_ERR_PARAM;
    }
    return ATN_OK;
}

static int hub_keys_save(const uint8_t ek[ATN_MLKEM1024_EK_LEN],
                         const uint8_t dk[ATN_MLKEM1024_DK_LEN])
{
    FILE *f;
    size_t i;
    const char *path = hub_keys_path();

#if defined(_WIN32)
    (void)_mkdir("lab");
#else
    (void)mkdir("lab", 0755);
#endif
    f = fopen(path, "wb");
    if (f == NULL) {
        return ATN_ERR_SOCK;
    }
    fputs("ek=", f);
    for (i = 0; i < ATN_MLKEM1024_EK_LEN; i++) {
        fprintf(f, "%02x", ek[i]);
    }
    fputs("\ndk=", f);
    for (i = 0; i < ATN_MLKEM1024_DK_LEN; i++) {
        fprintf(f, "%02x", dk[i]);
    }
    fputc('\n', f);
    if (fclose(f) != 0) {
        return ATN_ERR_SOCK;
    }
    return ATN_OK;
}

static int update_same(const atn_update *a, const atn_update *b)
{
    return a->update_id == b->update_id && a->kind == b->kind &&
           a->size == b->size && a->chunk_size == b->chunk_size &&
           strcmp(a->version, b->version) == 0 &&
           strcmp(a->sha256_hex, b->sha256_hex) == 0 &&
           strcmp(a->payload_path, b->payload_path) == 0;
}

/*
 * Purpose:  Push announce + all payload chunks on an ESTABLISHED tunnel.
 * Spec:     DEC-0048. Used for phone nodes and peer hubs.
 */
static int hub_stream_update(atn_tun *t, const atn_update *u)
{
    uint8_t wire[ATN_TUN_MAX_PT];
    uint8_t chunk[ATN_UPD_CHUNK_MAX];
    size_t wn = 0;
    FILE *f;
    uint32_t off = 0;
    size_t got;
    int rc;

    if (t == NULL || u == NULL || t->state != ATN_TUN_ESTABLISHED) {
        return ATN_ERR_STATE;
    }
    if (u->update_id == 0 || u->size == 0 || u->sha256_hex[0] == '\0') {
        return ATN_ERR_PARAM;
    }
    if (atn_update_encode_announce_wire(u, wire, sizeof(wire), &wn) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_send(t, wire, wn);
    atn_memzero(wire, sizeof(wire));
    if (rc != ATN_OK) {
        return rc;
    }
    printf("update_announce id=%u kind=%u size=%u ver=%s\n",
           (unsigned)u->update_id, (unsigned)u->kind, (unsigned)u->size,
           u->version);
    fflush(stdout);
    f = fopen(u->payload_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "update payload missing: %s\n", u->payload_path);
        return ATN_ERR_SOCK;
    }
    while (off < u->size) {
        uint16_t want = (uint16_t)u->chunk_size;
        if ((uint32_t)want > u->size - off) {
            want = (uint16_t)(u->size - off);
        }
        got = fread(chunk, 1, want, f);
        if (got != (size_t)want) {
            fclose(f);
            return ATN_ERR_SOCK;
        }
        if (atn_update_encode_chunk_wire(u->update_id, off, chunk, want, wire,
                                         sizeof(wire), &wn) != ATN_OK) {
            fclose(f);
            return ATN_ERR_PARAM;
        }
        rc = atn_tun_send(t, wire, wn);
        atn_memzero(wire, sizeof(wire));
        atn_memzero(chunk, sizeof(chunk));
        if (rc != ATN_OK) {
            fclose(f);
            return rc;
        }
        off += want;
        hub_upd_pace();
    }
    fclose(f);
    printf("update_stream_done id=%u bytes=%u\n", (unsigned)u->update_id,
           (unsigned)u->size);
    fflush(stdout);
    return ATN_OK;
}

static int parse_ek_hex(const char *hex, uint8_t ek[ATN_MLKEM1024_EK_LEN])
{
    size_t n;
    if (hex == NULL) {
        return ATN_ERR_PARAM;
    }
    n = strlen(hex);
    return parse_hex_buf(hex, n, ek, ATN_MLKEM1024_EK_LEN);
}

/*
 * Purpose:  Push current update to peer hubs listed in lab/hub-peers.conf.
 * Spec:     DEC-0048. Line format: ipv4 port ek_hex.
 *           Each peer gets an initiator PQ/AEAD tunnel; chunks ride TUN_DATA
 *           only (no cleartext file copy / HTTP).
 */
static void hub_fanout_update_peers(const atn_update *u)
{
    FILE *f;
    char line[8192];
    const char *path = hub_peers_path();

    if (u == NULL || u->update_id == 0) {
        return;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    printf("update_fanout peers file %s (tunnel-only)\n", path);
    fflush(stdout);
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        char ip[64];
        unsigned port = 0;
        char *ekhex;
        uint8_t ek[ATN_MLKEM1024_EK_LEN];
        atn_tun peer;
        uint32_t ipv4 = 0;
        unsigned a = 0, b = 0, c = 0, d = 0;
        int attempt;
        int rc;
        char *sp1;
        char *sp2;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        /* Format: ipv4 port ek_hex (ek is 3136 hex chars). */
        sp1 = strchr(line, ' ');
        if (sp1 == NULL) {
            continue;
        }
        *sp1 = '\0';
        if (strlen(line) >= sizeof(ip)) {
            continue;
        }
        memcpy(ip, line, strlen(line) + 1u);
        while (*++sp1 == ' ') {
        }
        port = (unsigned)strtoul(sp1, &sp2, 10);
        if (sp2 == sp1 || port == 0 || port > 65535u) {
            continue;
        }
        while (*sp2 == ' ' || *sp2 == '\t') {
            sp2++;
        }
        ekhex = sp2;
        {
            size_t el = strlen(ekhex);
            while (el > 0 && (ekhex[el - 1u] == '\n' || ekhex[el - 1u] == '\r' ||
                              ekhex[el - 1u] == ' ')) {
                ekhex[--el] = '\0';
            }
        }
        if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255u ||
            b > 255u || c > 255u || d > 255u) {
            continue;
        }
        ipv4 = (a << 24) | (b << 16) | (c << 8) | d;
        if (parse_ek_hex(ekhex, ek) != ATN_OK) {
            fprintf(stderr, "hub-peers bad ek for %s\n", ip);
            continue;
        }
        if (atn_tun_init_initiator(&peer, ek) != ATN_OK) {
            continue;
        }
        if (atn_tun_bind(&peer, 0) != ATN_OK ||
            atn_tun_set_peer(&peer, ipv4, (uint16_t)port) != ATN_OK ||
            atn_tun_hs_send_init(&peer) != ATN_OK) {
            atn_tun_wipe(&peer);
            continue;
        }
        for (attempt = 0; attempt < 20; attempt++) {
            rc = atn_tun_pump(&peer, 500);
            if (peer.state == ATN_TUN_ESTABLISHED) {
                break;
            }
            if (rc != ATN_OK && rc != ATN_ERR_STATE) {
                break;
            }
            if (peer.state == ATN_TUN_HANDSHAKE) {
                (void)atn_tun_hs_retry(&peer);
            }
        }
        if (peer.state == ATN_TUN_ESTABLISHED) {
            printf("update_fanout ESTABLISHED %s:%u\n", ip, port);
            fflush(stdout);
            (void)hub_stream_update(&peer, u);
        } else {
            fprintf(stderr, "update_fanout failed %s:%u\n", ip, port);
        }
        atn_tun_wipe(&peer);
        atn_memzero(ek, sizeof(ek));
    }
    fclose(f);
}

/* Inbound peer-hub update staging (tunnel DATA only). */
typedef struct {
    int      active;
    uint32_t update_id;
    uint32_t size;
    uint32_t got;
    uint8_t  kind;
    char     version[ATN_UPD_VER_MAX];
    char     sha256_hex[ATN_UPD_SHA_HEX + 1u];
    FILE    *f;
} hub_upd_rx;

static void hub_upd_rx_close(hub_upd_rx *rx)
{
    if (rx == NULL) {
        return;
    }
    if (rx->f != NULL) {
        fclose(rx->f);
        rx->f = NULL;
    }
    rx->active = 0;
}

static int hub_upd_rx_begin(hub_upd_rx *rx, const atn_update *u)
{
    if (rx == NULL || u == NULL || u->update_id == 0 || u->size == 0) {
        return ATN_ERR_PARAM;
    }
    hub_upd_rx_close(rx);
    {
#if defined(_WIN32)
        (void)_mkdir("lab");
        (void)_mkdir("lab\\updates");
#else
        (void)mkdir("lab", 0755);
        (void)mkdir("lab/updates", 0755);
#endif
    }
    rx->f = fopen("lab/updates/payload.recv", "wb");
    if (rx->f == NULL) {
        return ATN_ERR_SOCK;
    }
    rx->active = 1;
    rx->update_id = u->update_id;
    rx->size = u->size;
    rx->got = 0;
    rx->kind = u->kind;
    memcpy(rx->version, u->version, ATN_UPD_VER_MAX);
    memcpy(rx->sha256_hex, u->sha256_hex, ATN_UPD_SHA_HEX + 1u);
    return ATN_OK;
}

static int hub_upd_rx_chunk(hub_upd_rx *rx, uint32_t off, const uint8_t *data,
                            uint16_t len)
{
    if (rx == NULL || !rx->active || rx->f == NULL || data == NULL || len == 0) {
        return ATN_ERR_PARAM;
    }
    if (off != rx->got || (uint32_t)len > rx->size - rx->got) {
        return ATN_ERR_PARAM;
    }
    if (fwrite(data, 1, len, rx->f) != (size_t)len) {
        return ATN_ERR_SOCK;
    }
    rx->got += len;
    return ATN_OK;
}

/*
 * Purpose:  Finalize inbound peer update: verify SHA-256, promote payload,
 *           write announce.conf, refresh in-memory upd (no fan-out — avoids
 *           hub A↔B loops). Delivery path was tunnel-only.
 */
static int hub_upd_rx_finish(hub_upd_rx *rx, atn_update *upd)
{
    char hex[ATN_UPD_SHA_HEX + 1u];
    uint32_t sz = 0;
    atn_update out;

    if (rx == NULL || upd == NULL || !rx->active || rx->got != rx->size) {
        return ATN_ERR_PARAM;
    }
    if (rx->f != NULL) {
        fclose(rx->f);
        rx->f = NULL;
    }
    if (atn_update_file_sha256_hex("lab/updates/payload.recv", hex, &sz) !=
            ATN_OK ||
        sz != rx->size || strcmp(hex, rx->sha256_hex) != 0) {
        remove("lab/updates/payload.recv");
        hub_upd_rx_close(rx);
        fprintf(stderr, "update_rx hash mismatch id=%u\n",
                (unsigned)rx->update_id);
        return ATN_ERR_PARAM;
    }
    remove("lab/updates/payload.bin");
    if (rename("lab/updates/payload.recv", "lab/updates/payload.bin") != 0) {
        remove("lab/updates/payload.recv");
        hub_upd_rx_close(rx);
        return ATN_ERR_SOCK;
    }
    atn_update_init(&out);
    out.update_id = rx->update_id;
    out.kind = rx->kind;
    memcpy(out.version, rx->version, ATN_UPD_VER_MAX);
    memcpy(out.sha256_hex, rx->sha256_hex, ATN_UPD_SHA_HEX + 1u);
    out.size = rx->size;
    if (atn_update_save_file(update_announce_path(), &out) != ATN_OK) {
        hub_upd_rx_close(rx);
        return ATN_ERR_SOCK;
    }
    *upd = out;
    hub_upd_rx_close(rx);
    printf("update_rx complete id=%u size=%u (tunnel)\n",
           (unsigned)upd->update_id, (unsigned)upd->size);
    fflush(stdout);
    return ATN_OK;
}

static int policy_same(const atn_policy *a, const atn_policy *b)
{
    return a->ver == b->ver && a->diag == b->diag &&
           a->flush_mode == b->flush_mode && a->wipe_armed == b->wipe_armed &&
           a->outage_class == b->outage_class &&
           a->boom_silence_s == b->boom_silence_s &&
           a->password_fail_max == b->password_fail_max &&
           a->biometric_allowed == b->biometric_allowed &&
           a->password_min_len == b->password_min_len &&
           a->usb_data_block == b->usb_data_block &&
           a->pwd_deny_check == b->pwd_deny_check;
}

static int compromise_same(const atn_compromise *a, const atn_compromise *b)
{
    return a->vote_id == b->vote_id && a->state == b->state &&
           a->yes == b->yes && a->no == b->no && a->quorum == b->quorum &&
           a->timeout_s == b->timeout_s && a->opened_unix == b->opened_unix &&
           strcmp(a->target_label, b->target_label) == 0;
}

static int hub_send_policy(atn_tun *t, const atn_policy *pol)
{
    uint8_t wire[ATN_POLICY_MAX_WIRE];
    size_t wn = 0;
    int rc;
    if (atn_policy_encode_wire(pol, wire, sizeof(wire), &wn) != ATN_OK) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_send(t, wire, wn);
    atn_memzero(wire, sizeof(wire));
    if (rc == ATN_OK) {
        printf("policy_push ver=%u\n", (unsigned)pol->ver);
        fflush(stdout);
    }
    return rc;
}

static int hub_send_compromise(atn_tun *t, const atn_compromise *c, uint8_t act)
{
    uint8_t wire[ATN_COMP_MAX_WIRE];
    size_t wn = 0;
    int rc;
    if (atn_compromise_encode_wire(c, act, wire, sizeof(wire), &wn) !=
        ATN_OK) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_send(t, wire, wn);
    atn_memzero(wire, sizeof(wire));
    if (rc == ATN_OK) {
        printf("compromise_push act=%u vote_id=%u state=%u\n", (unsigned)act,
               (unsigned)c->vote_id, (unsigned)c->state);
        fflush(stdout);
    }
    return rc;
}

static uint32_t unix_now(void)
{
    time_t t = time(NULL);
    if (t < 0) {
        return 0;
    }
    return (uint32_t)t;
}

/*
 * Purpose:  Advance open vote → boom_pending on quorum or timeout (DEC-0047).
 * Spec:     Fail-closed: timeout always booms; NO does not cancel.
 */
static void hub_compromise_tick(atn_compromise *c, const char *cpath,
                                int *need_boom)
{
    uint32_t now = unix_now();
    if (c == NULL || cpath == NULL || need_boom == NULL) {
        return;
    }
    if (c->state == ATN_COMP_STATE_OPEN) {
        if (atn_compromise_quorum_met(c) || atn_compromise_timed_out(c, now)) {
            c->state = ATN_COMP_STATE_BOOM_PENDING;
            (void)atn_compromise_save_file(cpath, c);
            *need_boom = 1;
            printf("compromise_boom_pending vote_id=%u yes=%u quorum=%u\n",
                   (unsigned)c->vote_id, (unsigned)c->yes,
                   (unsigned)c->quorum);
            fflush(stdout);
        }
    } else if (c->state == ATN_COMP_STATE_BOOM_PENDING) {
        *need_boom = 1;
    }
}

static int cmd_listen(uint16_t port)
{
    atn_tun t;
    atn_policy pol, pol_new;
    atn_compromise comp, comp_new;
    atn_update upd, upd_new;
    hub_upd_rx urx;
    uint8_t ek[ATN_MLKEM1024_EK_LEN], dk[ATN_MLKEM1024_DK_LEN];
    uint8_t pt[ATN_TUN_MAX_PT];
    const char *ppath = org_policy_path();
    const char *cpath = compromise_path();
    const char *upath = update_announce_path();
    uint16_t listen_port = port;
    int rc;

    memset(&urx, 0, sizeof(urx));
    if (hub_keys_load(ek, dk) == ATN_OK) {
        printf("# hub keys loaded from %s\n", hub_keys_path());
    } else {
        if (atn_mlkem1024_keygen(ek, dk) != ATN_OK) {
            return 1;
        }
        if (hub_keys_save(ek, dk) != ATN_OK) {
            fprintf(stderr, "hub keys save failed: %s\n", hub_keys_path());
            atn_memzero(dk, sizeof(dk));
            atn_memzero(ek, sizeof(ek));
            return 1;
        }
        printf("# hub keys generated + saved to %s\n", hub_keys_path());
    }
    fflush(stdout);
    if (atn_tun_init_responder(&t, dk) != ATN_OK) {
        atn_memzero(dk, sizeof(dk));
        return 1;
    }
    if (atn_tun_bind_any(&t, listen_port) != ATN_OK) {
        fprintf(stderr, "bind_any failed\n");
        atn_memzero(dk, sizeof(dk));
        atn_tun_wipe(&t);
        return 1;
    }
    listen_port = t.local_port;
    (void)atn_policy_load_file(ppath, &pol);
    (void)atn_compromise_load_file(cpath, &comp);
    (void)atn_update_load_file(upath, &upd);
    printf("# atn-node.conf (set peer_ipv4 to this machine)\n");
    printf("peer_port=%u\n", (unsigned)t.local_port);
    printf("peer_ek=");
    print_hex(ek, ATN_MLKEM1024_EK_LEN);
    printf("\n");
    printf("# org policy %s ver=%u (DEC-0045)\n", ppath, (unsigned)pol.ver);
    printf("# compromise %s vote_id=%u state=%u (DEC-0047)\n", cpath,
           (unsigned)comp.vote_id, (unsigned)comp.state);
    printf("# update %s id=%u (DEC-0048 tunnel-only)\n", upath,
           (unsigned)upd.update_id);
    fflush(stdout);

    /* Outer re-arm: same peer_ek after CLOSED (multi-session still deferred). */
    for (;;) {
        int need_push = 0;
        int need_comp_open = 0;
        int need_comp_boom = 0;
        int need_update = 0;

        for (;;) {
            rc = atn_tun_pump(&t, 1000);
            if (t.state == ATN_TUN_ESTABLISHED) {
                printf("ESTABLISHED\n");
                fflush(stdout);
                need_push = 1;
                if (upd.update_id > 0u && upd.size > 0u) {
                    need_update = 1;
                }
                if (comp.state == ATN_COMP_STATE_OPEN) {
                    need_comp_open = 1;
                }
                if (comp.state == ATN_COMP_STATE_BOOM_PENDING) {
                    need_comp_boom = 1;
                }
                break;
            }
            if (rc != ATN_OK && rc != ATN_ERR_STATE) {
                /*
                 * AUTH/NONCE/LEN/PARAM during wait: log and keep listening.
                 * CLOSED → re-arm below (do not kill hub process).
                 */
                fprintf(stderr, "pump failed %d (transient; continue)\n", rc);
                fflush(stderr);
            }
            if (t.state == ATN_TUN_CLOSED) {
                printf("CLOSED — re-arm listen port=%u (same peer_ek)\n",
                       (unsigned)listen_port);
                fflush(stdout);
                hub_upd_rx_close(&urx);
                atn_tun_wipe(&t);
                if (atn_tun_init_responder(&t, dk) != ATN_OK ||
                    atn_tun_bind_any(&t, listen_port) != ATN_OK) {
                    fprintf(stderr, "re-arm bind failed\n");
                    atn_memzero(dk, sizeof(dk));
                    return 1;
                }
                continue; /* wait for next ESTABLISHED */
            }
            /* Still wait HS: reload announce so idle hub sees admin publish. */
            if (atn_update_load_file(upath, &upd_new) == ATN_OK &&
                !update_same(&upd, &upd_new)) {
                upd = upd_new;
                printf("update_reload id=%u (idle; fanout peers)\n",
                       (unsigned)upd.update_id);
                fflush(stdout);
                if (upd.update_id > 0u && upd.size > 0u) {
                    hub_fanout_update_peers(&upd);
                }
            }
        }

        for (;;) {
            size_t n = 0;
            /* Reload policy file each tick (admin website is source of truth). */
            if (atn_policy_load_file(ppath, &pol_new) == ATN_OK &&
                !policy_same(&pol, &pol_new)) {
                pol = pol_new;
                need_push = 1;
                printf("policy_reload ver=%u\n", (unsigned)pol.ver);
                fflush(stdout);
            }
            if (atn_compromise_load_file(cpath, &comp_new) == ATN_OK &&
                !compromise_same(&comp, &comp_new)) {
                uint8_t prev = comp.state;
                comp = comp_new;
                printf("compromise_reload vote_id=%u state=%u yes=%u\n",
                       (unsigned)comp.vote_id, (unsigned)comp.state,
                       (unsigned)comp.yes);
                fflush(stdout);
                if (comp.state == ATN_COMP_STATE_OPEN &&
                    prev != ATN_COMP_STATE_OPEN) {
                    need_comp_open = 1;
                }
                if (comp.state == ATN_COMP_STATE_CLEARED) {
                    need_comp_open = 0;
                    need_comp_boom = 0;
                    (void)hub_send_compromise(&t, &comp, ATN_COMP_ACT_CLEAR);
                }
                if (comp.state == ATN_COMP_STATE_BOOM_PENDING) {
                    need_comp_boom = 1;
                }
            }
            if (atn_update_load_file(upath, &upd_new) == ATN_OK &&
                !update_same(&upd, &upd_new)) {
                upd = upd_new;
                need_update = 1;
                printf("update_reload id=%u (tunnel push + peer fanout)\n",
                       (unsigned)upd.update_id);
                fflush(stdout);
                if (upd.update_id > 0u && upd.size > 0u) {
                    hub_fanout_update_peers(&upd);
                }
            }
            hub_compromise_tick(&comp, cpath, &need_comp_boom);
            if (need_push && t.state == ATN_TUN_ESTABLISHED) {
                (void)hub_send_policy(&t, &pol);
                need_push = 0;
            }
            if (need_comp_open && t.state == ATN_TUN_ESTABLISHED &&
                comp.state == ATN_COMP_STATE_OPEN) {
                (void)hub_send_compromise(&t, &comp, ATN_COMP_ACT_OPEN);
                need_comp_open = 0;
            }
            if (need_comp_boom && t.state == ATN_TUN_ESTABLISHED &&
                (comp.state == ATN_COMP_STATE_BOOM_PENDING ||
                 comp.state == ATN_COMP_STATE_DONE)) {
                if (hub_send_compromise(&t, &comp, ATN_COMP_ACT_BOOM) ==
                    ATN_OK) {
                    if (comp.state == ATN_COMP_STATE_BOOM_PENDING) {
                        comp.state = ATN_COMP_STATE_DONE;
                        (void)atn_compromise_save_file(cpath, &comp);
                    }
                    need_comp_boom = 0;
                }
            }
            if (need_update && t.state == ATN_TUN_ESTABLISHED &&
                upd.update_id > 0u && upd.size > 0u) {
                (void)hub_stream_update(&t, &upd);
                need_update = 0;
            }
            /* recv_data also accepts HS_INIT (phone WiFi↔5G / reconnect). */
            rc = atn_tun_recv_data(&t, pt, &n, sizeof(pt), 1000);
            if (rc == ATN_OK && n > 0) {
                printf("recv %u\n", (unsigned)n);
                fflush(stdout);
                if (pt[0] == ATN_POLICY_WIRE) {
                    atn_policy tmp;
                    int pr = atn_policy_parse_wire(pt, n, &tmp);
                    if (pr == ATN_ERR_STATE) {
                        /* Phone asked for current org policy. */
                        need_push = 1;
                    }
                    /* Hub never applies phone-authored policy. */
                } else if (pt[0] == ATN_COMP_WIRE) {
                    atn_compromise tmp;
                    if (atn_compromise_parse_wire(pt, n, &tmp) == ATN_OK &&
                        comp.state == ATN_COMP_STATE_OPEN &&
                        tmp.vote_id == comp.vote_id) {
                        if (tmp.action == ATN_COMP_ACT_VOTE_YES) {
                            comp.yes++;
                            (void)atn_compromise_save_file(cpath, &comp);
                            printf("compromise_vote yes=%u\n",
                                   (unsigned)comp.yes);
                            fflush(stdout);
                        } else if (tmp.action == ATN_COMP_ACT_VOTE_NO) {
                            comp.no++;
                            (void)atn_compromise_save_file(cpath, &comp);
                            printf("compromise_vote no=%u\n",
                                   (unsigned)comp.no);
                            fflush(stdout);
                        }
                    }
                } else if (pt[0] == ATN_UPD_WIRE) {
                    atn_update tmp;
                    uint32_t coff = 0;
                    uint16_t clen = 0;
                    const uint8_t *cdata = NULL;
                    int ur = atn_update_parse_wire(pt, n, &tmp, &coff, &clen,
                                                   &cdata);
                    if (ur == ATN_ERR_STATE) {
                        /* Phone/peer asked for current update (U / U?). */
                        need_update = 1;
                    } else if (ur == ATN_OK &&
                               tmp.action == ATN_UPD_ACT_ANNOUNCE) {
                        if (hub_upd_rx_begin(&urx, &tmp) != ATN_OK) {
                            fprintf(stderr, "update_rx begin fail\n");
                        }
                    } else if (ur == ATN_OK &&
                               tmp.action == ATN_UPD_ACT_CHUNK) {
                        if (hub_upd_rx_chunk(&urx, coff, cdata, clen) !=
                            ATN_OK) {
                            fprintf(stderr, "update_rx chunk fail\n");
                            hub_upd_rx_close(&urx);
                        } else if (urx.active && urx.got == urx.size) {
                            if (hub_upd_rx_finish(&urx, &upd) == ATN_OK) {
                                /* Saved locally; next phone ESTABLISHED
                                 * or U? gets tunnel stream. No fan-out. */
                                ;
                            }
                        }
                    }
                } else if (n >= 1 && pt[0] == 0x41u /* 'A' DEC-0050 voice */) {
                    /*
                     * Opaque forward/echo of family 'A' (CONTROL / SEALED /
                     * AUDIO). Lab single-session: echo enables loopback.
                     * Product relay: hub MUST forward without attempting to
                     * decode 'A''S' (nested E2E). Multi-peer A↔Hub↔B fan-out
                     * still deferred on this listen path.
                     */
                    {
                        unsigned subtype = (n >= 2) ? (unsigned)pt[1] : 0u;
                        printf("voice_frame n=%u subtype=%u\n",
                               (unsigned)n, subtype);
                        fflush(stdout);
                    }
                    (void)atn_tun_send(&t, pt, n);
                } else {
                    (void)atn_tun_send(&t, pt, n); /* LAB echo / other */
                }
                atn_memzero(pt, n);
            } else if (rc == ATN_OK && n == 0) {
                /* KA or HS re-pin with no DATA payload */
                ;
            } else if (rc == ATN_ERR_STATE) {
                (void)atn_tun_keepalive(&t);
            } else if (rc != ATN_OK) {
                /*
                 * AUTH closes tunnel → CLOSED re-arm below.
                 * NONCE/LEN/PARAM (bad/replay frame during voice flood):
                 * log and stay ESTABLISHED — never exit hub process.
                 */
                fprintf(stderr,
                        "recv failed %d (AUTH/NONCE/transient; continue)\n",
                        rc);
                fflush(stderr);
            }
            if (t.state == ATN_TUN_CLOSED) {
                printf("CLOSED — re-arm listen port=%u (same peer_ek)\n",
                       (unsigned)listen_port);
                fflush(stdout);
                hub_upd_rx_close(&urx);
                atn_tun_wipe(&t);
                if (atn_tun_init_responder(&t, dk) != ATN_OK ||
                    atn_tun_bind_any(&t, listen_port) != ATN_OK) {
                    fprintf(stderr, "re-arm bind failed\n");
                    atn_memzero(dk, sizeof(dk));
                    return 1;
                }
                break; /* outer loop waits for next ESTABLISHED */
            }
        }
    }
}

/*
 * Purpose:  Lab initiator with DEC-0031 hub failover (two-process soak).
 * Spec:     Walk hubs; peer listen process pumps ACK on the live hub.
 */
static int cmd_connect(const char *path)
{
    atn_cfg c;
    atn_dmon d;
    uint8_t dk[32], ck[32], hello[4], back[64];
    size_t n = 0;
    unsigned count, hub, attempt;
    int rc;
    int established = 0;

    if (atn_cfg_load_file(path, &c) != ATN_OK || !atn_cfg_ready(&c)) {
        fprintf(stderr, "conf not ready\n");
        return 1;
    }
    count = atn_cfg_hub_count(&c);
    if (count == 0) {
        fprintf(stderr, "no hubs\n");
        return 1;
    }
    if (atn_random_bytes(dk, 32) != ATN_OK ||
        atn_random_bytes(ck, 32) != ATN_OK) {
        return 1;
    }
    atn_dmon_init(&d);
    if (atn_dmon_load(&d, dk, ck) != ATN_OK) {
        return 1;
    }
    atn_memzero(dk, sizeof(dk));
    atn_memzero(ck, sizeof(ck));

    for (hub = 0; hub < count && !established; hub++) {
        rc = atn_dmon_tun_connect_hub(&d, &c, hub);
        if (rc != ATN_OK) {
            continue;
        }
        for (attempt = 0; attempt < ATN_DMON_HUB_HS_ATTEMPTS; attempt++) {
            if (atn_dmon_tun_state(&d) == ATN_TUN_ESTABLISHED) {
                established = 1;
                break;
            }
            rc = atn_dmon_tun_pump(&d, 1000);
            if (atn_dmon_tun_state(&d) == ATN_TUN_ESTABLISHED) {
                established = 1;
                break;
            }
            if (rc == ATN_ERR_AUTH || d.tun.state == ATN_TUN_CLOSED) {
                break;
            }
            if (d.tun.state == ATN_TUN_HANDSHAKE) {
                (void)atn_dmon_tun_hs_retry(&d);
            }
        }
    }
    if (!established) {
        fprintf(stderr, "connect failed all %u hubs\n", count);
        atn_dmon_flush(&d);
        return 1;
    }

    memcpy(hello, "lab!", 4);
    if (atn_dmon_tun_send(&d, hello, 4) != ATN_OK ||
        atn_dmon_tun_recv(&d, back, &n, sizeof(back), 3000) != ATN_OK ||
        n != 4 || memcmp(back, hello, 4) != 0) {
        fprintf(stderr, "echo failed\n");
        atn_dmon_flush(&d);
        return 1;
    }
    printf("atnnode connect: echo OK hub=%u of %u (DEC-0031)\n",
           atn_dmon_hub_idx(&d), count);
    atn_dmon_flush(&d);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: atnnode demo|listen [port]|connect <file>\n");
        return 1;
    }
    if (strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (strcmp(argv[1], "listen") == 0) {
        unsigned port = 2402;
        if (argc == 3) {
            unsigned i, v = 0;
            if (argv[2][0] == 0) {
                return 1;
            }
            for (i = 0; argv[2][i] != 0; i++) {
                if (argv[2][i] < '0' || argv[2][i] > '9') {
                    return 1;
                }
                v = v * 10u + (unsigned)(argv[2][i] - '0');
                if (v > 65535u) {
                    return 1;
                }
            }
            if (v < 1u) {
                return 1;
            }
            port = v;
        } else if (argc != 2) {
            return 1;
        }
        return cmd_listen((uint16_t)port);
    }
    if (strcmp(argv[1], "connect") == 0 && argc == 3) {
        return cmd_connect(argv[2]);
    }
    fprintf(stderr, "usage: atnnode demo|listen [port]|connect <file>\n");
    return 1;
}
