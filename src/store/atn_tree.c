/*
 * Module: atn_tree.c
 * REQ:    REQ-3.2
 * Spec:   DEC-0012. AVL (Adelson-Velsky & Landis 1962). Snapshot: RFC 8439.
 */

#include "atn_tree.h"

#include <stdlib.h>
#include <string.h>

static int key_cmp(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    size_t n = (na < nb) ? na : nb;
    int c = memcmp(a, b, n);
    if (c != 0) {
        return c;
    }
    if (na < nb) {
        return -1;
    }
    if (na > nb) {
        return 1;
    }
    return 0;
}

static int height_of(const atn_tree_node *n)
{
    return n == NULL ? 0 : n->height;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static void fix_height(atn_tree_node *n)
{
    int hl = height_of(n->left);
    int hr = height_of(n->right);
    n->height = 1 + max_int(hl, hr);
}

static int balance_of(const atn_tree_node *n)
{
    return n == NULL ? 0 : height_of(n->left) - height_of(n->right);
}

static atn_tree_node *rot_right(atn_tree_node *y)
{
    atn_tree_node *x = y->left;
    atn_tree_node *t = x->right;
    x->right = y;
    y->left = t;
    fix_height(y);
    fix_height(x);
    return x;
}

static atn_tree_node *rot_left(atn_tree_node *x)
{
    atn_tree_node *y = x->right;
    atn_tree_node *t = y->left;
    y->left = x;
    x->right = t;
    fix_height(x);
    fix_height(y);
    return y;
}

static atn_tree_node *rebalance(atn_tree_node *n)
{
    int b;
    fix_height(n);
    b = balance_of(n);
    if (b > 1) {
        if (balance_of(n->left) < 0) {
            n->left = rot_left(n->left);
        }
        return rot_right(n);
    }
    if (b < -1) {
        if (balance_of(n->right) > 0) {
            n->right = rot_right(n->right);
        }
        return rot_left(n);
    }
    return n;
}

static void node_wipe(atn_tree_node *n)
{
    if (n == NULL) {
        return;
    }
    node_wipe(n->left);
    node_wipe(n->right);
    atn_memzero(n->key, n->klen);
    atn_memzero(n->val, n->vlen);
    free(n->key);
    free(n->val);
    atn_memzero(n, sizeof(*n));
    free(n);
}

static atn_tree_node *node_new(const uint8_t *key, size_t klen,
                               const uint8_t *val, size_t vlen)
{
    atn_tree_node *n = (atn_tree_node *)malloc(sizeof(*n));
    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    n->key = (uint8_t *)malloc(klen ? klen : 1u);
    n->val = (uint8_t *)malloc(vlen ? vlen : 1u);
    if (n->key == NULL || n->val == NULL) {
        free(n->key);
        free(n->val);
        free(n);
        return NULL;
    }
    if (klen > 0) {
        memcpy(n->key, key, klen);
    }
    if (vlen > 0) {
        memcpy(n->val, val, vlen);
    }
    n->klen = klen;
    n->vlen = vlen;
    n->height = 1;
    return n;
}

static int replace_val(atn_tree_node *n, const uint8_t *val, size_t vlen)
{
    uint8_t *p = (uint8_t *)malloc(vlen ? vlen : 1u);
    if (p == NULL) {
        return ATN_ERR_LEN;
    }
    if (vlen > 0) {
        memcpy(p, val, vlen);
    }
    atn_memzero(n->val, n->vlen);
    free(n->val);
    n->val = p;
    n->vlen = vlen;
    return ATN_OK;
}

static atn_tree_node *insert_rec(atn_tree_node *n, const uint8_t *key, size_t klen,
                                 const uint8_t *val, size_t vlen, int *rc, int *added)
{
    int c;
    if (n == NULL) {
        n = node_new(key, klen, val, vlen);
        if (n == NULL) {
            *rc = ATN_ERR_LEN;
            return NULL;
        }
        *added = 1;
        *rc = ATN_OK;
        return n;
    }
    c = key_cmp(key, klen, n->key, n->klen);
    if (c == 0) {
        *rc = replace_val(n, val, vlen);
        return n;
    }
    if (c < 0) {
        n->left = insert_rec(n->left, key, klen, val, vlen, rc, added);
    } else {
        n->right = insert_rec(n->right, key, klen, val, vlen, rc, added);
    }
    if (*rc != ATN_OK) {
        return n;
    }
    return rebalance(n);
}

static const atn_tree_node *find_rec(const atn_tree_node *n,
                                     const uint8_t *key, size_t klen)
{
    int c;
    if (n == NULL) {
        return NULL;
    }
    c = key_cmp(key, klen, n->key, n->klen);
    if (c == 0) {
        return n;
    }
    if (c < 0) {
        return find_rec(n->left, key, klen);
    }
    return find_rec(n->right, key, klen);
}

static atn_tree_node *min_node(atn_tree_node *n)
{
    while (n != NULL && n->left != NULL) {
        n = n->left;
    }
    return n;
}

static atn_tree_node *del_rec(atn_tree_node *n, const uint8_t *key, size_t klen,
                              int *found)
{
    int c;
    if (n == NULL) {
        return NULL;
    }
    c = key_cmp(key, klen, n->key, n->klen);
    if (c < 0) {
        n->left = del_rec(n->left, key, klen, found);
    } else if (c > 0) {
        n->right = del_rec(n->right, key, klen, found);
    } else {
        *found = 1;
        if (n->left == NULL || n->right == NULL) {
            atn_tree_node *ch = n->left ? n->left : n->right;
            atn_memzero(n->key, n->klen);
            atn_memzero(n->val, n->vlen);
            free(n->key);
            free(n->val);
            atn_memzero(n, sizeof(*n));
            free(n);
            return ch;
        } else {
            atn_tree_node *m = min_node(n->right);
            uint8_t *nk, *nv;
            size_t nkl, nvl;
            nk = (uint8_t *)malloc(m->klen ? m->klen : 1u);
            nv = (uint8_t *)malloc(m->vlen ? m->vlen : 1u);
            if (nk == NULL || nv == NULL) {
                free(nk);
                free(nv);
                return n;
            }
            if (m->klen) {
                memcpy(nk, m->key, m->klen);
            }
            if (m->vlen) {
                memcpy(nv, m->val, m->vlen);
            }
            nkl = m->klen;
            nvl = m->vlen;
            n->right = del_rec(n->right, m->key, m->klen, found);
            atn_memzero(n->key, n->klen);
            atn_memzero(n->val, n->vlen);
            free(n->key);
            free(n->val);
            n->key = nk;
            n->val = nv;
            n->klen = nkl;
            n->vlen = nvl;
        }
    }
    return rebalance(n);
}

static int scan_rec(const atn_tree_node *n, atn_tree_iter fn, void *ctx)
{
    int rc;
    if (n == NULL) {
        return ATN_OK;
    }
    rc = scan_rec(n->left, fn, ctx);
    if (rc != ATN_OK) {
        return rc;
    }
    rc = fn(n->key, n->klen, n->val, n->vlen, ctx);
    if (rc != ATN_OK) {
        return rc;
    }
    return scan_rec(n->right, fn, ctx);
}

void atn_tree_init(atn_tree *t)
{
    if (t != NULL) {
        t->root = NULL;
        t->count = 0;
    }
}

void atn_tree_wipe(atn_tree *t)
{
    if (t == NULL) {
        return;
    }
    node_wipe(t->root);
    t->root = NULL;
    t->count = 0;
}

int atn_tree_put(atn_tree *t, const uint8_t *key, size_t klen,
                 const uint8_t *val, size_t vlen)
{
    int rc = ATN_OK, added = 0;
    if (t == NULL || key == NULL || (vlen > 0 && val == NULL)) {
        return ATN_ERR_PARAM;
    }
    if (klen == 0 || klen > ATN_TREE_MAX_BLOB || vlen > ATN_TREE_MAX_BLOB) {
        return ATN_ERR_LEN;
    }
    t->root = insert_rec(t->root, key, klen, val, vlen, &rc, &added);
    if (rc == ATN_OK && added) {
        t->count++;
    }
    return rc;
}

int atn_tree_get(const atn_tree *t, const uint8_t *key, size_t klen,
                 uint8_t *val, size_t *vlen, size_t max)
{
    const atn_tree_node *n;
    if (t == NULL || key == NULL || val == NULL || vlen == NULL) {
        return ATN_ERR_PARAM;
    }
    n = find_rec(t->root, key, klen);
    if (n == NULL) {
        return ATN_ERR_STATE;
    }
    if (n->vlen > max) {
        return ATN_ERR_LEN;
    }
    if (n->vlen > 0) {
        memcpy(val, n->val, n->vlen);
    }
    *vlen = n->vlen;
    return ATN_OK;
}

int atn_tree_del(atn_tree *t, const uint8_t *key, size_t klen)
{
    int found = 0;
    if (t == NULL || key == NULL) {
        return ATN_ERR_PARAM;
    }
    t->root = del_rec(t->root, key, klen, &found);
    if (!found) {
        return ATN_ERR_STATE;
    }
    t->count--;
    return ATN_OK;
}

int atn_tree_scan(const atn_tree *t, atn_tree_iter fn, void *ctx)
{
    if (t == NULL || fn == NULL) {
        return ATN_ERR_PARAM;
    }
    return scan_rec(t->root, fn, ctx);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

typedef struct {
    uint8_t *buf;
    size_t   used;
    size_t   max;
    int      rc;
} ser_ctx;

static int ser_one(const uint8_t *key, size_t klen, const uint8_t *val, size_t vlen,
                   void *ctx)
{
    ser_ctx *s = (ser_ctx *)ctx;
    if (s->used + 8u + klen + vlen > s->max) {
        s->rc = ATN_ERR_LEN;
        return ATN_ERR_LEN;
    }
    wr32(s->buf + s->used, (uint32_t)klen);
    s->used += 4u;
    memcpy(s->buf + s->used, key, klen);
    s->used += klen;
    wr32(s->buf + s->used, (uint32_t)vlen);
    s->used += 4u;
    if (vlen > 0) {
        memcpy(s->buf + s->used, val, vlen);
    }
    s->used += vlen;
    return ATN_OK;
}

int atn_tree_snapshot(const atn_tree *t, const uint8_t key[32],
                      uint8_t *out, size_t *n, size_t max)
{
    uint8_t *plain;
    ser_ctx sc;
    uint8_t nonce[12], tag[16];
    size_t need;
    int rc;

    if (t == NULL || key == NULL || out == NULL || n == NULL) {
        return ATN_ERR_PARAM;
    }
    need = 4u + t->count * (8u + 2u * ATN_TREE_MAX_BLOB);
    if (need > 8u * 1024u * 1024u) {
        return ATN_ERR_LEN;
    }
    /* Tight bound: walk would need exact size; allocate count*(8+2*max) is huge.
     * Use a running serialize into a 1MiB cap for this DEC. */
    need = 4u + 1024u * 1024u;
    if (12u + need + 16u > max) {
        need = (max > 28u) ? (max - 28u) : 0;
    }
    if (need < 4u) {
        return ATN_ERR_LEN;
    }
    plain = (uint8_t *)malloc(need);
    if (plain == NULL) {
        return ATN_ERR_LEN;
    }
    wr32(plain, (uint32_t)t->count);
    sc.buf = plain;
    sc.used = 4;
    sc.max = need;
    sc.rc = ATN_OK;
    rc = atn_tree_scan(t, ser_one, &sc);
    if (rc != ATN_OK) {
        atn_memzero(plain, need);
        free(plain);
        return rc;
    }
    rc = atn_random_bytes(nonce, 12);
    if (rc != ATN_OK) {
        atn_memzero(plain, need);
        free(plain);
        return rc;
    }
    if (12u + sc.used + 16u > max) {
        atn_memzero(plain, need);
        free(plain);
        return ATN_ERR_LEN;
    }
    rc = atn_aead_encrypt(key, nonce, NULL, 0, plain, sc.used, out + 12, tag);
    if (rc != ATN_OK) {
        atn_memzero(plain, need);
        free(plain);
        return rc;
    }
    memcpy(out, nonce, 12);
    memcpy(out + 12 + sc.used, tag, 16);
    *n = 12u + sc.used + 16u;
    atn_memzero(plain, need);
    free(plain);
    return ATN_OK;
}

int atn_tree_restore(atn_tree *t, const uint8_t key[32],
                     const uint8_t *in, size_t n)
{
    uint8_t nonce[12], *plain;
    size_t clen, i, off;
    uint32_t count;
    int rc = ATN_OK;

    if (t == NULL || key == NULL || in == NULL || n < 12u + 16u) {
        return ATN_ERR_PARAM;
    }
    atn_tree_wipe(t);
    memcpy(nonce, in, 12);
    clen = n - 12u - 16u;
    plain = (uint8_t *)malloc(clen ? clen : 1u);
    if (plain == NULL) {
        return ATN_ERR_LEN;
    }
    rc = atn_aead_decrypt(key, nonce, NULL, 0, in + 12, clen, in + 12 + clen, plain);
    if (rc != ATN_OK) {
        atn_memzero(plain, clen);
        free(plain);
        return ATN_ERR_AUTH;
    }
    if (clen < 4u) {
        atn_memzero(plain, clen);
        free(plain);
        return ATN_ERR_LEN;
    }
    count = rd32(plain);
    off = 4;
    for (i = 0; i < count; i++) {
        uint32_t klen, vlen;
        if (off + 4u > clen) {
            rc = ATN_ERR_LEN;
            break;
        }
        klen = rd32(plain + off);
        off += 4u;
        if (klen == 0 || klen > ATN_TREE_MAX_BLOB || off + klen + 4u > clen) {
            rc = ATN_ERR_LEN;
            break;
        }
        {
            const uint8_t *k = plain + off;
            off += klen;
            vlen = rd32(plain + off);
            off += 4u;
            if (vlen > ATN_TREE_MAX_BLOB || off + vlen > clen) {
                rc = ATN_ERR_LEN;
                break;
            }
            rc = atn_tree_put(t, k, klen, plain + off, vlen);
            off += vlen;
            if (rc != ATN_OK) {
                break;
            }
        }
    }
    atn_memzero(plain, clen);
    free(plain);
    if (rc != ATN_OK) {
        atn_tree_wipe(t);
    }
    return rc;
}
