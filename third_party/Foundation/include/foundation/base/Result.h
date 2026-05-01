#ifndef FOUNDATION_BASE_RESULT_H_
#define FOUNDATION_BASE_RESULT_H_

#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "foundation/base/Assert.h"
#include "foundation/base/Compiler.h"
#include "foundation/base/ErrorCode.h"

namespace foundation {
namespace base {

// ---------------------------------------------------------------------------
// InPlaceValue – tag type for constructing a value in-place inside Result<T>.
//
//   return Result<Widget>(InPlaceValue{}, width, height);
// ---------------------------------------------------------------------------
struct InPlaceValue {
    explicit InPlaceValue() {}
};

namespace detail {

// Type constraint: T must not be a reference, raw array, void, or ErrorCode
// (the last one would make the value/error constructors ambiguous).
template <typename T>
struct IsResultValueType {
    static const bool value =
        !std::is_reference<T>::value &&
        !std::is_array<T>::value &&
        !std::is_same<typename std::remove_cv<T>::type, void>::value &&
        !std::is_same<typename std::remove_cv<T>::type, ErrorCode>::value;
};

template <typename T>
struct IsNothrowResultSwap {
    static const bool value =
        std::is_nothrow_move_constructible<T>::value &&
        std::is_nothrow_move_assignable<T>::value;
};

}  // namespace detail

// ===========================================================================
// Result<T>
//
// A discriminated union that holds either a value of type T (success) or an
// error code with an optional human-readable message (failure).
//
// Design decisions
// ----------------
//   * Implicit construction from T and ErrorCode is intentional so that
//     `return 42;` and `return ErrorCode::kNotFound;` work naturally.
//   * Copy assignment uses copy-and-swap for strong exception safety.
//   * Move operations are conditionally noexcept based on T.
//   * The class is marked NODISCARD so the compiler warns on ignored returns.
//   * Monadic helpers (Transform / AndThen / OrElse) reduce boilerplate.
//
// Typical usage
// -------------
//   Result<int> ParseInt(const std::string& s) {
//       int v;
//       if (TryParse(s, &v)) return v;
//       return Result<int>::Err(ErrorCode::kInvalidArgument, "bad integer");
//   }
//
//   auto r = ParseInt(input);
//   if (r) { Use(*r); }
//   else   { Log(r.GetError(), r.GetMessage()); }
//
// ===========================================================================
template <typename T>
class FOUNDATION_NODISCARD Result {
    static_assert(
        detail::IsResultValueType<T>::value,
        "Result<T>: T must not be a reference, array, void, or ErrorCode.");

public:
    typedef T value_type;

    // ---- Value constructors -------------------------------------------------

    Result(const T& value)
        : has_value_(false), error_(ErrorCode::kOk), message_() {
        ConstructValue(value);
    }

    Result(T&& value) noexcept(std::is_nothrow_move_constructible<T>::value)
        : has_value_(false), error_(ErrorCode::kOk), message_() {
        ConstructValue(std::move(value));
    }

    // Construct the value in-place from arbitrary arguments.
    //   Result<Widget> r(InPlaceValue{}, w, h);
    template <typename... Args>
    explicit Result(InPlaceValue, Args&&... args)
        : has_value_(false), error_(ErrorCode::kOk), message_() {
        ConstructValue(std::forward<Args>(args)...);
    }

    // ---- Error constructors -------------------------------------------------

    Result(ErrorCode error)
        : has_value_(false), error_(error), message_() {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result.");
    }

    Result(ErrorCode error, const std::string& message)
        : has_value_(false), error_(error), message_(message) {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result.");
    }

    Result(ErrorCode error, std::string&& message)
        : has_value_(false), error_(error), message_(std::move(message)) {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result.");
    }

    Result(ErrorCode error, const char* message)
        : has_value_(false), error_(error),
          message_(message ? message : "") {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result.");
    }

    // ---- Copy / Move / Destroy ----------------------------------------------

    Result(const Result& other)
        : has_value_(false), error_(other.error_), message_(other.message_) {
        if (other.has_value_) {
            ConstructValue(other.ValueRef());
        }
    }

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
        : has_value_(false), error_(other.error_),
          message_(std::move(other.message_)) {
        if (other.has_value_) {
            ConstructValue(std::move(other.ValueRef()));
        }
    }

    ~Result() {
        DestroyValue();
    }

    // Copy assignment: build a temporary copy first (may throw), then swap
    // (noexcept when T's move is noexcept).  This guarantees that *this is
    // never left in a half-modified state if the copy throws.
    Result& operator=(const Result& other) {
        if (this != &other) {
            Result temp(other);
            Swap(temp);
        }
        return *this;
    }

    // Move assignment: build a temporary from the rvalue (noexcept when T's
    // move is noexcept), then swap.
    Result& operator=(Result&& other)
        noexcept(detail::IsNothrowResultSwap<T>::value) {
        if (this != &other) {
            Result temp(std::move(other));
            Swap(temp);
        }
        return *this;
    }

    // ---- Static factory methods ---------------------------------------------

    static Result Ok(const T& value) { return Result(value); }
    static Result Ok(T&& value)      { return Result(std::move(value)); }

    template <typename... Args>
    static Result OkEmplace(Args&&... args) {
        return Result(InPlaceValue(), std::forward<Args>(args)...);
    }

    static Result Err(ErrorCode error) {
        return Result(error);
    }
    static Result Err(ErrorCode error, const std::string& message) {
        return Result(error, message);
    }
    static Result Err(ErrorCode error, std::string&& message) {
        return Result(error, std::move(message));
    }
    static Result Err(ErrorCode error, const char* message) {
        return Result(error, message);
    }

    // ---- Observers ----------------------------------------------------------

    bool IsOk()    const noexcept { return has_value_; }
    bool IsError() const noexcept { return !has_value_; }

    explicit operator bool() const noexcept { return has_value_; }

    ErrorCode GetError() const noexcept {
        return has_value_ ? ErrorCode::kOk : error_;
    }

    const std::string& GetMessage() const noexcept {
        return message_;
    }

    // ---- Value access -------------------------------------------------------

    const T& Value() const {
        FOUNDATION_ASSERT_MSG(has_value_,
            "Result::Value() called on an error result.");
        return ValueRef();
    }

    T& Value() {
        FOUNDATION_ASSERT_MSG(has_value_,
            "Result::Value() called on an error result.");
        return ValueRef();
    }

    const T& operator*() const { return Value(); }
    T& operator*() { return Value(); }

    const T* operator->() const {
        FOUNDATION_ASSERT_MSG(has_value_,
            "Result::operator->() called on an error result.");
        return &ValueRef();
    }

    T* operator->() {
        FOUNDATION_ASSERT_MSG(has_value_,
            "Result::operator->() called on an error result.");
        return &ValueRef();
    }

    // Returns the held value on success, or |default_value| on error.
    T ValueOr(const T& default_value) const {
        return has_value_ ? ValueRef() : default_value;
    }

    // Overload that avoids copying when the default is an rvalue.
    T ValueOr(T&& default_value) const {
        return has_value_ ? ValueRef() : std::move(default_value);
    }

    // ---- Monadic operations -------------------------------------------------
    //
    // These help flatten chains of Result-returning operations without
    // repetitive if-error-return boilerplate.

    // Transform: func(T) -> U.  Wraps the return value in Result<U>.
    //   result.Transform([](int x) { return std::to_string(x); })
    //   // Result<int> -> Result<std::string>
    template <typename F>
    auto Transform(F&& func) const
        -> Result<typename std::decay<
            decltype(func(std::declval<const T&>()))>::type> {
        typedef typename std::decay<
            decltype(func(std::declval<const T&>()))>::type U;
        if (has_value_) {
            return Result<U>(func(ValueRef()));
        }
        return Result<U>(error_, message_);
    }

    // AndThen: func(T) -> Result<U>.  Returns the Result directly.
    //   result.AndThen([](int x) -> Result<double> { return Sqrt(x); })
    template <typename F>
    auto AndThen(F&& func) const
        -> decltype(func(std::declval<const T&>())) {
        typedef decltype(func(std::declval<const T&>())) ReturnType;
        if (has_value_) {
            return func(ValueRef());
        }
        return ReturnType(error_, message_);
    }

    // OrElse: f(ErrorCode, string) -> Result<T>.  Called only on error.
    //   result.OrElse([](ErrorCode e, const std::string& m) {
    //       return Result<int>(0);  // fallback
    //   })
    template <typename F>
    Result OrElse(F&& func) const {
        if (has_value_) {
            return *this;
        }
        return func(error_, message_);
    }

    // ---- Swap ---------------------------------------------------------------

    void Swap(Result& other) noexcept(detail::IsNothrowResultSwap<T>::value) {
        if (this == &other) {
            return;
        }

        using std::swap;

        if (has_value_ && other.has_value_) {
            // Both hold values: swap values only; error fields are both kOk/"".
            swap(ValueRef(), other.ValueRef());
            return;
        }

        if (!has_value_ && !other.has_value_) {
            // Both hold errors: swap the error metadata.
            swap(error_, other.error_);
            swap(message_, other.message_);
            return;
        }

        // Cross-state: one value, one error.
        Result& with_value = has_value_ ? *this : other;
        Result& with_error = has_value_ ? other : *this;

        // 1. Save the value.
        T tmp(std::move(with_value.ValueRef()));
        with_value.DestroyValue();

        // 2. Transfer error metadata to the previously-value side.
        with_value.error_   = with_error.error_;
        with_value.message_ = std::move(with_error.message_);

        // 3. Construct the value on the previously-error side.
        //    ConstructValue resets error_ and message_ internally.
        with_error.ConstructValue(std::move(tmp));
    }

    friend void swap(Result& lhs, Result& rhs) noexcept(noexcept(lhs.Swap(rhs))) {
        lhs.Swap(rhs);
    }

private:
    // ---- Raw storage access (no assertions; private use only) ---------------

    T& ValueRef() noexcept {
        return *reinterpret_cast<T*>(storage_);
    }

    const T& ValueRef() const noexcept {
        return *reinterpret_cast<const T*>(storage_);
    }

    // ---- Construct / Destroy helpers ----------------------------------------

    template <typename... Args>
    void ConstructValue(Args&&... args) {
        ::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
        has_value_ = true;
        error_ = ErrorCode::kOk;
        message_.clear();
    }

    void DestroyValue() noexcept {
        if (has_value_) {
            ValueRef().~T();
            has_value_ = false;
        }
    }

    // ---- Data members -------------------------------------------------------

    // `alignas(T)` ensures proper alignment for placement-new of T.
    // Using a raw byte array instead of the deprecated std::aligned_storage.
    alignas(T) unsigned char storage_[sizeof(T)];
    bool has_value_;
    ErrorCode error_;
    std::string message_;
};

// ===========================================================================
// Result<void> – specialization for operations with no return value.
//
//   Result<void> Initialize() {
//       if (!ready) return ErrorCode::kUnavailable;
//       return Result<void>::Success();
//   }
//
// All five special member functions are compiler-generated (the class holds
// only trivially-destructible / standard-library types).
// ===========================================================================
template <>
class FOUNDATION_NODISCARD Result<void> {
public:
    // ---- Constructors -------------------------------------------------------

    Result() noexcept
        : ok_(true), error_(ErrorCode::kOk), message_() {}
	
    Result(ErrorCode error)
        : ok_(false), error_(error), message_() {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result<void>.");
    }

    Result(ErrorCode error, const std::string& message)
        : ok_(false), error_(error), message_(message) {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result<void>.");
    }

    Result(ErrorCode error, std::string&& message)
        : ok_(false), error_(error), message_(std::move(message)) {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result<void>.");
    }

    Result(ErrorCode error, const char* message)
        : ok_(false), error_(error), message_(message ? message : "") {
        FOUNDATION_ASSERT_MSG(error != ErrorCode::kOk,
            "ErrorCode::kOk cannot be used to construct an error Result<void>.");
    }

    // Compiler-generated copy/move/destructor are correct for this class.

    // ---- Factory methods ----------------------------------------------------

    static Result Success() { return Result(); }
    static Result Err(ErrorCode error) {
        return Result(error);
    }
    static Result Err(ErrorCode error, const std::string& message) {
        return Result(error, message);
    }
    static Result Err(ErrorCode error, std::string&& message) {
        return Result(error, std::move(message));
    }
    static Result Err(ErrorCode error, const char* message) {
        return Result(error, message);
    }

    // ---- Observers ----------------------------------------------------------

    bool IsOk() const noexcept { return ok_; }
    bool IsError() const noexcept { return !ok_; }

    explicit operator bool() const noexcept { return ok_; }

    ErrorCode GetError() const noexcept {
        return ok_ ? ErrorCode::kOk : error_;
    }

    const std::string& GetMessage() const noexcept {
        return message_;
    }

    // ---- Swap ---------------------------------------------------------------

    void Swap(Result& other) noexcept {
        if (this == &other) {
            return;
        }

        using std::swap;
        swap(ok_, other.ok_);
        swap(error_, other.error_);
        swap(message_, other.message_);
    }

    friend void swap(Result& lhs, Result& rhs) noexcept {
        lhs.Swap(rhs);
    }

private:
    bool ok_;
    ErrorCode error_;
    std::string message_;
};

// ===========================================================================
// Free helper functions
// ===========================================================================

template <typename T>
inline Result<typename std::decay<T>::type> MakeResult(T&& value) {
    typedef typename std::decay<T>::type ValueType;
    return Result<ValueType>::Ok(std::forward<T>(value));
}

template <typename T>
inline Result<T> MakeErrorResult(ErrorCode error) {
    return Result<T>::Err(error);
}

template <typename T>
inline Result<T> MakeErrorResult(ErrorCode error, const std::string& message) {
    return Result<T>::Err(error, message);
}

template <typename T>
inline Result<T> MakeErrorResult(ErrorCode error, std::string&& message) {
    return Result<T>::Err(error, std::move(message));
}

template <typename T>
inline Result<T> MakeErrorResult(ErrorCode error, const char* message) {
    return Result<T>::Err(error, message);
}

inline Result<void> MakeSuccess() {
    return Result<void>::Success();
}

inline Result<void> MakeError(ErrorCode error) {
    return Result<void>::Err(error);
}

inline Result<void> MakeError(ErrorCode error, const std::string& message) {
    return Result<void>::Err(error, message);
}

inline Result<void> MakeError(ErrorCode error, std::string&& message) {
    return Result<void>::Err(error, std::move(message));
}

inline Result<void> MakeError(ErrorCode error, const char* message) {
    return Result<void>::Err(error, message);
}

}  // namespace base
}  // namespace foundation

#endif  // FOUNDATION_BASE_RESULT_H_