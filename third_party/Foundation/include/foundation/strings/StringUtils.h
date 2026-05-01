#ifndef FOUNDATION_STRINGS_STRINGUTILS_H_
#define FOUNDATION_STRINGS_STRINGUTILS_H_

#include <cstddef>
#include <string>
#include <vector>

#include "foundation/base/Export.h"

namespace foundation {
namespace strings {

// Split 选项。
// kNone:
//   - 丢弃空分段。
//   - 例如 Split("a,,b,", ',', kNone) => ["a", "b"]
//
// kKeepEmpty:
//   - 保留空分段。
//   - 例如 Split("a,,b,", ',', kKeepEmpty) => ["a", "", "b", ""]
enum class SplitOptions {
    kNone = 0,
    kKeepEmpty = 1 << 0
};

inline SplitOptions operator|(SplitOptions lhs, SplitOptions rhs) {
    return static_cast<SplitOptions>(
        static_cast<int>(lhs) | static_cast<int>(rhs));
}

inline SplitOptions operator&(SplitOptions lhs, SplitOptions rhs) {
    return static_cast<SplitOptions>(
        static_cast<int>(lhs) & static_cast<int>(rhs));
}

FOUNDATION_API bool HasSplitOption(SplitOptions options, SplitOptions option);

// 移除 ASCII 空白字符：' ', '\t', '\n', '\r', '\f', '\v'
FOUNDATION_API std::string TrimLeft(const std::string& value);
FOUNDATION_API std::string TrimRight(const std::string& value);
FOUNDATION_API std::string Trim(const std::string& value);

FOUNDATION_API void TrimLeftInPlace(std::string* value);
FOUNDATION_API void TrimRightInPlace(std::string* value);
FOUNDATION_API void TrimInPlace(std::string* value);

// 仅处理 ASCII 字母。
// 不做 locale 相关大小写转换，不保证 UTF-8/Unicode 语义。
FOUNDATION_API std::string ToLowerAscii(const std::string& value);
FOUNDATION_API std::string ToUpperAscii(const std::string& value);

FOUNDATION_API bool StartsWith(const std::string& value, const std::string& prefix);
FOUNDATION_API bool EndsWith(const std::string& value, const std::string& suffix);
FOUNDATION_API bool Contains(const std::string& value, const std::string& sub);

// Split 约定：
// 1. delimiter 为 char：按单字符分割。
// 2. delimiter 为 string：按完整字符串分割。
// 3. delimiter 为空字符串时，返回 {value}。
// 4. 当未设置 kKeepEmpty 时，空分段会被丢弃。
// 5. 当 value 为空且未设置 kKeepEmpty 时，返回空 vector；
//    当 value 为空且设置 kKeepEmpty 时，返回包含一个空串的 vector。
FOUNDATION_API std::vector<std::string> Split(
    const std::string& value,
    char delimiter,
    SplitOptions options = SplitOptions::kNone);

FOUNDATION_API std::vector<std::string> Split(
    const std::string& value,
    const std::string& delimiter,
    SplitOptions options = SplitOptions::kNone);

FOUNDATION_API std::string Join(
    const std::vector<std::string>& values,
    const std::string& delimiter);

// ReplaceFirst:
//   - 仅替换第一个匹配项。
//   - from 为空字符串时，直接返回原字符串。
FOUNDATION_API std::string ReplaceFirst(
    const std::string& value,
    const std::string& from,
    const std::string& to);

// ReplaceAll:
//   - 替换所有非重叠匹配项。
//   - from 为空字符串时，直接返回原字符串。
FOUNDATION_API std::string ReplaceAll(
    const std::string& value,
    const std::string& from,
    const std::string& to);

// 原地替换所有非重叠匹配项，返回替换次数。
// value == NULL 或 from.empty() 时返回 0。
FOUNDATION_API std::size_t ReplaceAllInPlace(
    std::string* value,
    const std::string& from,
    const std::string& to);

} // namespace strings
} // namespace foundation

#endif // FOUNDATION_STRINGS_STRINGUTILS_H_