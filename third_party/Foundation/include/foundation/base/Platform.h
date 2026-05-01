#ifndef FOUNDATION_BASE_PLATFORM_H_
#define FOUNDATION_BASE_PLATFORM_H_

// Platform
#if defined(_WIN32) || defined(_WIN64)
    #define FOUNDATION_PLATFORM_WINDOWS 1
#else
    #define FOUNDATION_PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
    #define FOUNDATION_PLATFORM_LINUX 1
#else
    #define FOUNDATION_PLATFORM_LINUX 0
#endif

#if !FOUNDATION_PLATFORM_WINDOWS && !FOUNDATION_PLATFORM_LINUX
    #error "Unsupported platform"
#endif

// Architecture
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
    #define FOUNDATION_ARCH_64 1
    #define FOUNDATION_ARCH_32 0
#else
    #define FOUNDATION_ARCH_64 0
    #define FOUNDATION_ARCH_32 1
#endif

// Path separator
#if FOUNDATION_PLATFORM_WINDOWS
    #define FOUNDATION_PATH_SEPARATOR '\\'
#else
    #define FOUNDATION_PATH_SEPARATOR '/'
#endif

#endif // FOUNDATION_BASE_PLATFORM_H_