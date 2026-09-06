/*
 * Portable mutex (DEC-0025). Windows CRITICAL_SECTION, POSIX pthread.
 * Recursive so ingest may emit.
 */
#ifndef ATN_SYNC_H
#define ATN_SYNC_H

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
typedef CRITICAL_SECTION atn_lock;
#else
#  include <pthread.h>
typedef pthread_mutex_t atn_lock;
#endif

void atn_lock_init(atn_lock *l);
void atn_lock_acquire(atn_lock *l);
void atn_lock_release(atn_lock *l);
void atn_lock_fini(atn_lock *l);

#endif
