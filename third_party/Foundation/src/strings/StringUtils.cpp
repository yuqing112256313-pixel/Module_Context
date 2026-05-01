#include "foundation/strings/StringUtils.h"

#include <algorithm>

namespace foundation {
namespace strings {
namespace {

inline bool IsAsciiWhitespace(unsigned char ch) {
    return ch == ' '  ||
           ch == '\t' ||
           ch == '\n' ||
           ch == '\r' ||
           ch == '\f' ||
           ch == '\v';
}


std::size_t FindTrimLeftBound(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size() &&
           IsAsciiWhitespace(static_cast<unsigned char>(value[index]))) {
        ++index;
    }
    return index;
}

// 返回“去掉右侧空白后”的尾后位置（exclusive end）。
std::size_t FindTrimRightBound(const std::string& value) {
    std::size_t index = value.size();
    while (index > 0 &&
           IsAsciiWhitespace(static_cast<unsigned char>(value[index - 1]))) {
        --index;
    }
    return index;
}

} // namespace

bool HasSplitOption(SplitOptions options, SplitOptions option) {
    return (options & option) != SplitOptions::kNone;
}

std::string TrimLeft(const std::string& value) {
    const std::size_t begin = FindTrimLeftBound(value);
    return value.substr(begin);
}

std::string TrimRight(const std::string& value) {
    const std::size_t end = FindTrimRightBound(value);
    return value.substr(0, end);
}

std::string Trim(const std::string& value) {
    const std::size_t begin = FindTrimLeftBound(value);
    const std::size_t end = FindTrimRightBound(value);
    if (begin >= end) {
        return std::string();
    }
    return value.substr(begin, end - begin);
}

void TrimLeftInPlace(std::string* value) {
    if (value == NULL) {
        return;
    }
    value->erase(0, FindTrimLeftBound(*value));
}

void TrimRightInPlace(std::string* value) {
    if (value == NULL) {
        return;
    }
    value->erase(FindTrimRightBound(*value));
}

void TrimInPlace(std::string* value) {
    if (value == NULL) {
        return;
    }

    const std::size_t begin = FindTrimLeftBound(*value);
    const std::size_t end = FindTrimRightBound(*value);

    if (begin >= end) {
        value->clear();
        return;
    }

    value->erase(end);
    value->erase(0, begin);
}

std::string ToLowerAscii(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) -> char {
                       if (ch >= 'A' && ch <= 'Z') {
                           return static_cast<char>(ch - 'A' + 'a');
                       }
                       return static_cast<char>(ch);
                   });
    return result;
}

std::string ToUpperAscii(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) -> char {
                       if (ch >= 'a' && ch <= 'z') {
                           return static_cast<char>(ch - 'a' + 'A');
                       }
                       return static_cast<char>(ch);
                   });
    return result;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(),
                         suffix.size(),
                         suffix) == 0;
}

bool Contains(const std::string& value, const std::string& sub) {
    return value.find(sub) != std::string::npos;
}

std::vector<std::string> Split(
    const std::string& value,
    char delimiter,
    SplitOptions options) {
    std::vector<std::string> result;

    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = value.find(delimiter, start);
        const std::string token = (pos == std::string::npos)
            ? value.substr(start)
            : value.substr(start, pos - start);

        if (HasSplitOption(options, SplitOptions::kKeepEmpty) || !token.empty()) {
            result.push_back(token);
        }

        if (pos == std::string::npos) {
            break;
        }

        start = pos + 1;
    }

    return result;
}

std::vector<std::string> Split(
    const std::string& value,
    const std::string& delimiter,
    SplitOptions options) {
    std::vector<std::string> result;

    if (delimiter.empty()) {
        result.push_back(value);
        return result;
    }

    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = value.find(delimiter, start);
        const std::string token = (pos == std::string::npos)
            ? value.substr(start)
            : value.substr(start, pos - start);

        if (HasSplitOption(options, SplitOptions::kKeepEmpty) || !token.empty()) {
            result.push_back(token);
        }

        if (pos == std::string::npos) {
            break;
        }

        start = pos + delimiter.size();
    }

    return result;
}

std::string Join(
    const std::vector<std::string>& values,
    const std::string& delimiter) {
    if (values.empty()) {
        return std::string();
    }

    std::size_t total_size = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        total_size += values[i].size();
    }
    if (values.size() > 1) {
        total_size += (values.size() - 1) * delimiter.size();
    }

    std::string result;
    result.reserve(total_size);

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            result.append(delimiter);
        }
        result.append(values[i]);
    }

    return result;
}

std::string ReplaceFirst(
    const std::string& value,
    const std::string& from,
    const std::string& to) {
    if (from.empty()) {
        return value;
    }

    std::string result = value;
    const std::size_t pos = result.find(from);
    if (pos != std::string::npos) {
        result.replace(pos, from.size(), to);
    }
    return result;
}

std::string ReplaceAll(
    const std::string& value,
    const std::string& from,
    const std::string& to) {
    std::string result = value;
    ReplaceAllInPlace(&result, from, to);
    return result;
}

std::size_t ReplaceAllInPlace(
    std::string* value,
    const std::string& from,
    const std::string& to) {
    if (value == NULL || from.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = value->find(from, pos)) != std::string::npos) {
        value->replace(pos, from.size(), to);
        pos += to.size();
        ++count;
    }

    return count;
}

} // namespace strings
} // namespace foundation