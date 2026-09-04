/*
 * REQ-5.1 recipe gate: product Makefile must not fetch URLs.
 * GitHub Actions may still use actions/checkout (DEC-0020).
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *f;
    char buf[4096];
    int bad = 0;

    printf("athanor recipe-check\n");
    f = fopen("Makefile", "rb");
    if (f == NULL) {
        printf("FAIL open Makefile\n");
        return 1;
    }
    while (fgets(buf, (int)sizeof(buf), f) != NULL) {
        if (strstr(buf, "http://") != NULL || strstr(buf, "https://") != NULL) {
            printf("FAIL fetch URL in Makefile: %s", buf);
            bad = 1;
        }
    }
    fclose(f);
    if (bad) {
        return 1;
    }
    printf("ok   no fetch URL in Makefile\n");
    printf("ALL PASSED\n");
    return 0;
}
