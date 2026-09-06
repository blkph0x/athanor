/*
 * Lab enroll console launcher (DEC-0042).
 *   atnenroll demo          — validate phone label + write sample receipt
 *   atnenroll serve [port]  — loopback UI via tools/enroll-console.ps1 (Windows)
 *
 * Plain HTTP on 127.0.0.1 only. Not mesh atnhttp (ISS-0009).
 * phone_number is a roster label only (no SMS — ISS-0020).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int phone_label_ok(const char *p)
{
    size_t n, i, digits = 0;
    if (p == NULL) {
        return 0;
    }
    n = strlen(p);
    if (n < 7u || n > 32u) {
        return 0;
    }
    i = 0;
    if (p[0] == '+') {
        i = 1;
    }
    for (; i < n; i++) {
        char c = p[i];
        if (c >= '0' && c <= '9') {
            digits++;
            continue;
        }
        if (c == ' ' || c == '-') {
            continue;
        }
        return 0;
    }
    return digits >= 6u;
}

static int cmd_demo(void)
{
    const char *good = "+61400000000";
    const char *bad = "not-a-phone";
    FILE *f;
    if (!phone_label_ok(good)) {
        fprintf(stderr, "demo: good label rejected\n");
        return 1;
    }
    if (phone_label_ok(bad)) {
        fprintf(stderr, "demo: bad label accepted\n");
        return 1;
    }
    /* Sample receipt under lab/ (gitignored enrollments dir may not exist). */
#ifdef _WIN32
    (void)CreateDirectoryA("lab", NULL);
    (void)CreateDirectoryA("lab\\enrollments", NULL);
    (void)CreateDirectoryA("lab\\enrollments\\demo", NULL);
    f = fopen("lab\\enrollments\\demo\\enrollment.txt", "wb");
#else
    (void)system("mkdir -p lab/enrollments/demo");
    f = fopen("lab/enrollments/demo/enrollment.txt", "wb");
#endif
    if (f == NULL) {
        fprintf(stderr, "demo: cannot write receipt\n");
        return 1;
    }
    fputs("ATN-ENROLL-1\nphone_number_label=+61400000000\n"
          "note=atnenroll demo DEC-0042\n", f);
    fclose(f);
    printf("atnenroll demo: phone label gate + sample receipt OK (DEC-0042)\n");
    return 0;
}

static int cmd_serve(const char *port)
{
#ifdef _WIN32
    char cmd[512];
    int n;
    n = snprintf(cmd, sizeof(cmd),
                 "powershell -NoProfile -ExecutionPolicy Bypass -File "
                 "tools\\enroll-console.ps1 -Port %s",
                 port != NULL ? port : "8787");
    if (n <= 0 || (size_t)n >= sizeof(cmd)) {
        return 1;
    }
    printf("atnenroll: starting loopback enroll UI on 127.0.0.1:%s\n",
           port != NULL ? port : "8787");
    printf("Open loopback port %s in a browser (Ctrl+C in that window to stop)\n",
           port != NULL ? port : "8787");
    return system(cmd);
#else
    (void)port;
    fprintf(stderr,
            "atnenroll serve: Windows lab console only for now (DEC-0042)\n");
    return 1;
#endif
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: atnenroll demo|serve [port]\n");
        return 1;
    }
    if (strcmp(argv[1], "demo") == 0) {
        return cmd_demo();
    }
    if (strcmp(argv[1], "serve") == 0) {
        return cmd_serve(argc >= 3 ? argv[2] : NULL);
    }
    fprintf(stderr, "usage: atnenroll demo|serve [port]\n");
    return 1;
}
