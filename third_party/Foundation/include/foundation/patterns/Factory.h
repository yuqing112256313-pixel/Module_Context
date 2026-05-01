#ifndef FOUNDATION_PATTERNS_FACTORY_H_
#define FOUNDATION_PATTERNS_FACTORY_H_

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

namespace foundation {
namespace patterns {

// ============================================================================
// Factory<Base, Key, Args...>
//
// Thread-safe runtime factory with optional constructor arguments.
//
// Template parameters:
//   Base  - abstract base/product type
//   Key   - registration key type, default std::string
//   Args  - creator argument list
//
// Notes:
//   1. Base should have a virtual destructor.
//   2. Creator returns std::unique_ptr<Base>.
//   3. Registration rejects duplicate keys.
//   4. Create copies the creator under lock, then invokes it outside the lock.
// ============================================================================
template <typename Base, typename Key = std::string, typename... Args>
class Factory : private foundation::base::NonCopyable {
public:
    typedef Base base_type;
    typedef Key key_type;
    typedef std::function<std::unique_ptr<Base>(Args...)> Creator;
    typedef std::map<Key, Creator> CreatorMap;

public:
    static Factory& Instance() {
        static Factory instance;
        return instance;
    }

public:
    foundation::base::Result<void> Register(const Key& key,
                                            const Creator& creator) {
        if (!creator) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kInvalidArgument,
                "Factory::Register failed: creator is empty");
        }

        std::lock_guard<std::mutex> lock(mutex_);

        typename CreatorMap::const_iterator it = creators_.find(key);
        if (it != creators_.end()) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kAlreadyExists,
                "Factory::Register failed: key already registered");
        }

        creators_[key] = creator;
        return foundation::base::Result<void>();
    }

    foundation::base::Result<void> Unregister(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        typename CreatorMap::iterator it = creators_.find(key);
        if (it == creators_.end()) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kNotFound,
                "Factory::Unregister failed: key not found");
        }

        creators_.erase(it);
        return foundation::base::Result<void>();
    }

    foundation::base::Result<std::unique_ptr<Base> > Create(const Key& key, Args... args) const {
        Creator creator;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            typename CreatorMap::const_iterator it = creators_.find(key);
            if (it == creators_.end()) {
                return foundation::base::Result<std::unique_ptr<Base> >(
                    foundation::base::ErrorCode::kNotFound,
                    "Factory::Create failed: key not registered");
            }

            creator = it->second;
        }

        std::unique_ptr<Base> instance = creator(args...);
        if (!instance) {
            return foundation::base::Result<std::unique_ptr<Base> >(
                foundation::base::ErrorCode::kOperationFailed,
                "Factory::Create failed: creator returned null");
        }

        return foundation::base::Result<std::unique_ptr<Base> >(
            std::move(instance));
    }

public:
    bool Contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return creators_.find(key) != creators_.end();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return creators_.size();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return creators_.empty();
    }

    std::vector<Key> Keys() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Key> keys;
        keys.reserve(creators_.size());

        typename CreatorMap::const_iterator it = creators_.begin();
        for (; it != creators_.end(); ++it) {
            keys.push_back(it->first);
        }

        return keys;
    }

    // Primarily intended for tests or controlled reset scenarios.
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        creators_.clear();
    }

private:
    Factory() {}
    ~Factory() {}

private:
    mutable std::mutex mutex_;
    CreatorMap creators_;
};

// ============================================================================
// FactoryRegistrar<FactoryType, Derived>
//
// Self-registration helper for default construction with argument forwarding.
//
// Derived must be constructible from Args... declared by FactoryType.
// ============================================================================
template <typename FactoryType, typename Derived>
class FactoryRegistrar;

template <typename Base, typename Key, typename... Args, typename Derived>
class FactoryRegistrar<Factory<Base, Key, Args...>, Derived>
    : private foundation::base::NonCopyable {
private:
    typedef Factory<Base, Key, Args...> FactoryType;

public:
    explicit FactoryRegistrar(const typename FactoryType::key_type& key)
        : registered_(false) {
        foundation::base::Result<void> result =
            FactoryType::Instance().Register(
                key,
                &FactoryRegistrar::Create);
        registered_ = result.IsOk();
    }

    bool IsRegistered() const {
        return registered_;
    }

private:
    static std::unique_ptr<typename FactoryType::base_type> Create(Args... args) {
        return std::unique_ptr<typename FactoryType::base_type>(
            new Derived(std::forward<Args>(args)...));
    }

private:
    bool registered_;
};

}  // namespace patterns
}  // namespace foundation

#define FOUNDATION_INTERNAL_CONCAT_IMPL_(a, b) a##b
#define FOUNDATION_INTERNAL_CONCAT_(a, b) FOUNDATION_INTERNAL_CONCAT_IMPL_(a, b)

#define FOUNDATION_REGISTER_CREATOR(FactoryType, DerivedType, KeyValue) \
    namespace { \
    ::foundation::patterns::FactoryRegistrar<FactoryType, DerivedType> \
        FOUNDATION_INTERNAL_CONCAT_(g_factory_registrar_, __LINE__)(KeyValue); \
    }

#endif  // FOUNDATION_PATTERNS_FACTORY_H_
