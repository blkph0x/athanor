/*
 * REQ-2.3 verification: in-zone A, out-of-zone REFUSED (not forwarded),
 * NXDOMAIN, no recursion, querier is loopback only.
 */
#include "atn_dns.h"
#include "atn_platform.h"

#include <stdio.h>
#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET atn_sock;
#  define ATN_INV INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
   typedef int atn_sock;
#  define ATN_INV (-1)
#endif

static int g_fail;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("ok   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        g_fail++;
    }
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int rcode_of(const uint8_t *m)
{
    return (int)(rd16(m + 2) & 0xfu);
}

static int aa_of(const uint8_t *m)
{
    return (rd16(m + 2) & 0x400u) != 0;
}

static int ra_of(const uint8_t *m)
{
    return (rd16(m + 2) & 0x80u) != 0;
}

static int ancount_of(const uint8_t *m)
{
    return (int)rd16(m + 6);
}

static int a_is_loopback(const uint8_t *m, size_t n)
{
    /* last 4 bytes of a short A answer */
    if (n < 4) {
        return 0;
    }
    return m[n - 4u] == 127 && m[n - 3u] == 0 && m[n - 2u] == 0 && m[n - 1u] == 1;
}

static int ask_udp(atn_dns_srv *s, const char *name, uint16_t qtype,
                   uint8_t *resp, size_t *n)
{
    atn_sock c;
    struct sockaddr_in sa;
    uint8_t q[128];
    size_t qn = 0;
    const char *p;
    size_t pos = 12;
    int r;
#if defined(ATN_OS_WINDOWS)
    int slen = (int)sizeof(sa);
#else
    socklen_t slen = sizeof(sa);
#endif
    memset(q, 0, sizeof(q));
    q[0] = 0x12;
    q[1] = 0x34;
    q[2] = 0x01; /* RD */
    q[5] = 1;    /* QDCOUNT */
    p = name;
    while (*p) {
        const char *dot = p;
        size_t lab;
        while (*dot && *dot != '.') {
            dot++;
        }
        lab = (size_t)(dot - p);
        if (lab == 0 || lab > 63u || pos + 1u + lab + 5u > sizeof(q)) {
            return ATN_ERR_LEN;
        }
        q[pos++] = (uint8_t)lab;
        memcpy(q + pos, p, lab);
        pos += lab;
        p = *dot ? dot + 1 : dot;
    }
    q[pos++] = 0;
    q[pos++] = (uint8_t)(qtype >> 8);
    q[pos++] = (uint8_t)qtype;
    q[pos++] = 0;
    q[pos++] = 1;
    qn = pos;
    c = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (c == ATN_INV) {
        return ATN_ERR_SOCK;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atn_dns_port(s));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(ATN_OS_WINDOWS)
    r = sendto(c, (const char *)q, (int)qn, 0, (struct sockaddr *)&sa, sizeof(sa));
#else
    r = (int)sendto(c, q, qn, 0, (const struct sockaddr *)&sa, sizeof(sa));
#endif
    if (r != (int)qn) {
#if defined(ATN_OS_WINDOWS)
        closesocket(c);
#else
        close(c);
#endif
        return ATN_ERR_SOCK;
    }
    r = atn_dns_serve_one(s, 3000);
    if (r != ATN_OK) {
#if defined(ATN_OS_WINDOWS)
        closesocket(c);
#else
        close(c);
#endif
        return r;
    }
#if defined(ATN_OS_WINDOWS)
    r = recvfrom(c, (char *)resp, (int)ATN_DNS_MAX_MSG, 0, (struct sockaddr *)&sa, &slen);
    closesocket(c);
#else
    r = (int)recvfrom(c, resp, ATN_DNS_MAX_MSG, 0, (struct sockaddr *)&sa, &slen);
    close(c);
#endif
    if (r <= 0) {
        return ATN_ERR_SOCK;
    }
    *n = (size_t)r;
    return ATN_OK;
}

int main(void)
{
    atn_dns_zone z;
    atn_dns_q q;
    atn_dns_srv s;
    uint8_t query[64], resp[ATN_DNS_MAX_MSG];
    size_t n = 0, qn = 0;
    int rc;

    printf("athanor dns  platform=%s\n", atn_platform_id());
    atn_dns_zone_init(&z);
    check("zone apex", strcmp(z.apex, "atn.test") == 0 && z.n >= 5);

    /* Build a tiny A query for node1.atn.test */
    memset(query, 0, sizeof(query));
    query[5] = 1;
    {
        static const uint8_t labels[] = {
            5, 'n','o','d','e','1',
            3, 'a','t','n',
            4, 't','e','s','t',
            0, 0, 1, 0, 1
        };
        memcpy(query + 12, labels, sizeof(labels));
        qn = 12 + sizeof(labels);
    }
    rc = atn_dns_parse_query(query, qn, &q);
    check("parse A node1", rc == ATN_OK && strcmp(q.qname, "node1.atn.test") == 0 &&
          q.qtype == ATN_DNS_TYPE_A);
    rc = atn_dns_respond(&z, &q, query, qn, resp, &n, sizeof(resp));
    check("respond A", rc == ATN_OK && rcode_of(resp) == 0 && aa_of(resp) &&
          !ra_of(resp) && ancount_of(resp) == 1 && a_is_loopback(resp, n));

    {
        static const uint8_t labels[] = {
            7, 'e','x','a','m','p','l','e',
            3, 'c','o','m',
            0, 0, 1, 0, 1
        };
        memset(query, 0, sizeof(query));
        query[5] = 1;
        memcpy(query + 12, labels, sizeof(labels));
        qn = 12 + sizeof(labels);
    }
    rc = atn_dns_parse_query(query, qn, &q);
    check("parse example.com", rc == ATN_OK);
    rc = atn_dns_respond(&z, &q, query, qn, resp, &n, sizeof(resp));
    check("out-of-zone REFUSED",
          rc == ATN_OK && rcode_of(resp) == (int)ATN_DNS_RCODE_REFUSED &&
          ancount_of(resp) == 0 && !aa_of(resp));

    {
        static const uint8_t labels[] = {
            5, 'g','h','o','s','t',
            3, 'a','t','n',
            4, 't','e','s','t',
            0, 0, 1, 0, 1
        };
        memset(query, 0, sizeof(query));
        query[5] = 1;
        memcpy(query + 12, labels, sizeof(labels));
        qn = 12 + sizeof(labels);
    }
    rc = atn_dns_parse_query(query, qn, &q);
    rc = atn_dns_respond(&z, &q, query, qn, resp, &n, sizeof(resp));
    check("in-zone missing NXDOMAIN",
          rc == ATN_OK && rcode_of(resp) == (int)ATN_DNS_RCODE_NXDOMAIN);

    check("listen", atn_dns_listen(&s, 0) == ATN_OK && atn_dns_port(&s) != 0);
    rc = ask_udp(&s, "node1.atn.test", ATN_DNS_TYPE_A, resp, &n);
    check("udp A rc", rc == ATN_OK);
    check("udp A loopback", rc == ATN_OK && rcode_of(resp) == 0 && a_is_loopback(resp, n));
    check("udp querier is loopback", s.last_peer == 0x7f000001u);
    check("udp not forwarded",
          s.last_peer != 0x08080808u && s.last_peer != 0x01010101u);

    rc = ask_udp(&s, "example.com", ATN_DNS_TYPE_A, resp, &n);
    check("udp REFUSED", rc == ATN_OK && rcode_of(resp) == (int)ATN_DNS_RCODE_REFUSED);

    rc = ask_udp(&s, "node1.atn.test", ATN_DNS_TYPE_AAAA, resp, &n);
    check("udp AAAA empty NOERROR",
          rc == ATN_OK && rcode_of(resp) == 0 && ancount_of(resp) == 0 && aa_of(resp));

    check("upsert", atn_dns_upsert_a(&s.zone, "n2.atn.test", 0x7f000002u, 60) == ATN_OK);
    rc = ask_udp(&s, "n2.atn.test", ATN_DNS_TYPE_A, resp, &n);
    check("udp new A",
          rc == ATN_OK && rcode_of(resp) == 0 && n >= 4 &&
          resp[n - 4u] == 127 && resp[n - 1u] == 2);

    check("delete", atn_dns_delete(&s.zone, "n2.atn.test", ATN_DNS_TYPE_A) == ATN_OK);
    rc = ask_udp(&s, "n2.atn.test", ATN_DNS_TYPE_A, resp, &n);
    check("udp deleted NXDOMAIN",
          rc == ATN_OK && rcode_of(resp) == (int)ATN_DNS_RCODE_NXDOMAIN);

    atn_dns_close(&s);
    if (g_fail == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}
