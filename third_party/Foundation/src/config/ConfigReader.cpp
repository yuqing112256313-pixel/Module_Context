#include "foundation/config/ConfigReader.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "foundation/filesystem/FileUtils.h"
#include "foundation/strings/StringUtils.h"

namespace foundation {
namespace config {

namespace {

std::string EscapeJsonString(const std::string& input,
                             bool* ok) {
    std::ostringstream oss;
    *ok = true;

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        switch (c) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\b':
                oss << "\\b";
                break;
            case '\f':
                oss << "\\f";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    *ok = false;
                    return std::string();
                }
                oss << c;
                break;
        }
    }

    return oss.str();
}

foundation::base::Result<std::string> SerializeJson(
    const ConfigValue& value) {
    switch (value.GetType()) {
        case ConfigValue::Type::kNull:
            return foundation::base::Result<std::string>("null");

        case ConfigValue::Type::kBool:
            return foundation::base::Result<std::string>(
                value.AsBool().Value() ? "true" : "false");

        case ConfigValue::Type::kInt64: {
            std::ostringstream oss;
            oss << value.AsInt64().Value();
            return foundation::base::Result<std::string>(oss.str());
        }

        case ConfigValue::Type::kDouble: {
            double number = value.AsDouble().Value();
            if (!std::isfinite(number)) {
                return foundation::base::Result<std::string>(
                    foundation::base::ErrorCode::kInvalidArgument,
                    "Cannot serialize non-finite double");
            }
            std::ostringstream oss;
            oss.precision(17);
            oss << number;
            return foundation::base::Result<std::string>(oss.str());
        }

        case ConfigValue::Type::kString: {
            bool ok = false;
            std::string escaped =
                EscapeJsonString(value.AsString().Value(), &ok);
            if (!ok) {
                return foundation::base::Result<std::string>(
                    foundation::base::ErrorCode::kInvalidArgument,
                    "Unsupported control character in string");
            }
            return foundation::base::Result<std::string>(
                std::string("\"") + escaped + "\"");
        }

        case ConfigValue::Type::kArray: {
            const ConfigValue::Array* array = value.GetArray();
            if (array == NULL) {
                return foundation::base::Result<std::string>(
                    foundation::base::ErrorCode::kInvalidState,
                    "Array storage unavailable");
            }

            std::ostringstream oss;
            oss << "[";
            for (std::size_t i = 0; i < array->size(); ++i) {
                foundation::base::Result<std::string> item =
                    SerializeJson((*array)[i]);
                if (!item.IsOk()) {
                    return item;
                }
                if (i > 0) {
                    oss << ",";
                }
                oss << item.Value();
            }
            oss << "]";
            return foundation::base::Result<std::string>(oss.str());
        }

        case ConfigValue::Type::kObject: {
            const ConfigValue::Object* object = value.GetObject();
            if (object == NULL) {
                return foundation::base::Result<std::string>(
                    foundation::base::ErrorCode::kInvalidState,
                    "Object storage unavailable");
            }

            std::ostringstream oss;
            oss << "{";
            bool first = true;

            for (ConfigValue::Object::const_iterator it = object->begin();
                 it != object->end();
                 ++it) {
                bool ok = false;
                std::string escaped_key = EscapeJsonString(it->first, &ok);
                if (!ok) {
                    return foundation::base::Result<std::string>(
                        foundation::base::ErrorCode::kInvalidArgument,
                        "Unsupported control character in object key");
                }

                foundation::base::Result<std::string> item =
                    SerializeJson(it->second);
                if (!item.IsOk()) {
                    return item;
                }

                if (!first) {
                    oss << ",";
                }
                first = false;
                oss << "\"" << escaped_key << "\":" << item.Value();
            }

            oss << "}";
            return foundation::base::Result<std::string>(oss.str());
        }

        default:
            return foundation::base::Result<std::string>(
                foundation::base::ErrorCode::kInvalidState,
                "Unknown ConfigValue type");
    }
}

class JsonParser {
public:
    JsonParser(const std::string& text, std::size_t max_depth)
        : text_(text),
          position_(0),
          max_depth_(max_depth) {
    }

    foundation::base::Result<ConfigValue> Parse() {
        SkipSpaces();
        foundation::base::Result<ConfigValue> value = ParseValue(0);
        if (!value.IsOk()) {
            return value;
        }

        SkipSpaces();
        if (position_ != text_.size()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Unexpected trailing content in JSON");
        }

        return value;
    }

private:
    foundation::base::Result<ConfigValue> ParseValue(std::size_t depth) {
        if (depth > max_depth_) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kOverflow,
                "JSON depth exceeded");
        }

        SkipSpaces();
        if (position_ >= text_.size()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Unexpected end of JSON");
        }

        char c = text_[position_];
        if (c == '{') {
            return ParseObject(depth + 1);
        }
        if (c == '[') {
            return ParseArray(depth + 1);
        }
        if (c == '"') {
            foundation::base::Result<std::string> s = ParseString();
            if (!s.IsOk()) {
                return foundation::base::Result<ConfigValue>(
                    s.GetError(),
                    s.GetMessage());
            }
            return foundation::base::Result<ConfigValue>(
                ConfigValue(s.Value()));
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return ParseNumber();
        }
        if (ConsumeLiteral("true")) {
            return foundation::base::Result<ConfigValue>(ConfigValue(true));
        }
        if (ConsumeLiteral("false")) {
            return foundation::base::Result<ConfigValue>(ConfigValue(false));
        }
        if (ConsumeLiteral("null")) {
            return foundation::base::Result<ConfigValue>(ConfigValue());
        }

        return foundation::base::Result<ConfigValue>(
            foundation::base::ErrorCode::kParseError,
            "Unsupported JSON token");
    }

    foundation::base::Result<ConfigValue> ParseObject(std::size_t depth) {
        Consume('{');
        ConfigValue object = ConfigValue::MakeObject();

        SkipSpaces();
        if (TryConsume('}')) {
            return foundation::base::Result<ConfigValue>(object);
        }

        while (true) {
            foundation::base::Result<std::string> key = ParseString();
            if (!key.IsOk()) {
                return foundation::base::Result<ConfigValue>(
                    key.GetError(),
                    key.GetMessage());
            }

            SkipSpaces();
            if (!TryConsume(':')) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Expected ':' in object");
            }

            foundation::base::Result<ConfigValue> value = ParseValue(depth);
            if (!value.IsOk()) {
                return value;
            }

            foundation::base::Result<void> set_result =
                object.Set(key.Value(), value.Value());
            if (!set_result.IsOk()) {
                return foundation::base::Result<ConfigValue>(
                    set_result.GetError(),
                    set_result.GetMessage());
            }

            SkipSpaces();
            if (TryConsume('}')) {
                break;
            }
            if (!TryConsume(',')) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Expected ',' or '}' in object");
            }
            SkipSpaces();
        }

        return foundation::base::Result<ConfigValue>(object);
    }

    foundation::base::Result<ConfigValue> ParseArray(std::size_t depth) {
        Consume('[');
        ConfigValue array = ConfigValue::MakeArray();

        SkipSpaces();
        if (TryConsume(']')) {
            return foundation::base::Result<ConfigValue>(array);
        }

        while (true) {
            foundation::base::Result<ConfigValue> item = ParseValue(depth);
            if (!item.IsOk()) {
                return item;
            }

            foundation::base::Result<void> append_result =
                array.Append(item.Value());
            if (!append_result.IsOk()) {
                return foundation::base::Result<ConfigValue>(
                    append_result.GetError(),
                    append_result.GetMessage());
            }

            SkipSpaces();
            if (TryConsume(']')) {
                break;
            }
            if (!TryConsume(',')) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Expected ',' or ']' in array");
            }
            SkipSpaces();
        }

        return foundation::base::Result<ConfigValue>(array);
    }

    foundation::base::Result<ConfigValue> ParseNumber() {
        std::size_t begin = position_;

        if (text_[position_] == '-') {
            ++position_;
        }

        if (position_ >= text_.size()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Bad number token");
        }

        if (text_[position_] == '0') {
            ++position_;
        } else {
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        bool is_double = false;

        if (position_ < text_.size() && text_[position_] == '.') {
            is_double = true;
            ++position_;

            if (position_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Bad fractional number token");
            }

            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            is_double = true;
            ++position_;

            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }

            if (position_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Bad exponent number token");
            }

            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        std::string token = text_.substr(begin, position_ - begin);

        if (is_double) {
            char* end = NULL;
            errno = 0;
            double value = std::strtod(token.c_str(), &end);
            if (errno != 0 || end == NULL || *end != '\0') {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Invalid double token");
            }
            return foundation::base::Result<ConfigValue>(ConfigValue(value));
        }

        char* end = NULL;
        errno = 0;
        long long value = std::strtoll(token.c_str(), &end, 10);
        if (errno != 0 || end == NULL || *end != '\0') {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Invalid int64 token");
        }

        return foundation::base::Result<ConfigValue>(
            ConfigValue(static_cast<std::int64_t>(value)));
    }

    foundation::base::Result<std::string> ParseString() {
        if (!TryConsume('"')) {
            return foundation::base::Result<std::string>(
                foundation::base::ErrorCode::kParseError,
                "Expected string start");
        }

        std::ostringstream oss;

        while (position_ < text_.size()) {
            char c = text_[position_++];

            if (c == '"') {
                return foundation::base::Result<std::string>(oss.str());
            }

            if (c == '\\') {
                if (position_ >= text_.size()) {
                    return foundation::base::Result<std::string>(
                        foundation::base::ErrorCode::kParseError,
                        "Bad escape sequence");
                }

                char esc = text_[position_++];
                switch (esc) {
                    case '"':
                        oss << '"';
                        break;
                    case '\\':
                        oss << '\\';
                        break;
                    case '/':
                        oss << '/';
                        break;
                    case 'b':
                        oss << '\b';
                        break;
                    case 'f':
                        oss << '\f';
                        break;
                    case 'n':
                        oss << '\n';
                        break;
                    case 'r':
                        oss << '\r';
                        break;
                    case 't':
                        oss << '\t';
                        break;
                    default:
                        return foundation::base::Result<std::string>(
                            foundation::base::ErrorCode::kParseError,
                            "Unsupported JSON escape");
                }
            } else {
                oss << c;
            }
        }

        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Unterminated string");
    }

    bool ConsumeLiteral(const char* literal) {
        std::size_t len = std::strlen(literal);
        if (text_.compare(position_, len, literal) == 0) {
            position_ += len;
            return true;
        }
        return false;
    }

    void SkipSpaces() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    bool TryConsume(char c) {
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    void Consume(char c) {
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
        }
    }

private:
    const std::string& text_;
    std::size_t position_;
    std::size_t max_depth_;
};

foundation::base::Result<ConfigValue> ParseIni(
    const std::string& content) {
    ConfigValue root = ConfigValue::MakeObject();
    ConfigValue* current_section = &root;

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::string trimmed = foundation::strings::Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            std::string section_name = foundation::strings::Trim(
                trimmed.substr(1, trimmed.size() - 2));
            if (section_name.empty()) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kParseError,
                    "Empty INI section name");
            }

            foundation::base::Result<void> set_result =
                root.Set(section_name, ConfigValue::MakeObject());
            if (!set_result.IsOk()) {
                return foundation::base::Result<ConfigValue>(
                    set_result.GetError(),
                    set_result.GetMessage());
            }

            const ConfigValue::Object* object = root.GetObject();
            if (object == NULL) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kInvalidState,
                    "INI root object unavailable");
            }

            ConfigValue::Object::const_iterator it =
                object->find(section_name);
            if (it == object->end()) {
                return foundation::base::Result<ConfigValue>(
                    foundation::base::ErrorCode::kInvalidState,
                    "Failed to locate created INI section");
            }

            current_section =
                const_cast<ConfigValue*>(&it->second);
            continue;
        }

        std::size_t pos = trimmed.find('=');
        if (pos == std::string::npos) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Bad INI key-value line");
        }

        std::string key = foundation::strings::Trim(trimmed.substr(0, pos));
        std::string value = foundation::strings::Trim(trimmed.substr(pos + 1));
        if (key.empty()) {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kParseError,
                "Empty INI key");
        }

        foundation::base::Result<void> set_result =
            current_section->Set(key, ConfigValue(value));
        if (!set_result.IsOk()) {
            return foundation::base::Result<ConfigValue>(
                set_result.GetError(),
                set_result.GetMessage());
        }
    }

    return foundation::base::Result<ConfigValue>(root);
}

}  // namespace

ConfigReader::ConfigReader()
    : root_() {
}

foundation::base::ErrorCode ConfigReader::LoadFromJsonString(
    const std::string& json,
    std::size_t max_depth) {
    JsonParser parser(json, max_depth);
    foundation::base::Result<ConfigValue> result = parser.Parse();
    if (!result.IsOk()) {
        return result.GetError();
    }
    root_ = result.Value();
    return foundation::base::ErrorCode::kOk;
}

foundation::base::ErrorCode ConfigReader::LoadFromJsonFile(
    const std::string& path,
    std::size_t max_depth) {
    foundation::base::Result<std::string> content =
        foundation::filesystem::ReadAllText(path);
    if (!content.IsOk()) {
        return content.GetError();
    }
    return LoadFromJsonString(content.Value(), max_depth);
}

foundation::base::ErrorCode ConfigReader::LoadFromIniString(
    const std::string& ini) {
    foundation::base::Result<ConfigValue> result = ParseIni(ini);
    if (!result.IsOk()) {
        return result.GetError();
    }
    root_ = result.Value();
    return foundation::base::ErrorCode::kOk;
}

foundation::base::ErrorCode ConfigReader::LoadFromIniFile(
    const std::string& path) {
    foundation::base::Result<std::string> content =
        foundation::filesystem::ReadAllText(path);
    if (!content.IsOk()) {
        return content.GetError();
    }
    return LoadFromIniString(content.Value());
}

const ConfigValue& ConfigReader::Root() const {
    return root_;
}

bool ConfigReader::Has(const std::string& dot_path) const {
    return root_.GetByPath(dot_path).IsOk();
}

foundation::base::Result<ConfigValue> ConfigReader::Get(
    const std::string& dot_path) const {
    return root_.GetByPath(dot_path);
}

foundation::base::Result<std::string> ConfigReader::GetString(
    const std::string& dot_path) const {
    foundation::base::Result<ConfigValue> value = root_.GetByPath(dot_path);
    if (!value.IsOk()) {
        return foundation::base::Result<std::string>(
            value.GetError(),
            value.GetMessage());
    }
    return value.Value().AsString();
}

foundation::base::Result<std::int64_t> ConfigReader::GetInt64(
    const std::string& dot_path) const {
    foundation::base::Result<ConfigValue> value = root_.GetByPath(dot_path);
    if (!value.IsOk()) {
        return foundation::base::Result<std::int64_t>(
            value.GetError(),
            value.GetMessage());
    }
    return value.Value().AsInt64();
}

foundation::base::Result<double> ConfigReader::GetDouble(
    const std::string& dot_path) const {
    foundation::base::Result<ConfigValue> value = root_.GetByPath(dot_path);
    if (!value.IsOk()) {
        return foundation::base::Result<double>(
            value.GetError(),
            value.GetMessage());
    }
    return value.Value().AsDouble();
}

foundation::base::Result<bool> ConfigReader::GetBool(
    const std::string& dot_path) const {
    foundation::base::Result<ConfigValue> value = root_.GetByPath(dot_path);
    if (!value.IsOk()) {
        return foundation::base::Result<bool>(
            value.GetError(),
            value.GetMessage());
    }
    return value.Value().AsBool();
}

foundation::base::Result<std::string> ConfigReader::WriteJsonString(
    const ConfigValue& value) {
    return SerializeJson(value);
}

}  // namespace config
}  // namespace foundation