#ifndef FOUNDATION_PATTERNS_SINGLETON_H_
#define FOUNDATION_PATTERNS_SINGLETON_H_

#include <type_traits>

#include "foundation/base/NonCopyable.h"

namespace foundation {
namespace patterns {

// -----------------------------------------------------------------------------
// Singleton<T>
//
// A CRTP-style singleton helper based on function-local static initialization.
// C++11 guarantees thread-safe initialization of local statics.
//
// Usage:
//
//   class MyService : public foundation::patterns::Singleton<MyService> {
//       friend class foundation::patterns::Singleton<MyService>;
//
//   private:
//       MyService() = default;
//       ~MyService() = default;
//
//   public:
//       void DoWork();
//   };
//
//   MyService::Instance().DoWork();
//
// Notes:
//   1. T must inherit from Singleton<T>.
//   2. T should make Singleton<T> a friend if T's constructor is non-public.
//   3. This helper prevents copying/moving of the singleton base, but uniqueness
//      of T still depends on T restricting its own construction appropriately.
// -----------------------------------------------------------------------------
template <typename T>
class Singleton : private base::NonCopyable {
public:
    static T& Instance() {
        static_assert(std::is_base_of<Singleton<T>, T>::value,
                      "T must inherit from foundation::patterns::Singleton<T>.");

        static T instance;
        return instance;
    }

protected:
    Singleton() = default;
    ~Singleton() = default;
};

}  // namespace patterns
}  // namespace foundation

#endif  // FOUNDATION_PATTERNS_SINGLETON_H_