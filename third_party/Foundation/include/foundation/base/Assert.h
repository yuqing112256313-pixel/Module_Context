#ifndef FOUNDATION_BASE_ASSERT_H_
#define FOUNDATION_BASE_ASSERT_H_

#include "foundation/base/Export.h"
#include <cstdlib>

namespace foundation {
namespace base {

FOUNDATION_API void HandleAssertFailure(const char* expression,
                                        const char* file,
                                        int line,
                                        const char* function,
                                        const char* message);

} // namespace base
} // namespace foundation

#if defined(_MSC_VER)
#define FOUNDATION_FUNCTION_NAME __FUNCTION__
#else
#define FOUNDATION_FUNCTION_NAME __func__
#endif

#if !defined(NDEBUG)
#define FOUNDATION_ASSERT(expr)                                                      \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ::foundation::base::HandleAssertFailure(#expr, __FILE__, __LINE__,       \
                                                    FOUNDATION_FUNCTION_NAME, "");    \
        }                                                                            \
    } while (0)

#define FOUNDATION_ASSERT_MSG(expr, msg)                                             \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ::foundation::base::HandleAssertFailure(#expr, __FILE__, __LINE__,       \
                                                    FOUNDATION_FUNCTION_NAME, (msg)); \
        }                                                                            \
    } while (0)
#else
#define FOUNDATION_ASSERT(expr) ((void)0)
#define FOUNDATION_ASSERT_MSG(expr, msg) ((void)0)
#endif

#define FOUNDATION_CHECK(expr)                                                       \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ::foundation::base::HandleAssertFailure(#expr, __FILE__, __LINE__,       \
                                                    FOUNDATION_FUNCTION_NAME, "");    \
        }                                                                            \
    } while (0)

#define FOUNDATION_CHECK_MSG(expr, msg)                                              \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ::foundation::base::HandleAssertFailure(#expr, __FILE__, __LINE__,       \
                                                    FOUNDATION_FUNCTION_NAME, (msg)); \
        }                                                                            \
    } while (0)

#endif // FOUNDATION_BASE_ASSERT_H_