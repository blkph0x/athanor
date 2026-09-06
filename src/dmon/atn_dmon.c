/*
 * Module: atn_dmon.c
 * REQ:    REQ-4.1 / REQ-4.4
 * Spec:   DEC-0016, 0017, 0025, 0027, 0029.
 */

#include "atn_dmon.h"

#include <string.h>

static void dmon_attach_hb_tun(atn_dmon *d);

static int dmon_should_zeroize(const atn_dmon *d)
{
    if (d->flush_mode == ATN_CFG_FLUSH_LOG_ONLY && !d->wipe_armed) {
        return 0;
    }
    return 1;
}

static void dmon_maybe_flush(atn_dmon *d)
{
    if (!dmon_should_zeroize(d)) {
        d->flush_log_count++;
        return;
    }
    atn_dmon_flush(d);
}

void atn_dmon_init(atn_dmon *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    atn_2fa_store_init(&d->twofa);
    d->flush_mode = ATN_CFG_FLUSH_ZEROIZE;
    d->outage_class = ATN_CFG_OUTAGE_NORMAL;
}

int atn_dmon_apply_cfg(atn_dmon *d, const atn_cfg *c)
{
    if (d == NULL || c == NULL) {
        return ATN_ERR_PARAM;
    }
    d->diag = c->diag;
    d->flush_mode = c->flush_mode;
    d->wipe_armed = c->wipe_armed;
    d->outage_class = c->outage_class;
    return ATN_OK;
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
    dmon_attach_hb_tun(d);
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

int atn_dmon_hb_emit(atn_dmon *d, uint64_t bucket)
{
    if (atn_dmon_require(d) != ATN_OK || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    if (d->tun_ready) {
        d->hb.tun = &d->tun;
    }
    return atn_hb_emit(&d->hb, bucket);
}

static void dmon_attach_hb_tun(atn_dmon *d)
{
    if (d->hb_ready && d->tun_ready) {
        d->hb.tun = &d->tun;
    }
}

int atn_dmon_hb_tick(atn_dmon *d, uint64_t bucket)
{
    int rc;
    if (atn_dmon_require(d) != ATN_OK || !d->hb_ready) {
        return ATN_ERR_STATE;
    }
    /* DEC-0029: blackout/maintenance → local HOLD so grace cannot wipe. */
    if ((d->outage_class == ATN_CFG_OUTAGE_BLACKOUT ||
         d->outage_class == ATN_CFG_OUTAGE_MAINTENANCE) &&
        d->hb.state == ATN_HB_UNTRUSTED) {
        (void)atn_hb_vote(&d->hb, bucket, d->hb.id, ATN_HB_VOTE_HOLD);
    }
    rc = atn_hb_tick(&d->hb, bucket);
    /* DEC-0025: flush on DEAD only. DEC-0027: maybe LOG_ONLY. */
    if (d->hb.state == ATN_HB_DEAD) {
        dmon_maybe_flush(d);
        if (!d->loaded) {
            return ATN_ERR_STATE;
        }
        /* log_only: keys remain; report STATE so callers see DEAD path. */
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
        dmon_maybe_flush(d);
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
        dmon_attach_hb_tun(d);
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
        dmon_attach_hb_tun(d);
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

int atn_dmon_tun_bind_any(atn_dmon *d, uint16_t port)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_bind_any(&d->tun, port);
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

int atn_dmon_tun_hs_retry(atn_dmon *d)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_hs_retry(&d->tun);
}

int atn_dmon_tun_keepalive(atn_dmon *d)
{
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    return atn_tun_keepalive(&d->tun);
}

int atn_dmon_tun_pump(atn_dmon *d, int timeout_ms)
{
    uint8_t buf[ATN_TUN_MAX_PT];
    size_t n = 0;
    int rc;
    if (atn_dmon_require(d) != ATN_OK || !d->tun_ready) {
        return ATN_ERR_STATE;
    }
    if (d->tun.state != ATN_TUN_ESTABLISHED) {
        return atn_tun_pump(&d->tun, timeout_ms);
    }
    rc = atn_tun_recv_data(&d->tun, buf, &n, sizeof(buf), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    if (n == 0) {
        return ATN_OK; /* KA */
    }
    if (!d->hb_ready) {
        return ATN_OK;
    }
    rc = atn_hb_ingest(&d->hb, buf, n);
    atn_memzero(buf, n);
    /* Forged hb does not close the IPv4 tunnel (DEC-0022). */
    if (rc == ATN_ERR_AUTH || rc == ATN_ERR_PARAM) {
        return ATN_OK;
    }
    return rc;
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
