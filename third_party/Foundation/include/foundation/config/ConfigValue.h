#ifndef FOUNDATION_CONFIG_CONFIGVALUE_H_
#define FOUNDATION_CONFIG_CONFIGVALUE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Export.h"
#include "foundation/base/Result.h"

namespace foundation {
namespace config {

class FOUNDATION_API ConfigValue {
public:
    enum class Type {
        kNull = 0,
        kBool = 1,
        kInt64 = 2,
        kDouble = 3,
        kString = 4,
        kArray = 5,
        kObject = 6
    };

    typedef std::vector<ConfigValue> Array;
    typedef std::map<std::string, ConfigValue> Object;

public:
    ConfigValue();
    explicit ConfigValue(bool value);
    explicit ConfigValue(int value);
    explicit ConfigValue(std::int64_t value);
    explicit ConfigValue(double value);
    explicit ConfigValue(const std::string& value);
    explicit ConfigValue(const char* value);
    explicit ConfigValue(const Array& value);
    explicit ConfigValue(const Object& value);

    ConfigValue(const ConfigValue& other);
    ConfigValue(ConfigValue&& other);
    ConfigValue& operator=(const ConfigValue& other);
    ConfigValue& operator=(ConfigValue&& other);
    ~ConfigValue();

public:
    Type GetType() const;

    bool IsNull() const;
    bool IsBool() const;
    bool IsInt64() const;
    bool IsDouble() const;
    bool IsString() const;
    bool IsArray() const;
    bool IsObject() const;
    bool IsNumber() const;

public:
    foundation::base::Result<bool> AsBool() const;
    foundation::base::Result<std::int64_t> AsInt64() const;
    foundation::base::Result<double> AsDouble() const;
    foundation::base::Result<std::string> AsString() const;
    foundation::base::Result<Array> AsArray() const;
    foundation::base::Result<Object> AsObject() const;

public:
    static ConfigValue MakeArray();
    static ConfigValue MakeObject();

    foundation::base::Result<void> Append(const ConfigValue& value);
    foundation::base::Result<std::size_t> Size() const;
    bool Empty() const;
    foundation::base::Result<ConfigValue> ArrayGet(std::size_t index) const;
    const Array* GetArray() const;

public:
    foundation::base::Result<void> Set(const std::string& key,
                                       const ConfigValue& value);
    bool Contains(const std::string& key) const;
    foundation::base::Result<ConfigValue> ObjectGet(
        const std::string& key) const;
    std::vector<std::string> ObjectKeys() const;
    const Object* GetObject() const;

public:
    foundation::base::Result<ConfigValue> GetByPath(
        const std::string& dot_path) const;

private:
    Type type_;
    bool bool_value_;
    std::int64_t int64_value_;
    double double_value_;
    std::string string_value_;
    Array array_value_;
    Object object_value_;
};

}  // namespace config
}  // namespace foundation

#endif  // FOUNDATION_CONFIG_CONFIGVALUE_H_
