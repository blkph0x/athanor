/*
 * Lab / daemon config (REQ-4.1). Spec: DEC-0021 / 0027 / 0028 / 0029.
 *
 * Text file, key=value lines. Unknown keys fail closed.
 * peer_ipv4 is required for ready() (DEC-0022: IPv4-only networks).
 */
#ifndef ATN_CFG_H
#define ATN_CFG_H

#include "atn_crypto.h"

/* Matches ATN_REPL_MAX_NODES until a migration DEC raises both (DEC-0028). */
#define ATN_CFG_MAX_HUBS 4u

#define ATN_CFG_FLUSH_ZEROIZE  0u
#define ATN_CFG_FLUSH_LOG_ONLY 1u

#define ATN_CFG_OUTAGE_NORMAL      0u
#define ATN_CFG_OUTAGE_MAINTENANCE 1u
#define ATN_CFG_OUTAGE_BLACKOUT    2u
#define ATN_CFG_OUTAGE_FARADAY     3u
#define ATN_CFG_OUTAGE_CAPTURE     4u

typedef struct {
    uint32_t ipv4_host; /* host order — hub 0 / peer_* */
    uint16_t port;
    uint8_t  ek[ATN_MLKEM1024_EK_LEN];
    uint8_t  have_ipv4;
    uint8_t  have_port;
    uint8_t  have_ek;
    uint8_t  witness[8];
    uint8_t  have_witness;
    /* DEC-0028: optional hubs 1..3 (indices 1..MAX-1); slot 0 is peer_* */
    struct {
        uint32_t ipv4_host;
        uint16_t port;
        uint8_t  ek[ATN_MLKEM1024_EK_LEN];
        uint8_t  have_ipv4;
        uint8_t  have_port;
        uint8_t  have_ek;
    } hub[ATN_CFG_MAX_HUBS]; /* hub[0] mirrors peer_* after parse */
    /* DEC-0027 / 0029 */
    uint8_t  diag;
    uint8_t  have_diag;
    uint8_t  flush_mode; /* ATN_CFG_FLUSH_* */
    uint8_t  have_flush_mode;
    uint8_t  wipe_armed;
    uint8_t  have_wipe_armed;
    uint8_t  outage_class; /* ATN_CFG_OUTAGE_* */
    uint8_t  have_outage;
} atn_cfg;

void atn_cfg_init(atn_cfg *c);
int  atn_cfg_parse(const char *text, size_t n, atn_cfg *c);
int  atn_cfg_load_file(const char *path, atn_cfg *c);
int  atn_cfg_ready(const atn_cfg *c);

/*
 * Purpose:  Number of complete hubs (primary + optional hub2..hub4).
 * Spec:     DEC-0028. Returns 0 if primary incomplete.
 */
unsigned atn_cfg_hub_count(const atn_cfg *c);

/*
 * Purpose:  Copy hub i (0 = primary) endpoints.
 * Returns:  ATN_OK or ATN_ERR_PARAM if i >= count.
 */
int atn_cfg_hub_get(const atn_cfg *c, unsigned i, uint32_t *ipv4_host,
                    uint16_t *port, uint8_t ek[ATN_MLKEM1024_EK_LEN]);

#endif
