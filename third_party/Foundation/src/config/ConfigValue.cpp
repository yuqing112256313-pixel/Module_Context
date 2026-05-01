#include "foundation/config/ConfigValue.h"

#include "foundation/strings/StringUtils.h"

namespace foundation {
namespace config {

ConfigValue::ConfigValue()
    : type_(Type::kNull),
      bool_value_(false),
      int64_value_(0),
      double_value_(0.0),
      string_value_(),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(bool value)
    : type_(Type::kBool),
      bool_value_(value),
      int64_value_(0),
      double_value_(0.0),
      string_value_(),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(int value)
    : type_(Type::kInt64),
      bool_value_(false),
      int64_value_(static_cast<std::int64_t>(value)),
      double_value_(0.0),
      string_value_(),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(std::int64_t value)
    : type_(Type::kInt64),
      bool_value_(false),
      int64_value_(value),
      double_value_(0.0),
      string_value_(),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(double value)
    : type_(Type::kDouble),
      bool_value_(false),
      int64_value_(0),
      double_value_(value),
      string_value_(),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(const std::string& value)
    : type_(Type::kString),
      bool_value_(false),
      int64_value_(0),
      double_value_(0.0),
      string_value_(value),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(const char* value)
    : type_(Type::kString),
      bool_value_(false),
      int64_value_(0),
      double_value_(0.0),
      string_value_(value == NULL ? "" : value),
      array_value_(),
      object_value_() {
}

ConfigValue::ConfigValue(const Array& value)
    : type_(Type::kArray),
      bool_value_(false),
      int64_value_(0),
      double_value_(0.0),
      string_value_(),
      array_value_(value),
      object_value_() {
}

ConfigValue::ConfigValue(const Object& value)
    : type_(Type::kObject),
      bool_value_(false),
      int64_value_(0),
      double_value_(0.0),
      string_value_(),
      array_value_(),
      object_value_(value) {
}

ConfigValue::ConfigValue(const ConfigValue& other) = default;
ConfigValue::ConfigValue(ConfigValue&& other) = default;
ConfigValue& ConfigValue::operator=(const ConfigValue& other) = default;
ConfigValue& ConfigValue::operator=(ConfigValue&& other) = default;
ConfigValue::~ConfigValue() = default;

ConfigValue::Type ConfigValue::GetType() const {
    return type_;
}

bool ConfigValue::IsNull() const {
    return type_ == Type::kNull;
}

bool ConfigValue::IsBool() const {
    return type_ == Type::kBool;
}

bool ConfigValue::IsInt64() const {
    return type_ == Type::kInt64;
}

bool ConfigValue::IsDouble() const {
    return type_ == Type::kDouble;
}

bool ConfigValue::IsString() const {
    return type_ == Type::kString;
}

bool ConfigValue::IsArray() const {
    return type_ == Type::kArray;
}

bool ConfigValue::IsObject() const {
    return type_ == Type::kObject;
}

bool ConfigValue::IsNumber() const {
    return IsInt64() || IsDouble();
}

foundation::base::Result<bool> ConfigValue::AsBool() const {
    if (!IsBool()) {
        return foundation::base::Result<bool>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not bool");
    }
    return foundation::base::Result<bool>(bool_value_);
}

foundation::base::Result<std::int64_t> ConfigValue::AsInt64() const {
    if (IsInt64()) {
        return foundation::base::Result<std::int64_t>(int64_value_);
    }
    if (IsDouble()) {
        return foundation::base::Result<std::int64_t>(
            static_cast<std::int64_t>(double_value_));
    }
    return foundation::base::Result<std::int64_t>(
        foundation::base::ErrorCode::kInvalidState,
        "ConfigValue is not int64");
}

foundation::base::Result<double> ConfigValue::AsDouble() const {
    if (IsDouble()) {
        return foundation::base::Result<double>(double_value_);
    }
    if (IsInt64()) {
        return foundation::base::Result<double>(
            static_cast<double>(int64_value_));
    }
    return foundation::base::Result<double>(
        foundation::base::ErrorCode::kInvalidState,
        "ConfigValue is not double");
}

foundation::base::Result<std::string> ConfigValue::AsString() const {
    if (!IsString()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not string");
    }
    return foundation::base::Result<std::string>(string_value_);
}

foundation::base::Result<ConfigValue::Array> ConfigValue::AsArray() const {
    if (!IsArray()) {
        return foundation::base::Result<Array>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not array");
    }
    return foundation::base::Result<Array>(array_value_);
}

foundation::base::Result<ConfigValue::Object> ConfigValue::AsObject() const {
    if (!IsObject()) {
        return foundation::base::Result<Object>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not object");
    }
    return foundation::base::Result<Object>(object_value_);
}

ConfigValue ConfigValue::MakeArray() {
    return ConfigValue(Array());
}

ConfigValue ConfigValue::MakeObject() {
    return ConfigValue(Object());
}

foundation::base::Result<void> ConfigValue::Append(const ConfigValue& value) {
    if (!IsArray()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not array");
    }
    array_value_.push_back(value);
    return foundation::base::Result<void>();
}

foundation::base::Result<std::size_t> ConfigValue::Size() const {
    if (IsArray()) {
        return foundation::base::Result<std::size_t>(array_value_.size());
    }
    if (IsObject()) {
        return foundation::base::Result<std::size_t>(object_value_.size());
    }
    if (IsString()) {
        return foundation::base::Result<std::size_t>(string_value_.size());
    }
    return foundation::base::Result<std::size_t>(
        foundation::base::ErrorCode::kInvalidState,
        "ConfigValue size is only valid for string/array/object");
}

bool ConfigValue::Empty() const {
    if (IsString()) {
        return string_value_.empty();
    }
    if (IsArray()) {
        return array_value_.empty();
    }
    if (IsObject()) {
        return object_value_.empty();
    }
    return true;
}

foundation::base::Result<ConfigValue> ConfigValue::ArrayGet(
    std::size_t index) const {
    if (!IsArray()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not array");
    }
    if (index >= array_value_.size()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kNotFound,
            "Array index out of range");
    }
    return foundation::base::Result<ConfigValue>(array_value_[index]);
}

const ConfigValue::Array* ConfigValue::GetArray() const {
    if (!IsArray()) {
        return NULL;
    }
    return &array_value_;
}

foundation::base::Result<void> ConfigValue::Set(
    const std::string& key,
    const ConfigValue& value) {
    if (!IsObject()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not object");
    }
    if (key.empty()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Object key is empty");
    }
    object_value_[key] = value;
    return foundation::base::Result<void>();
}

bool ConfigValue::Contains(const std::string& key) const {
    return IsObject() && object_value_.find(key) != object_value_.end();
}

foundation::base::Result<ConfigValue> ConfigValue::ObjectGet(
    const std::string& key) const {
    if (!IsObject()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidState,
            "ConfigValue is not object");
    }

    Object::const_iterator it = object_value_.find(key);
    if (it == object_value_.end()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kNotFound,
            "Object key not found");
    }

    return foundation::base::Result<ConfigValue>(it->second);
}

std::vector<std::string> ConfigValue::ObjectKeys() const {
    std::vector<std::string> keys;
    if (!IsObject()) {
        return keys;
    }

    for (Object::const_iterator it = object_value_.begin();
         it != object_value_.end();
         ++it) {
        keys.push_back(it->first);
    }
    return keys;
}

const ConfigValue::Object* ConfigValue::GetObject() const {
    if (!IsObject()) {
        return NULL;
    }
    return &object_value_;
}

foundation::base::Result<ConfigValue> ConfigValue::GetByPath(
    const std::string& dot_path) const {
    if (dot_path.empty()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Path is empty");
    }

    if (dot_path.front() == '.' || dot_path.back() == '.') {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Path must not start or end with '.'");
    }

    if (dot_path.find("..") != std::string::npos) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Path must not contain empty segments");
    }

    std::vector<std::string> parts =
        foundation::strings::Split(dot_path, '.');
    if (parts.empty()) {
        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Path is empty");
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kInvalidArgument,
                "Path must not contain empty segments");
        }
    }

    const ConfigValue* current = this;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!current->IsObject()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kInvalidState,
                "Path traverses through a non-object value");
        }

        const Object* object = current->GetObject();
        if (object == NULL) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kInvalidState,
                "Object storage is unavailable");
        }

        Object::const_iterator it = object->find(parts[i]);
        if (it == object->end()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kNotFound,
                "Path component not found");
        }

        current = &it->second;
    }

    return foundation::base::Result<ConfigValue>(*current);
}

}  // namespace config
}  // namespace foundation