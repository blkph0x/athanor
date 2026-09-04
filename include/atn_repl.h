/*
 * Athanor replication (REQ-3.1). Spec: docs/REPL.md, DEC-0013.
 *
 * Purpose:  Shard + vector-clock replicate AEAD blocks over REQ-1.2.
 * Policy:   No plaintext TCP. Conflicts are not last-writer-wins.
 */
#ifndef ATN_REPL_H
#define ATN_REPL_H

#include "atn_tree.h"
#include "atn_tun.h"

#define ATN_REPL_MAX_NODES  4u
#define ATN_REPL_FACTOR     2u
#define ATN_REPL_SHARDS     8u
#define ATN_REPL_ID_LEN     8u
#define ATN_REPL_MAX_KEY    64u
#define ATN_REPL_MAX_VAL    256u

#define ATN_REPL_PUT         1u
#define ATN_REPL_CATCHUP_REQ 2u

typedef struct {
    uint8_t id[ATN_REPL_ID_LEN];
    uint8_t cluster[32];
    uint8_t roster[ATN_REPL_MAX_NODES][ATN_REPL_ID_LEN];
    unsigned n_nodes;
    unsigned self;
    int      online;
    unsigned conflicts;
    atn_tree tree;
    atn_tun *tun; /* not owned */
} atn_repl;

/*
 * Purpose:  Shard id in 0 .. ATN_REPL_SHARDS-1.
 * Spec:     docs/REPL.md; SHA3-256(key)[0..7] BE % SHARDS.
 */
unsigned atn_repl_shard(const uint8_t *key, size_t klen);

int atn_repl_owner_p(const atn_repl *r, const uint8_t *key, size_t klen,
                     unsigned idx);

int atn_repl_init(atn_repl *r, const uint8_t id[ATN_REPL_ID_LEN],
                  const uint8_t cluster[32],
                  const uint8_t roster[][ATN_REPL_ID_LEN], unsigned n_nodes,
                  unsigned self, atn_tun *tun);

int atn_repl_put(atn_repl *r, const uint8_t *key, size_t klen,
                 const uint8_t *val, size_t vlen);
int atn_repl_get(const atn_repl *r, const uint8_t *key, size_t klen,
                 uint8_t *val, size_t *vlen, size_t max);
int atn_repl_pump(atn_repl *r, int timeout_ms);
int atn_repl_catchup_req(atn_repl *r);
int atn_repl_apply(atn_repl *r, const uint8_t *msg, size_t n);
void atn_repl_wipe(atn_repl *r);

#endif
