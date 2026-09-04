/*
 * Standalone DNS binary (REQ-2.3, DEC-0011).
 * Usage: atndns demo
 */
#include "atn_dns.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    atn_dns_zone z;
    atn_dns_q q;
    uint8_t query[64], resp[ATN_DNS_MAX_MSG];
    size_t n = 0, qn;
    static const uint8_t labels[] = {
        5, 'n','o','d','e','1',
        3, 'a','t','n',
        4, 't','e','s','t',
        0, 0, 1, 0, 1
    };
    int rc;

    if (argc != 2 || strcmp(argv[1], "demo") != 0) {
        fprintf(stderr, "usage: atndns demo\n");
        return 1;
    }
    atn_dns_zone_init(&z);
    memset(query, 0, sizeof(query));
    query[5] = 1;
    memcpy(query + 12, labels, sizeof(labels));
    qn = 12 + sizeof(labels);
    rc = atn_dns_parse_query(query, qn, &q);
    if (rc != ATN_OK) {
        fprintf(stderr, "parse %d\n", rc);
        return 1;
    }
    rc = atn_dns_respond(&z, &q, query, qn, resp, &n, sizeof(resp));
    if (rc != ATN_OK || n < 4 || resp[n - 1u] != 1) {
        fprintf(stderr, "respond rc=%d n=%u\n", rc, (unsigned)n);
        return 1;
    }
    printf("atndns demo: node1.atn.test A 127.0.0.1 OK (no recursion)\n");
    return 0;
}
