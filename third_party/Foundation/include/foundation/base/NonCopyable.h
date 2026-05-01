#ifndef FOUNDATION_BASE_NONCOPYABLE_H_
#define FOUNDATION_BASE_NONCOPYABLE_H_

namespace foundation {
namespace base {

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

}  // namespace base
}  // namespace foundation

#endif  // FOUNDATION_BASE_NONCOPYABLE_H_
