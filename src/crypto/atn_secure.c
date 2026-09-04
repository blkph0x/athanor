/*
 * Module: atn_secure.c
 * REQ:    REQ-1.1
 * Spec:   RFC 8439 §4 (constant-time compare); DEC-0002 (OS CSPRNG, zeroize)
 *
 * Side channels: atn_ct_equal must visit every byte. atn_memzero writes
 * through a volatile pointer so a compiler is less likely to drop the store.
 * Neither function claims to beat a debugger or a DMA device.
 */

#include "atn_crypto.h"

#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <sys/random.h>
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

#if defined(_WIN32)
    {
        NTSTATUS st;
        if (n > 0xffffffffu) {
            return ATN_ERR_LEN;
        }
        /* BCRYPT_USE_SYSTEM_PREFERRED_RNG = 0x00000002, documented in bcrypt.h */
        st = BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (st != 0) {
            atn_memzero(out, n);
            return ATN_ERR_ENTROPY;
        }
        return ATN_OK;
    }
#else
    {
        size_t filled = 0;
#  if defined(__linux__)
        while (filled < n) {
            ssize_t r = getrandom(out + filled, n - filled, 0);
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
                ssize_t r = read(fd, out + filled, n - filled);
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
