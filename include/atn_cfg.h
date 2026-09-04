/*
 * Lab node config (REQ-4.1). Spec: DEC-0021.
 *
 * Text file, key=value lines. Unknown keys fail closed.
 * peer_ipv4 is required (DEC-0022: IPv4-only networks must work).
 */
#ifndef ATN_CFG_H
#define ATN_CFG_H

#include "atn_crypto.h"

typedef struct {
    uint32_t ipv4_host; /* host order */
    uint16_t port;
    uint8_t  ek[ATN_MLKEM1024_EK_LEN];
    uint8_t  have_ipv4;
    uint8_t  have_port;
    uint8_t  have_ek;
} atn_cfg;

void atn_cfg_init(atn_cfg *c);
int  atn_cfg_parse(const char *text, size_t n, atn_cfg *c);
int  atn_cfg_ready(const atn_cfg *c);

#endif
