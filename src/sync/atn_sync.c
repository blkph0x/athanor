/*
 * Module: atn_sync.c
 * REQ:    REQ-3.3 / 2.1
 * Spec:   DEC-0025. OS mutex only; not a third-party runtime.
 */

#include "atn_sync.h"

void atn_lock_init(atn_lock *l)
{
    if (l == NULL) {
        return;
    }
#if defined(_WIN32)
    InitializeCriticalSection(l);
#else
    {
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(l, &a);
        pthread_mutexattr_destroy(&a);
    }
#endif
}

void atn_lock_acquire(atn_lock *l)
{
    if (l == NULL) {
        return;
    }
#if defined(_WIN32)
    EnterCriticalSection(l);
#else
    pthread_mutex_lock(l);
#endif
}

void atn_lock_release(atn_lock *l)
{
    if (l == NULL) {
        return;
    }
#if defined(_WIN32)
    LeaveCriticalSection(l);
#else
    pthread_mutex_unlock(l);
#endif
}

void atn_lock_fini(atn_lock *l)
{
    if (l == NULL) {
        return;
    }
#if defined(_WIN32)
    DeleteCriticalSection(l);
#else
    pthread_mutex_destroy(l);
#endif
}
