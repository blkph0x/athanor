/*
 * Module: atn_repl.c
 * REQ:    REQ-3.1
 * Spec:   docs/REPL.md, DEC-0013, FIPS 202 SHA3-256, RFC 8439 AEAD
 */

#include "atn_repl.h"

#include <string.h>

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr64(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint64_t rd64(const uint8_t *p)
{
    unsigned i;
    uint64_t v = 0;
    for (i = 0; i < 8u; i++) {
        v |= ((uint64_t)p[i]) << (8u * i);
    }
    return v;
}

unsigned atn_repl_shard(const uint8_t *key, size_t klen)
{
    uint8_t h[32];
    uint64_t v;
    unsigned i;
    if (key == NULL || klen == 0) {
        return 0;
    }
    atn_sha3_256(key, klen, h);
    v = 0;
    for (i = 0; i < 8u; i++) {
        v = (v << 8) | h[i];
    }
    return (unsigned)(v % (uint64_t)ATN_REPL_SHARDS);
}

int atn_repl_owner_p(const atn_repl *r, const uint8_t *key, size_t klen,
                     unsigned idx)
{
    unsigned shard, nown, i, want;
    if (r == NULL || key == NULL || r->n_nodes == 0 || idx >= r->n_nodes) {
        return 0;
    }
    shard = atn_repl_shard(key, klen);
    nown = r->n_nodes < ATN_REPL_FACTOR ? r->n_nodes : ATN_REPL_FACTOR;
    for (i = 0; i < nown; i++) {
        want = (shard + i) % r->n_nodes;
        if (want == idx) {
            return 1;
        }
    }
    return 0;
}

int atn_repl_init(atn_repl *r, const uint8_t id[ATN_REPL_ID_LEN],
                  const uint8_t cluster[32],
                  const uint8_t roster[][ATN_REPL_ID_LEN], unsigned n_nodes,
                  unsigned self, atn_tun *tun)
{
    unsigned i;
    if (r == NULL || id == NULL || cluster == NULL || roster == NULL ||
        n_nodes == 0 || n_nodes > ATN_REPL_MAX_NODES || self >= n_nodes) {
        return ATN_ERR_PARAM;
    }
    memset(r, 0, sizeof(*r));
    memcpy(r->id, id, ATN_REPL_ID_LEN);
    memcpy(r->cluster, cluster, 32);
    r->n_nodes = n_nodes;
    r->self = self;
    r->online = 1;
    r->tun = tun;
    atn_tree_init(&r->tree);
    for (i = 0; i < n_nodes; i++) {
        memcpy(r->roster[i], roster[i], ATN_REPL_ID_LEN);
    }
    if (memcmp(r->roster[self], id, ATN_REPL_ID_LEN) != 0) {
        atn_repl_wipe(r);
        return ATN_ERR_PARAM;
    }
    return ATN_OK;
}

typedef struct {
    uint8_t  id[ATN_REPL_ID_LEN];
    uint64_t seq;
} atn_vc;

static int parse_rec(const uint8_t *p, size_t n,
                     atn_vc *vc, unsigned *nvc,
                     const uint8_t **nonce, const uint8_t **ct, size_t *clen,
                     const uint8_t **tag, unsigned *conflict, size_t *used)
{
    size_t off = 0;
    unsigned i, nc;
    uint16_t cl;
    if (n < 1u) {
        return ATN_ERR_LEN;
    }
    nc = p[0];
    off = 1;
    if (nc == 0 || nc > ATN_REPL_MAX_NODES) {
        return ATN_ERR_PARAM;
    }
    if (off + nc * 16u + 12u + 2u + 16u + 1u > n) {
        return ATN_ERR_LEN;
    }
    for (i = 0; i < nc; i++) {
        memcpy(vc[i].id, p + off, ATN_REPL_ID_LEN);
        vc[i].seq = rd64(p + off + 8);
        off += 16u;
    }
    *nvc = nc;
    *nonce = p + off;
    off += 12u;
    cl = rd16(p + off);
    off += 2u;
    if (off + cl + 16u + 1u > n) {
        return ATN_ERR_LEN;
    }
    *ct = p + off;
    *clen = cl;
    off += cl;
    *tag = p + off;
    off += 16u;
    *conflict = p[off];
    off += 1u;
    *used = off;
    return ATN_OK;
}

static int pack_rec(uint8_t *out, size_t *n, size_t max,
                    const atn_vc *vc, unsigned nvc,
                    const uint8_t nonce[12],
                    const uint8_t *ct, uint16_t clen,
                    const uint8_t tag[16], unsigned conflict)
{
    size_t off = 0;
    unsigned i;
    if (nvc == 0 || nvc > ATN_REPL_MAX_NODES) {
        return ATN_ERR_PARAM;
    }
    if (1u + nvc * 16u + 12u + 2u + clen + 16u + 1u > max) {
        return ATN_ERR_LEN;
    }
    out[0] = (uint8_t)nvc;
    off = 1;
    for (i = 0; i < nvc; i++) {
        memcpy(out + off, vc[i].id, ATN_REPL_ID_LEN);
        wr64(out + off + 8, vc[i].seq);
        off += 16u;
    }
    memcpy(out + off, nonce, 12);
    off += 12u;
    wr16(out + off, clen);
    off += 2u;
    if (clen > 0) {
        memcpy(out + off, ct, clen);
    }
    off += clen;
    memcpy(out + off, tag, 16);
    off += 16u;
    out[off++] = (uint8_t)conflict;
    *n = off;
    return ATN_OK;
}

/* 1 = a dominates b, 2 = b dominates a, 0 = equal, -1 = concurrent */
static int vc_cmp(const atn_vc *a, unsigned na, const atn_vc *b, unsigned nb)
{
    unsigned i, j;
    int a_ge = 1, b_ge = 1, a_gt = 0, b_gt = 0;

    for (i = 0; i < na; i++) {
        uint64_t bs = 0;
        for (j = 0; j < nb; j++) {
            if (memcmp(a[i].id, b[j].id, ATN_REPL_ID_LEN) == 0) {
                bs = b[j].seq;
                break;
            }
        }
        if (a[i].seq < bs) {
            a_ge = 0;
        }
        if (a[i].seq > bs) {
            a_gt = 1;
        }
    }
    for (j = 0; j < nb; j++) {
        uint64_t as = 0;
        for (i = 0; i < na; i++) {
            if (memcmp(b[j].id, a[i].id, ATN_REPL_ID_LEN) == 0) {
                as = a[i].seq;
                break;
            }
        }
        if (b[j].seq < as) {
            b_ge = 0;
        }
        if (b[j].seq > as) {
            b_gt = 1;
        }
    }
    if (a_ge && b_ge && !a_gt && !b_gt) {
        return 0;
    }
    if (a_ge && a_gt && !b_gt) {
        return 1;
    }
    if (b_ge && b_gt && !a_gt) {
        return 2;
    }
    return -1;
}

static int load_vc(const atn_repl *r, const uint8_t *key, size_t klen,
                   atn_vc *vc, unsigned *nvc)
{
    uint8_t rec[512];
    size_t n = 0, used = 0, clen = 0;
    const uint8_t *nonce, *ct, *tag;
    unsigned conf;
    int rc;
    rc = atn_tree_get(&r->tree, key, klen, rec, &n, sizeof(rec));
    if (rc == ATN_ERR_STATE) {
        *nvc = 0;
        return ATN_OK;
    }
    if (rc != ATN_OK) {
        return rc;
    }
    return parse_rec(rec, n, vc, nvc, &nonce, &ct, &clen, &tag, &conf, &used);
}

static int send_put(atn_repl *r, const uint8_t *key, size_t klen,
                    const uint8_t *rec, size_t rn)
{
    uint8_t msg[ATN_TUN_MAX_PT];
    if (r->tun == NULL || !r->online) {
        return ATN_OK;
    }
    if (1u + 1u + klen + rn > sizeof(msg)) {
        return ATN_ERR_LEN;
    }
    msg[0] = ATN_REPL_PUT;
    msg[1] = (uint8_t)klen;
    memcpy(msg + 2, key, klen);
    memcpy(msg + 2 + klen, rec, rn);
    return atn_tun_send(r->tun, msg, 2u + klen + rn);
}

int atn_repl_put(atn_repl *r, const uint8_t *key, size_t klen,
                 const uint8_t *val, size_t vlen)
{
    atn_vc vc[ATN_REPL_MAX_NODES];
    unsigned nvc = 0, i, found = 0;
    uint8_t nonce[12], ct[ATN_REPL_MAX_VAL], tag[16], rec[512];
    size_t rn = 0;
    int rc;

    if (r == NULL || key == NULL || (vlen > 0 && val == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (klen == 0 || klen > ATN_REPL_MAX_KEY || vlen > ATN_REPL_MAX_VAL) {
        return ATN_ERR_LEN;
    }
    if (!atn_repl_owner_p(r, key, klen, r->self)) {
        return ATN_ERR_STATE;
    }
    rc = load_vc(r, key, klen, vc, &nvc);
    if (rc != ATN_OK) {
        return rc;
    }
    for (i = 0; i < nvc; i++) {
        if (memcmp(vc[i].id, r->id, ATN_REPL_ID_LEN) == 0) {
            vc[i].seq++;
            found = 1;
            break;
        }
    }
    if (!found) {
        if (nvc >= ATN_REPL_MAX_NODES) {
            return ATN_ERR_LEN;
        }
        memcpy(vc[nvc].id, r->id, ATN_REPL_ID_LEN);
        vc[nvc].seq = 1;
        nvc++;
    }
    rc = atn_random_bytes(nonce, 12);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = atn_aead_encrypt(r->cluster, nonce, key, klen, val, vlen, ct, tag);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = pack_rec(rec, &rn, sizeof(rec), vc, nvc, nonce, ct, (uint16_t)vlen,
                  tag, 0);
    atn_memzero(ct, sizeof(ct));
    if (rc != ATN_OK) {
        return rc;
    }
    rc = atn_tree_put(&r->tree, key, klen, rec, rn);
    if (rc != ATN_OK) {
        return rc;
    }
    return send_put(r, key, klen, rec, rn);
}

int atn_repl_get(const atn_repl *r, const uint8_t *key, size_t klen,
                 uint8_t *val, size_t *vlen, size_t max)
{
    uint8_t rec[512];
    size_t n = 0, used = 0, clen = 0;
    atn_vc vc[ATN_REPL_MAX_NODES];
    unsigned nvc = 0, conf;
    const uint8_t *nonce, *ct, *tag;
    int rc;
    if (r == NULL || key == NULL || val == NULL || vlen == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tree_get(&r->tree, key, klen, rec, &n, sizeof(rec));
    if (rc != ATN_OK) {
        return rc;
    }
    rc = parse_rec(rec, n, vc, &nvc, &nonce, &ct, &clen, &tag, &conf, &used);
    if (rc != ATN_OK) {
        return rc;
    }
    if (clen > max) {
        return ATN_ERR_LEN;
    }
    rc = atn_aead_decrypt(r->cluster, nonce, key, klen, ct, clen, tag, val);
    if (rc != ATN_OK) {
        return ATN_ERR_AUTH;
    }
    *vlen = clen;
    return ATN_OK;
}

static int apply_put_body(atn_repl *r, const uint8_t *key, size_t klen,
                          const uint8_t *rec, size_t rn)
{
    atn_vc inc[ATN_REPL_MAX_NODES], loc[ATN_REPL_MAX_NODES];
    unsigned ni = 0, nl = 0, conf;
    size_t used = 0, clen = 0;
    const uint8_t *nonce, *ct, *tag;
    uint8_t pt[ATN_REPL_MAX_VAL];
    int rc, cmp;

    rc = parse_rec(rec, rn, inc, &ni, &nonce, &ct, &clen, &tag, &conf, &used);
    if (rc != ATN_OK) {
        return rc;
    }
    if (clen > ATN_REPL_MAX_VAL) {
        return ATN_ERR_LEN;
    }
    rc = atn_aead_decrypt(r->cluster, nonce, key, klen, ct, clen, tag, pt);
    atn_memzero(pt, sizeof(pt));
    if (rc != ATN_OK) {
        return ATN_ERR_AUTH;
    }
    rc = load_vc(r, key, klen, loc, &nl);
    if (rc != ATN_OK) {
        return rc;
    }
    if (nl == 0) {
        return atn_tree_put(&r->tree, key, klen, rec, rn);
    }
    cmp = vc_cmp(inc, ni, loc, nl);
    if (cmp == 0 || cmp == 2) {
        return ATN_OK; /* equal or local dominates */
    }
    if (cmp == 1) {
        return atn_tree_put(&r->tree, key, klen, rec, rn);
    }
    /* concurrent */
    r->conflicts++;
    {
        uint8_t keep[512];
        size_t kn = 0;
        atn_vc kvc[ATN_REPL_MAX_NODES];
        unsigned knc;
        const uint8_t *knonce, *kct, *ktag;
        size_t kclen = 0, kused = 0;
        unsigned kconf;
        rc = atn_tree_get(&r->tree, key, klen, keep, &kn, sizeof(keep));
        if (rc != ATN_OK) {
            return rc;
        }
        rc = parse_rec(keep, kn, kvc, &knc, &knonce, &kct, &kclen, &ktag,
                       &kconf, &kused);
        if (rc != ATN_OK) {
            return rc;
        }
        keep[kused - 1u] = 1;
        (void)atn_tree_put(&r->tree, key, klen, keep, kn);
    }
    return ATN_ERR_CONFLICT;
}

typedef struct {
    atn_repl *r;
    int rc;
} catch_ctx;

static int catch_send(const uint8_t *key, size_t klen, const uint8_t *val,
                      size_t vlen, void *ctx)
{
    catch_ctx *c = (catch_ctx *)ctx;
    int rc = send_put(c->r, key, klen, val, vlen);
    if (rc != ATN_OK) {
        c->rc = rc;
    }
    return rc;
}

int atn_repl_apply(atn_repl *r, const uint8_t *msg, size_t n)
{
    uint8_t klen;
    if (r == NULL || msg == NULL || n == 0) {
        return ATN_ERR_PARAM;
    }
    if (msg[0] == ATN_REPL_CATCHUP_REQ) {
        catch_ctx c;
        c.r = r;
        c.rc = ATN_OK;
        (void)atn_tree_scan(&r->tree, catch_send, &c);
        return c.rc;
    }
    if (msg[0] != ATN_REPL_PUT || n < 3u) {
        return ATN_ERR_PARAM;
    }
    klen = msg[1];
    if (klen == 0 || klen > ATN_REPL_MAX_KEY || 2u + klen >= n) {
        return ATN_ERR_LEN;
    }
    if (!atn_repl_owner_p(r, msg + 2, klen, r->self)) {
        return ATN_ERR_STATE;
    }
    return apply_put_body(r, msg + 2, klen, msg + 2 + klen, n - 2u - klen);
}

int atn_repl_pump(atn_repl *r, int timeout_ms)
{
    uint8_t buf[ATN_TUN_MAX_PT];
    size_t n = 0;
    int rc;
    if (r == NULL || r->tun == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_tun_recv_data(r->tun, buf, &n, sizeof(buf), timeout_ms);
    if (rc != ATN_OK) {
        return rc;
    }
    return atn_repl_apply(r, buf, n);
}

int atn_repl_catchup_req(atn_repl *r)
{
    uint8_t msg = ATN_REPL_CATCHUP_REQ;
    if (r == NULL || r->tun == NULL) {
        return ATN_ERR_PARAM;
    }
    return atn_tun_send(r->tun, &msg, 1);
}

void atn_repl_wipe(atn_repl *r)
{
    if (r == NULL) {
        return;
    }
    atn_tree_wipe(&r->tree);
    atn_memzero(r->cluster, 32);
    r->conflicts = 0;
}
