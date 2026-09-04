/*
 * Module: atn_dmon.c
 * REQ:    REQ-4.1 / REQ-4.4
 * Spec:   DEC-0016, DEC-0017. Flush is atn_memzero of our slots.
 *         UNTRUSTED/DEAD heartbeat or 2FA lockout flushes RAM copies.
 */

#include "atn_dmon.h"

#include <string.h>

void atn_dmon_init(atn_dmon *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    atn_2fa_store_init(&d->twofa);
}

int atn_dmon_load(atn_dmon *d, const uint8_t device_key[32],
                  const uint8_t cluster[32])
{
    if (d == NULL || device_key == NULL || cluster == NULL) {
        return ATN_ERR_PARAM;
    }
    memcpy(d->device_key, device_key, 32);
    memcpy(d->cluster, cluster, 32);
    d->loaded = 1;
    return ATN_OK;
}

int atn_dmon_require(const atn_dmon *d)
{
    if (d == NULL || !d->loaded) {
        return ATN_ERR_STATE;
    }
    return ATN_OK;
}

void atn_dmon_flush(atn_dmon *d)
{
    if (d == NULL) {
        return;
    }
    atn_2fa_store_init(&d->twofa);
    atn_hb_wipe(&d->hb);
    if (d->tun_ready) {
        atn_tun_close(&d->tun);
    }
    atn_tun_wipe(&d->tun);
    atn_memzero(d->device_key, 32);
    atn_memzero(d->cluster, 32);
    d->loaded = 0;
    d->hb_ready = 0;
    d->tun_ready = 0;
}

int atn_dmon_hb_init(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                     uint64_t epoch, const uint8_t head[ATN_HB_HEAD_LEN])
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    if (id == NULL || head == NULL) {
        return ATN_ERR_PARAM;
    }
    /* Cluster key is the HMAC key (DEC-0014). Wire hb on tun when ready. */
    rc = atn_hb_init(&d->hb, id, d->cluster, epoch, head,
                     d->tun_ready ? &d->tun : NULL);
    if (rc != ATN_OK) {
        return rc;
    }
    d->hb_ready = 1;
    return ATN_OK;
}

int atn_dmon_hb_add_peer(atn_dmon *d, const uint8_t id[ATN_HB_ID_LEN],
                         const uint8_t key[32])
{
    if (atn_dmon_require(d) != ATN_OK || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    return atn_hb_add_peer(&d->hb, id, key);
}

int atn_dmon_hb_ingest(atn_dmon *d, const uint8_t *msg, size_t n)
{
    if (atn_dmon_require(d) != ATN_OK || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    return atn_hb_ingest(&d->hb, msg, n);
}

int atn_dmon_hb_tick(atn_dmon *d, uint64_t bucket)
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    rc = atn_hb_tick(&d->hb, bucket);
    /* DEC-0017: UNTRUSTED or DEAD destroys RAM copies (REQ-4.4 / 5.3). */
    if (d->hb.state == ATN_HB_UNTRUSTED || d->hb.state == ATN_HB_DEAD) {
        atn_dmon_flush(d);
        return ATN_ERR_STATE;
    }
    return rc;
}

int atn_dmon_hb_state(const atn_dmon *d)
{
    if (d == NULL || !d->loaded || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    return d->hb.state;
}

int atn_dmon_2fa_enroll(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                        uint8_t key_out[ATN_2FA_KEY_LEN])
{
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    return atn_2fa_enroll(&d->twofa, id, key_out);
}

int atn_dmon_2fa_challenge(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                           uint8_t chal[ATN_2FA_CHAL_LEN])
{
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    return atn_2fa_challenge(&d->twofa, id, chal);
}

int atn_dmon_2fa_verify(atn_dmon *d, const uint8_t id[ATN_2FA_ID_LEN],
                        const uint8_t chal[ATN_2FA_CHAL_LEN],
                        const uint8_t resp[ATN_2FA_RESP_LEN])
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    rc = atn_2fa_verify(&d->twofa, id, chal, resp);
    if (rc == ATN_ERR_LOCKOUT) {
        atn_dmon_flush(d);
    }
    return rc;
}

int atn_dmon_tun_initiator(atn_dmon *d,
                           const uint8_t peer_ek[ATN_MLKEM1024_EK_LEN])
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    rc = atn_tun_init_initiator(&d->tun, peer_ek);
    if (rc == ATN_OK) {
        d->tun_ready = 1;
    }
    return rc;
}

int atn_dmon_tun_responder(atn_dmon *d,
                           const uint8_t own_dk[ATN_MLKEM1024_DK_LEN])
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK) {
        return ATN_ERR_STATE;
    }
    rc = atn_tun_init_responder(&d->tun, own_dk);
    if (rc == ATN_OK) {
        d->tun_ready = 1;
    }
    return rc;
}

int atn_dmon_tun_bind(atn_dmon *d, uint16_t port)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_bind(&d->tun, port);
}

int atn_dmon_tun_set_peer(atn_dmon *d, uint32_t ipv4_host, uint16_t port)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_set_peer(&d->tun, ipv4_host, port);
}

int atn_dmon_tun_hs_send(atn_dmon *d)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_hs_send_init(&d->tun);
}

int atn_dmon_tun_pump(atn_dmon *d, int timeout_ms)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_pump(&d->tun, timeout_ms);
}

int atn_dmon_tun_send(atn_dmon *d, const uint8_t *pt, size_t n)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_send(&d->tun, pt, n);
}

int atn_dmon_tun_recv(atn_dmon *d, uint8_t *pt, size_t *n, size_t max,
                      int timeout_ms)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_recv_data(&d->tun, pt, n, max, timeout_ms);
}

int atn_dmon_tun_state(const atn_dmon *d)
{
    if (d == NULL || !d->loaded || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return d->tun.state;
}

uint16_t atn_dmon_tun_port(const atn_dmon *d)
{
    if (d == NULL || !d->tun_ready) {
        return 0;
    }
    return d->tun.local_port;
}
