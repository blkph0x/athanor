/*
 * Module: atn_secure.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §4 (constant-time compare); DEC-0002 / DEC-0004 (OS CSPRNG)
 *
 * CSPRNG is the OS's, selected by compiler OS macros — not by uname, not by
 * a guessed device node:
 *   Windows (x86/x64/ARM/ARM64): BCryptGenRandom  [bcrypt.h]
 *   Darwin / BSD:                arc4random_buf   [stdlib.h]
 *   Linux / Android:             getrandom, then /dev/urandom
 *   other unix:                  /dev/urandom
 *
 * Side channels: atn_ct_equal visits every byte. atn_memzero writes through
 * a volatile pointer. Neither claims to beat DMA or a debugger.
 */

#if defined(__APPLE__)
#  if !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE 1
#  endif
#elif !defined(_WIN32)
#  if !defined(_DEFAULT_SOURCE)
#    define _DEFAULT_SOURCE 1
#  endif
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "atn_crypto.h"

#include <string.h>

#if defined(ATN_OS_WINDOWS)
#  include <windows.h>
#  include <bcrypt.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "bcrypt.lib")
#  endif
#elif defined(ATN_OS_DARWIN) || defined(ATN_OS_BSD)
#  include <stdlib.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(ATN_OS_LINUX) && !defined(__ANDROID__)
#    include <sys/random.h>
#    define ATN_HAVE_GETRANDOM 1
#  elif defined(__ANDROID__) && defined(__ANDROID_API__) && (__ANDROID_API__ >= 28)
#    include <sys/random.h>
#    define ATN_HAVE_GETRANDOM 1
#  endif
#endif

void atn_memzero(void *p, size_t n)
{
    volatile uint8_t *v;
    size_t i;

    if (p == NULL || n == 0) {
        return;
    }
    v = (volatile uint8_t *)p;
    for (i = 0; i < n; i++) {
        v[i] = 0;
    }
}

int atn_ct_equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t acc = 0;
    size_t i;

    if (n == 0) {
        return 1;
    }
    if (x == NULL || y == NULL) {
        return 0;
    }
    /* RFC 8439 §4: do not reveal the first differing byte via timing. */
    for (i = 0; i < n; i++) {
        acc |= (uint8_t)(x[i] ^ y[i]);
    }
    return acc == 0;
}

int atn_random_bytes(void *buf, size_t n)
{
    uint8_t *out = (uint8_t *)buf;

    if (n == 0) {
        return ATN_OK;
    }
    if (out == NULL) {
        return ATN_ERR_PARAM;
    }

#if defined(ATN_OS_WINDOWS)
    {
        /* BCryptGenRandom length is ULONG. Chunk so size_t > 4GiB still works
         * on Win64, and Windows ARM64 uses the same API as x64. */
        size_t filled = 0;
        while (filled < n) {
            unsigned long chunk;
            NTSTATUS st;
            size_t left = n - filled;
            if (left > 0xfffffffful) {
                chunk = 0xfffffffful;
            } else {
                chunk = (unsigned long)left;
            }
            st = BCryptGenRandom(NULL, out + filled, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (st != 0) {
                atn_memzero(out, n);
                return ATN_ERR_ENTROPY;
            }
            filled += (size_t)chunk;
        }
        return ATN_OK;
    }
#elif defined(ATN_OS_DARWIN) || defined(ATN_OS_BSD)
    /* arc4random_buf(3): CSPRNG, fills n bytes. Darwin, OpenBSD, FreeBSD, NetBSD. */
    arc4random_buf(out, n);
    return ATN_OK;
#else
    {
        size_t filled = 0;
#  if defined(ATN_HAVE_GETRANDOM)
        while (filled < n) {
            size_t left = n - filled;
            ssize_t r;
            /* Linux getrandom(2): short reads are possible above 256 bytes. */
            if (left > 256u) {
                left = 256u;
            }
            r = getrandom(out + filled, left, 0);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break; /* fall through to /dev/urandom */
            }
            filled += (size_t)r;
        }
        if (filled == n) {
            return ATN_OK;
        }
#  endif
        {
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd < 0) {
                atn_memzero(out, n);
                return ATN_ERR_ENTROPY;
            }
            while (filled < n) {
                size_t left = n - filled;
                ssize_t r;
                if (left > 1048576u) {
                    left = 1048576u;
                }
                r = read(fd, out + filled, left);
                if (r < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    close(fd);
                    atn_memzero(out, n);
                    return ATN_ERR_ENTROPY;
                }
                if (r == 0) {
                    close(fd);
                    atn_memzero(out, n);
                    return ATN_ERR_ENTROPY;
                }
                filled += (size_t)r;
            }
            close(fd);
            return ATN_OK;
        }
    }
#endif
}
