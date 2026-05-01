#ifndef FOUNDATION_FILESYSTEM_PATHUTILS_H_
#define FOUNDATION_FILESYSTEM_PATHUTILS_H_

#include <string>

#include "foundation/base/Export.h"

namespace foundation {
namespace filesystem {

// 平台原生路径分隔符。
// Windows: '\\'
// Linux  : '/'
FOUNDATION_API char Separator();

// 是否为路径分隔符。
// 同时识别 '/' 和 '\\'，便于跨平台处理输入路径。
FOUNDATION_API bool IsSeparator(char c);

// 判断路径是否为绝对路径。
// Windows 支持：
// - "C:\foo"
// - "\\server\share\dir"
// Linux 支持：
// - "/usr/local"
FOUNDATION_API bool IsAbsolute(const std::string& path);

// 路径拼接。
// 规则：
// - a 为空 -> 返回 Normalize(b)
// - b 为空 -> 返回 Normalize(a)
// - 若 b 为绝对路径 -> 返回 Normalize(b)
// - 否则按平台分隔符拼接再 Normalize
FOUNDATION_API std::string Join(const std::string& a, const std::string& b);

// 路径规范化。
// 处理：
// - 统一分隔符为平台分隔符
// - 折叠重复分隔符
// - 解析 "." 与 ".."
// - 保留 Windows 盘符 / UNC 根前缀
// 返回的是“词法规范化”结果，不访问文件系统。
FOUNDATION_API std::string Normalize(const std::string& path);

// 获取扩展名。
// 例：
// - "a.txt"      -> ".txt"
// - "a.tar.gz"   -> ".gz"
// - ".gitignore" -> ""
FOUNDATION_API std::string GetExtension(const std::string& path);

// 获取文件名。
// 例：
// - "/a/b/c.txt" -> "c.txt"
FOUNDATION_API std::string GetFileName(const std::string& path);

// 获取 stem。
// 例：
// - "/a/b/c.txt" -> "c"
// - "a.tar.gz"   -> "a.tar"
// - ".profile"   -> ".profile"
FOUNDATION_API std::string GetStem(const std::string& path);

// 获取父路径。
// 例：
// - "/a/b/c.txt" -> "/a/b"
// - "a/b/c.txt"  -> "a/b"
// - "file.txt"   -> "."
// - "/"          -> "/"
// - "C:\\a\\b"   -> "C:\\a"
// - "C:\\"       -> "C:\\"
FOUNDATION_API std::string GetParentPath(const std::string& path);

// 获取绝对路径。
// - 对已存在路径：尽量返回规范化后的真实绝对路径
// - 对不存在路径：基于当前工作目录进行绝对化后再 Normalize
// 该函数不保证符号链接解析结果与 realpath 完全一致，但保证返回绝对路径字符串。
FOUNDATION_API std::string GetAbsolutePath(const std::string& path);

}  // namespace filesystem
}  // namespace foundation

#endif  // FOUNDATION_FILESYSTEM_PATHUTILS_H_