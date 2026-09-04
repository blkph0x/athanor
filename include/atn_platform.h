/*
 * Athanor platform identification (DEC-0004).
 *
 * Purpose:  Compiler-defined OS/arch only. No uname, no runtime guess.
 * Spec:     Predefined macros from the C compiler targeting this translation.
 * Policy:   If a target is missing, add a DEC and a CSPRNG path — do not
 *           pretend /dev/urandom exists on every unknown OS.
 */
#ifndef ATN_PLATFORM_H
#define ATN_PLATFORM_H

#if defined(_WIN32)
#  define ATN_OS_WINDOWS 1
#elif defined(__APPLE__)
#  define ATN_OS_DARWIN 1
#elif defined(__ANDROID__)
#  define ATN_OS_ANDROID 1
#  define ATN_OS_LINUX 1
#elif defined(__linux__)
#  define ATN_OS_LINUX 1
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
      || defined(__DragonFly__)
#  define ATN_OS_BSD 1
#elif defined(__unix__) || defined(__unix)
#  define ATN_OS_UNIX 1
#else
#  error "Unknown OS: add an OS CSPRNG path in atn_secure.c and record DEC-xxxx. Do not guess."
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#  define ATN_ARCH_AARCH64 1
#elif defined(__arm__) || defined(_M_ARM)
#  define ATN_ARCH_ARM 1
#elif defined(__x86_64__) || defined(_M_X64)
#  define ATN_ARCH_X86_64 1
#elif defined(__i386__) || defined(_M_IX86)
#  define ATN_ARCH_X86 1
#elif defined(__riscv) && (__riscv_xlen == 64)
#  define ATN_ARCH_RISCV64 1
#else
#  define ATN_ARCH_UNKNOWN 1
#endif

/*
 * Purpose: Static string such as "linux-aarch64" or "windows-x86_64".
 * Returns: Pointer to a persistent literal. Never NULL.
 */
const char *atn_platform_id(void);

#endif /* ATN_PLATFORM_H */
