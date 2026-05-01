#include "foundation/base/Assert.h"
#include <cstdio>
#include <cstdlib>

namespace foundation {
namespace base {

void HandleAssertFailure(const char* expression,
                         const char* file,
                         int line,
                         const char* function,
                         const char* message) {
    std::fprintf(stderr,
                 "Assertion failed!\n"
                 "  Expression: %s\n"
                 "  File      : %s\n"
                 "  Line      : %d\n"
                 "  Function  : %s\n"
                 "  Message   : %s\n",
                 expression ? expression : "",
                 file ? file : "",
                 line,
                 function ? function : "",
                 message ? message : "");

    std::fflush(stderr);
    std::abort();
}

} // namespace base
} // namespace foundation