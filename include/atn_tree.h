/*
 * Athanor in-process store (REQ-3.2). Spec: DEC-0012.
 *
 * Purpose:  AVL tree of length-prefixed blobs. Optional AEAD snapshot.
 * Policy:   libc malloc only. No SQLite, no mmap in this DEC.
 */
#ifndef ATN_TREE_H
#define ATN_TREE_H

#include "atn_crypto.h"

#define ATN_TREE_MAX_BLOB 4096u

typedef struct atn_tree_node atn_tree_node;

struct atn_tree_node {
    uint8_t *key;
    uint8_t *val;
    size_t   klen;
    size_t   vlen;
    int      height;
    atn_tree_node *left;
    atn_tree_node *right;
};

typedef struct {
    atn_tree_node *root;
    size_t         count;
} atn_tree;

typedef int (*atn_tree_iter)(const uint8_t *key, size_t klen,
                             const uint8_t *val, size_t vlen, void *ctx);

void atn_tree_init(atn_tree *t);
void atn_tree_wipe(atn_tree *t);

int atn_tree_put(atn_tree *t, const uint8_t *key, size_t klen,
                 const uint8_t *val, size_t vlen);
int atn_tree_get(const atn_tree *t, const uint8_t *key, size_t klen,
                 uint8_t *val, size_t *vlen, size_t max);
int atn_tree_del(atn_tree *t, const uint8_t *key, size_t klen);
int atn_tree_scan(const atn_tree *t, atn_tree_iter fn, void *ctx);

/*
 * Snapshot: nonce(12) || ciphertext || tag(16). Plaintext is not a file
 * format we expose. Caller owns key[32].
 */
int atn_tree_snapshot(const atn_tree *t, const uint8_t key[32],
                      uint8_t *out, size_t *n, size_t max);
int atn_tree_restore(atn_tree *t, const uint8_t key[32],
                     const uint8_t *in, size_t n);

#endif
