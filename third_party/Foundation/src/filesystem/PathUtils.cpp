#include "foundation/filesystem/PathUtils.h"

#include "foundation/base/Platform.h"

#include <cctype>
#include <vector>

#if FOUNDATION_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif

namespace foundation {
namespace filesystem {

namespace {

inline bool IsAlphaAscii(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string ReplaceSeparators(const std::string& path) {
    std::string out = path;
    const char sep = Separator();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (IsSeparator(out[i])) {
            out[i] = sep;
        }
    }
    return out;
}

// 根前缀长度。
// Windows:
//   "C:\a\b"         -> 3
//   "C:foo"          -> 2
//   "\\server\share" -> share 结束位置
//   "\foo"           -> 1
// Linux:
//   "/a/b"           -> 1
//   "abc"            -> 0
std::size_t GetRootLength(const std::string& path) {
    if (path.empty()) {
        return 0;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    if (path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1])) {
        std::size_t pos = 2;

        while (pos < path.size() && !IsSeparator(path[pos])) {
            ++pos;
        }
        if (pos >= path.size()) {
            return path.size();
        }

        while (pos < path.size() && IsSeparator(path[pos])) {
            ++pos;
        }

        while (pos < path.size() && !IsSeparator(path[pos])) {
            ++pos;
        }
        return pos;
    }

    if (path.size() >= 2 && IsAlphaAscii(path[0]) && path[1] == ':') {
        if (path.size() >= 3 && IsSeparator(path[2])) {
            return 3;
        }
        return 2;
    }

    if (IsSeparator(path[0])) {
        return 1;
    }

    return 0;
#else
    return IsSeparator(path[0]) ? 1U : 0U;
#endif
}

bool HasDriveLetter(const std::string& path) {
#if FOUNDATION_PLATFORM_WINDOWS
    return path.size() >= 2 && IsAlphaAscii(path[0]) && path[1] == ':';
#else
    (void)path;
    return false;
#endif
}

bool IsUncPath(const std::string& path) {
#if FOUNDATION_PLATFORM_WINDOWS
    return path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]);
#else
    (void)path;
    return false;
#endif
}

std::string TrimTrailingSeparatorsPreserveRoot(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    const std::size_t root_len = GetRootLength(path);
    std::size_t end = path.size();
    while (end > root_len && IsSeparator(path[end - 1])) {
        --end;
    }
    return path.substr(0, end);
}

std::vector<std::string> SplitComponents(const std::string& tail) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < tail.size()) {
        while (i < tail.size() && IsSeparator(tail[i])) {
            ++i;
        }
        if (i >= tail.size()) {
            break;
        }

        std::size_t j = i;
        while (j < tail.size() && !IsSeparator(tail[j])) {
            ++j;
        }
        parts.push_back(tail.substr(i, j - i));
        i = j;
    }
    return parts;
}

std::string JoinComponents(const std::vector<std::string>& parts, char sep) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result.push_back(sep);
        }
        result += parts[i];
    }
    return result;
}

}  // namespace

char Separator() {
#if FOUNDATION_PLATFORM_WINDOWS
    return '\\';
#else
    return '/';
#endif
}

bool IsSeparator(char c) {
    return c == '/' || c == '\\';
}

bool IsAbsolute(const std::string& path) {
    if (path.empty()) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    if (IsUncPath(path)) {
        return true;
    }
    return path.size() >= 3 &&
           IsAlphaAscii(path[0]) &&
           path[1] == ':' &&
           IsSeparator(path[2]);
#else
    return IsSeparator(path[0]);
#endif
}

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return Normalize(b);
    }
    if (b.empty()) {
        return Normalize(a);
    }
    if (IsAbsolute(b)) {
        return Normalize(b);
    }

#if FOUNDATION_PLATFORM_WINDOWS
    // "C:foo" 这种 drive-relative path 不视为绝对路径，但不应该直接拼到另一个路径后面。
    if (HasDriveLetter(b)) {
        return Normalize(b);
    }
#endif

    std::string lhs = a;
    if (!IsSeparator(lhs[lhs.size() - 1])) {
        lhs.push_back(Separator());
    }
    lhs += b;
    return Normalize(lhs);
}

std::string Normalize(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    const char sep = Separator();
    const std::string replaced = ReplaceSeparators(path);
    const std::size_t root_len = GetRootLength(replaced);
    const std::string root = replaced.substr(0, root_len);
    const std::string tail = replaced.substr(root_len);

    const bool absolute = IsAbsolute(replaced) ||
#if FOUNDATION_PLATFORM_WINDOWS
                          IsUncPath(replaced) ||
#else
                          false ||
#endif
                          (!root.empty() && root == std::string(1, sep));

#if FOUNDATION_PLATFORM_WINDOWS
    const bool drive_relative =
        root_len == 2 &&
        replaced.size() >= 2 &&
        ((replaced[0] >= 'A' && replaced[0] <= 'Z') ||
         (replaced[0] >= 'a' && replaced[0] <= 'z')) &&
        replaced[1] == ':';
#else
    const bool drive_relative = false;
#endif

    const std::vector<std::string> raw_parts = SplitComponents(tail);
    std::vector<std::string> parts;

    for (std::size_t i = 0; i < raw_parts.size(); ++i) {
        const std::string& part = raw_parts[i];

        if (part.empty() || part == ".") {
            continue;
        }

        if (part == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else {
#if FOUNDATION_PLATFORM_WINDOWS
                // 对 "C:\.."，不能越过根；
                // 对 "C:foo\.."，属于 drive-relative，保留相对语义。
                if (!absolute && !IsUncPath(replaced)) {
                    parts.push_back(part);
                }
#else
                if (!absolute) {
                    parts.push_back(part);
                }
#endif
            }
            continue;
        }

        parts.push_back(part);
    }

    std::string result = root;
    const std::string joined = JoinComponents(parts, sep);

    if (!joined.empty()) {
#if FOUNDATION_PLATFORM_WINDOWS
        // 关键修复：
        // "C:foo" 是 drive-relative，不能被拼成 "C:\foo"。
        if (drive_relative && result == root) {
            result += joined;
        } else {
            if (!result.empty() && !IsSeparator(result[result.size() - 1])) {
                result.push_back(sep);
            }
            result += joined;
        }
#else
        if (!result.empty() && !IsSeparator(result[result.size() - 1])) {
            result.push_back(sep);
        }
        result += joined;
#endif
    }

    if (result.empty()) {
        return ".";
    }

    result = TrimTrailingSeparatorsPreserveRoot(result);

#if FOUNDATION_PLATFORM_WINDOWS
    if (result.empty()) {
        return ".";
    }
#endif

    return result;
}

std::string GetFileName(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    const std::string normalized = TrimTrailingSeparatorsPreserveRoot(
        ReplaceSeparators(path));
    const std::size_t root_len = GetRootLength(normalized);

    if (normalized.size() == root_len) {
        return normalized.substr(0, root_len);
    }

    const std::size_t pos = normalized.find_last_of(Separator());
    if (pos == std::string::npos) {
        return normalized;
    }
    return normalized.substr(pos + 1);
}

std::string GetExtension(const std::string& path) {
    const std::string file_name = GetFileName(path);
    const std::size_t dot = file_name.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return std::string();
    }
    return file_name.substr(dot);
}

std::string GetStem(const std::string& path) {
    const std::string file_name = GetFileName(path);
    const std::size_t dot = file_name.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return file_name;
    }
    return file_name.substr(0, dot);
}

std::string GetParentPath(const std::string& path) {
    if (path.empty()) {
        return ".";
    }

    std::string normalized = Normalize(path);
    if (normalized.empty()) {
        return ".";
    }

    const char sep = Separator();
    const std::size_t root_len = GetRootLength(normalized);

#if FOUNDATION_PLATFORM_WINDOWS
    const bool drive_relative =
        root_len == 2 &&
        normalized.size() >= 2 &&
        ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
         (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
        normalized[1] == ':';
#else
    const bool drive_relative = false;
#endif

    const std::size_t pos = normalized.find_last_of(sep);

    if (pos == std::string::npos) {
#if FOUNDATION_PLATFORM_WINDOWS
        // "C:foo" -> "C:"
        if (drive_relative && normalized.size() > root_len) {
            return normalized.substr(0, root_len);
        }
#endif
        return ".";
    }

    if (pos < root_len) {
        return normalized.substr(0, root_len);
    }

    if (pos == 0) {
        return normalized.substr(0, 1);
    }

    return normalized.substr(0, pos);
}

std::string GetAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    DWORD required = GetFullPathNameA(path.c_str(), 0, NULL, NULL);
    if (required == 0) {
        return Normalize(path);
    }

    std::vector<char> buffer(static_cast<std::size_t>(required) + 1, '\0');
    DWORD length = GetFullPathNameA(path.c_str(),
                                    static_cast<DWORD>(buffer.size()),
                                    &buffer[0],
                                    NULL);
    if (length == 0 || length >= buffer.size()) {
        return Normalize(path);
    }

    return Normalize(std::string(&buffer[0], length));
#else
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != NULL) {
        return Normalize(std::string(resolved));
    }

    if (IsAbsolute(path)) {
        return Normalize(path);
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return Normalize(path);
    }

    return Normalize(Join(std::string(cwd), path));
#endif
}

}  // namespace filesystem
}  // namespace foundation