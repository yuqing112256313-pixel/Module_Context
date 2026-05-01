#ifndef FOUNDATION_BASE_COMPILER_H_
#define FOUNDATION_BASE_COMPILER_H_

/*
 * Compiler detection
 */
#if defined(_MSC_VER)
#define FOUNDATION_COMPILER_MSVC 1
#else
#define FOUNDATION_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define FOUNDATION_COMPILER_CLANG 1
#else
#define FOUNDATION_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define FOUNDATION_COMPILER_GCC 1
#else
#define FOUNDATION_COMPILER_GCC 0
#endif

#if !FOUNDATION_COMPILER_MSVC && !FOUNDATION_COMPILER_CLANG && !FOUNDATION_COMPILER_GCC
#define FOUNDATION_COMPILER_UNKNOWN 1
#else
#define FOUNDATION_COMPILER_UNKNOWN 0
#endif

/*
 * Compiler version
 */
#if FOUNDATION_COMPILER_MSVC
#define FOUNDATION_MSVC_VERSION _MSC_VER
#endif

/*
 * Function name
 */
#if FOUNDATION_COMPILER_MSVC
#define FOUNDATION_FUNCTION __FUNCTION__
#else
#define FOUNDATION_FUNCTION __func__
#endif

/*
 * [[nodiscard]] / warn_unused_result
 *
 * Use standard [[nodiscard]] when available.
 * Fallback to compiler-specific attributes for older compilers.
 */
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(nodiscard)
#define FOUNDATION_NODISCARD [[nodiscard]]
#elif FOUNDATION_COMPILER_GCC || FOUNDATION_COMPILER_CLANG
#define FOUNDATION_NODISCARD __attribute__((warn_unused_result))
#else
#define FOUNDATION_NODISCARD
#endif
#elif FOUNDATION_COMPILER_GCC || FOUNDATION_COMPILER_CLANG
#define FOUNDATION_NODISCARD __attribute__((warn_unused_result))
#else
#define FOUNDATION_NODISCARD
#endif

/*
 * Inline / noinline
 */
#if FOUNDATION_COMPILER_MSVC
#define FOUNDATION_FORCE_INLINE __forceinline
#define FOUNDATION_NO_INLINE __declspec(noinline)
#elif FOUNDATION_COMPILER_GCC || FOUNDATION_COMPILER_CLANG
#define FOUNDATION_FORCE_INLINE inline __attribute__((always_inline))
#define FOUNDATION_NO_INLINE __attribute__((noinline))
#else
#define FOUNDATION_FORCE_INLINE inline
#define FOUNDATION_NO_INLINE
#endif

/*
 * Deprecated
 */
#if FOUNDATION_COMPILER_MSVC
#define FOUNDATION_DEPRECATED __declspec(deprecated)
#elif FOUNDATION_COMPILER_GCC || FOUNDATION_COMPILER_CLANG
#define FOUNDATION_DEPRECATED __attribute__((deprecated))
#else
#define FOUNDATION_DEPRECATED
#endif

/*
 * Warning control - MSVC
 */
#if FOUNDATION_COMPILER_MSVC
#define FOUNDATION_WARNING_PUSH __pragma(warning(push))
#define FOUNDATION_WARNING_POP  __pragma(warning(pop))
#define FOUNDATION_WARNING_DISABLE_MSVC(w) __pragma(warning(disable: w))
#else
#define FOUNDATION_WARNING_PUSH
#define FOUNDATION_WARNING_POP
#define FOUNDATION_WARNING_DISABLE_MSVC(w)
#endif

/*
 * Warning control - GCC
 *
 * Usage:
 *   FOUNDATION_WARNING_PUSH_GCC
 *   FOUNDATION_WARNING_DISABLE_GCC(GCC diagnostic ignored "-Wunused-parameter")
 *   ...
 *   FOUNDATION_WARNING_POP_GCC
 */
#if FOUNDATION_COMPILER_GCC
#define FOUNDATION_WARNING_PUSH_GCC _Pragma("GCC diagnostic push")
#define FOUNDATION_WARNING_POP_GCC  _Pragma("GCC diagnostic pop")
#define FOUNDATION_WARNING_DISABLE_GCC(w) _Pragma(#w)
#else
#define FOUNDATION_WARNING_PUSH_GCC
#define FOUNDATION_WARNING_POP_GCC
#define FOUNDATION_WARNING_DISABLE_GCC(w)
#endif

/*
 * Warning control - Clang
 *
 * Usage:
 *   FOUNDATION_WARNING_PUSH_CLANG
 *   FOUNDATION_WARNING_DISABLE_CLANG(clang diagnostic ignored "-Wunused-parameter")
 *   ...
 *   FOUNDATION_WARNING_POP_CLANG
 */
#if FOUNDATION_COMPILER_CLANG
#define FOUNDATION_WARNING_PUSH_CLANG _Pragma("clang diagnostic push")
#define FOUNDATION_WARNING_POP_CLANG  _Pragma("clang diagnostic pop")
#define FOUNDATION_WARNING_DISABLE_CLANG(w) _Pragma(#w)
#else
#define FOUNDATION_WARNING_PUSH_CLANG
#define FOUNDATION_WARNING_POP_CLANG
#define FOUNDATION_WARNING_DISABLE_CLANG(w)
#endif

#endif  // FOUNDATION_BASE_COMPILER_H_