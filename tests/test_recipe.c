/*
 * REQ-5.1 / 6.2 / DEC-0033 recipe + crypto-floor gate.
 * Product Makefile and src/include/android paths in tools/src.list
 * must not contain fetch URLs or forbidden weak-crypto tokens.
 * Docs may cite URLs and may name forbidden algs while banning them.
 */
#include <stdio.h>
#include <string.h>

static int has_fetch(const char *line)
{
    /* Android XML xmlns is a namespace URI, not a fetch (DEC-0023). */
    if (strstr(line, "xmlns:") != NULL) {
        return 0;
    }
    return strstr(line, "http://") != NULL || strstr(line, "https://") != NULL;
}

/*
 * DEC-0033: lines that only document the ban are allowed.
 * Implementation of a weak primitive is not.
 */
static int ban_doc_line(const char *line)
{
    return strstr(line, "Do not") != NULL ||
           strstr(line, "do not") != NULL ||
           strstr(line, "Forbid") != NULL ||
           strstr(line, "forbid") != NULL ||
           strstr(line, "Forbidden") != NULL ||
           strstr(line, "DEC-0033") != NULL ||
           strstr(line, "not substitute") != NULL ||
           strstr(line, "NEVER") != NULL ||
           strstr(line, "reject") != NULL ||
           strstr(line, "Reject") != NULL ||
           strstr(line, "No OpenSSL") != NULL ||
           strstr(line, "no OpenSSL") != NULL ||
           strstr(line, "No WireGuard") != NULL;
}

static int has_weak_crypto(const char *line)
{
    static const char *bad[] = {
        "ML-KEM-512", "ML-KEM-768", "mlkem512", "mlkem768",
        "ML-DSA-44", "ML-DSA-65", "mldsa44", "mldsa65",
        "AES-128", "AES_128",
        "RC4", "3DES", "Blowfish",
        "libsodium", "OpenSSL", "BoringSSL", "WireGuard", "liboqs",
        NULL
    };
    unsigned i;
    if (ban_doc_line(line)) {
        return 0;
    }
    for (i = 0; bad[i] != NULL; i++) {
        if (strstr(line, bad[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static int product_path(const char *p)
{
    if (strcmp(p, "Makefile") == 0) {
        return 1;
    }
    if (strncmp(p, "src/", 4) == 0) {
        return 1;
    }
    if (strncmp(p, "include/", 8) == 0) {
        return 1;
    }
    if (strncmp(p, "android/", 8) == 0) {
        return 1;
    }
    return 0;
}

static int scan_file(const char *path)
{
    FILE *f;
    char buf[4096];
    int bad = 0;

    f = fopen(path, "rb");
    if (f == NULL) {
        printf("FAIL open %s\n", path);
        return 1;
    }
    while (fgets(buf, (int)sizeof(buf), f) != NULL) {
        if (has_fetch(buf)) {
            printf("FAIL fetch URL in %s: %s", path, buf);
            bad = 1;
        }
        if (has_weak_crypto(buf)) {
            printf("FAIL crypto floor in %s: %s", path, buf);
            bad = 1;
        }
    }
    fclose(f);
    return bad;
}

int main(void)
{
    FILE *lf;
    char line[256];
    int bad = 0;
    unsigned nprod = 0;

    printf("athanor recipe-check (DEC-0023/0033)\n");
    if (scan_file("Makefile") != 0) {
        bad = 1;
    } else {
        printf("ok   Makefile floor clean\n");
    }
    lf = fopen("tools/src.list", "rb");
    if (lf == NULL) {
        printf("FAIL open tools/src.list\n");
        return 1;
    }
    while (fgets(line, (int)sizeof(line), lf) != NULL) {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1u] == '\n' || line[L - 1u] == '\r')) {
            line[--L] = 0;
        }
        if (L == 0 || line[0] == '#') {
            continue;
        }
        if (!product_path(line)) {
            continue;
        }
        nprod++;
        if (scan_file(line) != 0) {
            bad = 1;
        }
    }
    fclose(lf);
    if (nprod < 8u) {
        printf("FAIL too few product paths (%u)\n", nprod);
        bad = 1;
    } else {
        printf("ok   scanned %u product paths (URL + crypto floor)\n", nprod);
    }
    if (bad) {
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
