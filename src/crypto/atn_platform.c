/*
 * Module: atn_platform.c
 * REQ:    REQ-1.1 (build identity)
 * Spec:   DEC-0004 — report the compiler's target, not the builder's uname.
 */

#include "atn_platform.h"

const char *atn_platform_id(void)
{
#if defined(ATN_OS_WINDOWS) && defined(ATN_ARCH_AARCH64)
    return "windows-aarch64";
#elif defined(ATN_OS_WINDOWS) && defined(ATN_ARCH_ARM)
    return "windows-arm";
#elif defined(ATN_OS_WINDOWS) && defined(ATN_ARCH_X86_64)
    return "windows-x86_64";
#elif defined(ATN_OS_WINDOWS) && defined(ATN_ARCH_X86)
    return "windows-x86";
#elif defined(ATN_OS_WINDOWS)
    return "windows-unknown";
#elif defined(ATN_OS_ANDROID) && defined(ATN_ARCH_AARCH64)
    return "android-aarch64";
#elif defined(ATN_OS_ANDROID) && defined(ATN_ARCH_ARM)
    return "android-arm";
#elif defined(ATN_OS_ANDROID) && defined(ATN_ARCH_X86_64)
    return "android-x86_64";
#elif defined(ATN_OS_ANDROID)
    return "android-unknown";
#elif defined(ATN_OS_LINUX) && defined(ATN_ARCH_AARCH64)
    return "linux-aarch64";
#elif defined(ATN_OS_LINUX) && defined(ATN_ARCH_ARM)
    return "linux-arm";
#elif defined(ATN_OS_LINUX) && defined(ATN_ARCH_X86_64)
    return "linux-x86_64";
#elif defined(ATN_OS_LINUX) && defined(ATN_ARCH_X86)
    return "linux-x86";
#elif defined(ATN_OS_LINUX) && defined(ATN_ARCH_RISCV64)
    return "linux-riscv64";
#elif defined(ATN_OS_LINUX)
    return "linux-unknown";
#elif defined(ATN_OS_DARWIN) && defined(ATN_ARCH_AARCH64)
    return "darwin-aarch64";
#elif defined(ATN_OS_DARWIN) && defined(ATN_ARCH_X86_64)
    return "darwin-x86_64";
#elif defined(ATN_OS_DARWIN)
    return "darwin-unknown";
#elif defined(ATN_OS_BSD) && defined(ATN_ARCH_AARCH64)
    return "bsd-aarch64";
#elif defined(ATN_OS_BSD) && defined(ATN_ARCH_X86_64)
    return "bsd-x86_64";
#elif defined(ATN_OS_BSD)
    return "bsd-unknown";
#elif defined(ATN_OS_UNIX)
    return "unix-unknown";
#else
    return "unknown";
#endif
}
