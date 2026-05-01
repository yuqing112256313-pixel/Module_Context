#include "foundation/filesystem/FileUtils.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdint>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Platform.h"

#if FOUNDATION_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace foundation {
namespace filesystem {

namespace {

static const std::size_t kCopyBufferSize = 1024 * 1024;  // 1MB

inline bool IsSeparator(char c) {
    return c == '/' || c == '\\';
}

inline char NativeSeparator() {
#if FOUNDATION_PLATFORM_WINDOWS
    return '\\';
#else
    return '/';
#endif
}

std::string ReplaceSeparators(const std::string& path) {
    std::string out = path;
    const char sep = NativeSeparator();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (IsSeparator(out[i])) {
            out[i] = sep;
        }
    }
    return out;
}

// 返回路径根前缀长度。
// 示例：
// Windows:
//   "C:\\a\\b"         -> 3
//   "C:"               -> 2
//   "\\\\server\\share" -> share 根长度
// Linux:
//   "/a/b"             -> 1
//   "abc"              -> 0
std::size_t GetRootLength(const std::string& path) {
    if (path.empty()) {
        return 0;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    // UNC: \\server\share\dir
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

    // Drive path: C: or C:\...
    if (path.size() >= 2 &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
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
    return IsSeparator(path[0]) ? 1 : 0;
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

std::string ParentPath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    std::string normalized = ReplaceSeparators(path);
    normalized = TrimTrailingSeparatorsPreserveRoot(normalized);

    if (normalized.empty()) {
        return std::string();
    }

    const std::size_t root_len = GetRootLength(normalized);
    const std::size_t pos = normalized.find_last_of(NativeSeparator());

    if (pos == std::string::npos) {
        return std::string();
    }

    if (pos < root_len) {
        return normalized.substr(0, root_len);
    }

    if (pos == 0) {
        return normalized.substr(0, 1);
    }

    return normalized.substr(0, pos);
}

base::Result<void> MakeStatus(base::ErrorCode code, const std::string& message) {
    return base::MakeError(code, message);
}

template <typename T>
base::Result<T> MakeFailure(base::ErrorCode code, const std::string& message) {
    return base::MakeErrorResult<T>(code, message);
}

#if FOUNDATION_PLATFORM_WINDOWS

bool GetPathAttributes(const std::string& path, DWORD* attributes) {
    if (path.empty() || attributes == NULL) {
        return false;
    }

    const DWORD attr = ::GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    *attributes = attr;
    return true;
}

base::Result<void> CreateDirectoryOneLevel(const std::string& path) {
    if (::CreateDirectoryA(path.c_str(), NULL) != 0) {
        return base::MakeSuccess();
    }

    const DWORD err = ::GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        if (IsDirectory(path)) {
            return base::MakeSuccess();
        }
        return MakeStatus(
            base::ErrorCode::kAlreadyExists,
            "Path already exists but is not a directory: " + path);
    }

    return MakeStatus(
        base::ErrorCode::kIoError,
        "Failed to create directory: " + path);
}

#else

base::Result<void> CreateDirectoryOneLevel(const std::string& path) {
    if (::mkdir(path.c_str(), 0755) == 0) {
        return base::MakeSuccess();
    }

    if (errno == EEXIST) {
        if (IsDirectory(path)) {
            return base::MakeSuccess();
        }
        return MakeStatus(
            base::ErrorCode::kAlreadyExists,
            "Path already exists but is not a directory: " + path);
    }

    return MakeStatus(
        base::ErrorCode::kIoError,
        "Failed to create directory: " + path);
}

#endif

base::Result<void> EnsureParentDirectoryForFile(const std::string& path) {
    const std::string parent = ParentPath(path);
    if (parent.empty()) {
        return base::MakeSuccess();
    }
    return CreateDirectories(parent);
}

bool LexicallyEqualPath(const std::string& lhs, const std::string& rhs) {
    std::string a = TrimTrailingSeparatorsPreserveRoot(ReplaceSeparators(lhs));
    std::string b = TrimTrailingSeparatorsPreserveRoot(ReplaceSeparators(rhs));

#if FOUNDATION_PLATFORM_WINDOWS
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
#else
    return a == b;
#endif
}

}  // namespace

bool Exists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    DWORD attr = 0;
    return GetPathAttributes(path, &attr);
#else
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
#endif
}

bool IsDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    DWORD attr = 0;
    if (!GetPathAttributes(path, &attr)) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

bool IsRegularFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    DWORD attr = 0;
    if (!GetPathAttributes(path, &attr)) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

base::Result<void> CreateDirectories(const std::string& path) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    std::string normalized = ReplaceSeparators(path);
    normalized = TrimTrailingSeparatorsPreserveRoot(normalized);

    if (normalized.empty() || normalized == ".") {
        return base::MakeSuccess();
    }

    if (Exists(normalized)) {
        if (IsDirectory(normalized)) {
            return base::MakeSuccess();
        }
        return MakeStatus(
            base::ErrorCode::kAlreadyExists,
            "Path already exists but is not a directory: " + normalized);
    }

    const std::size_t root_len = GetRootLength(normalized);
    std::string current = normalized.substr(0, root_len);
    std::size_t pos = root_len;

    while (pos < normalized.size()) {
        while (pos < normalized.size() && IsSeparator(normalized[pos])) {
            ++pos;
        }
        if (pos >= normalized.size()) {
            break;
        }

        std::size_t next = pos;
        while (next < normalized.size() && !IsSeparator(normalized[next])) {
            ++next;
        }

        if (!current.empty() && !IsSeparator(current[current.size() - 1])) {
            current.push_back(NativeSeparator());
        }
        current.append(normalized, pos, next - pos);

        if (Exists(current)) {
            if (!IsDirectory(current)) {
                return MakeStatus(
                    base::ErrorCode::kAlreadyExists,
                    "Path component exists but is not a directory: " + current);
            }
        } else {
            base::Result<void> status = CreateDirectoryOneLevel(current);
            if (!status.IsOk()) {
                return status;
            }
        }

        pos = next;
    }

    return base::MakeSuccess();
}

base::Result<std::string> ReadAllText(const std::string& path) {
    if (path.empty()) {
        return MakeFailure<std::string>(
            base::ErrorCode::kInvalidArgument,
            "Path is empty");
    }

    std::ifstream ifs(path.c_str(), std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return MakeFailure<std::string>(
            base::ErrorCode::kOpenFailed,
            "Failed to open file for reading: " + path);
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();

    if (ifs.bad()) {
        return MakeFailure<std::string>(
            base::ErrorCode::kIoError,
            "Failed to read file: " + path);
    }

    return base::MakeResult(oss.str());
}

base::Result<std::vector<std::uint8_t> > ReadAllBytes(const std::string& path) {
    if (path.empty()) {
        return MakeFailure<std::vector<std::uint8_t> >(
            base::ErrorCode::kInvalidArgument,
            "Path is empty");
    }

    std::ifstream ifs(path.c_str(), std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return MakeFailure<std::vector<std::uint8_t> >(
            base::ErrorCode::kOpenFailed,
            "Failed to open file for reading: " + path);
    }

    std::vector<std::uint8_t> data;
    const base::Result<std::uint64_t> size_result = GetFileSize(path);
    if (size_result.IsOk()) {
        const std::uint64_t size = size_result.Value();
        if (size > 0) {
            data.reserve(static_cast<std::size_t>(size));
        }
    }

    char buffer[64 * 1024];
    while (ifs.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) ||
           ifs.gcount() > 0) {
        const std::streamsize count = ifs.gcount();
        data.insert(
            data.end(),
            reinterpret_cast<const std::uint8_t*>(buffer),
            reinterpret_cast<const std::uint8_t*>(buffer + count));
    }

    if (ifs.bad()) {
        return MakeFailure<std::vector<std::uint8_t> >(
            base::ErrorCode::kIoError,
            "Failed to read file: " + path);
    }

    return base::MakeResult(data);
}

base::Result<void> WriteAllText(const std::string& path,
                                const std::string& content) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    base::Result<void> dir_status = EnsureParentDirectoryForFile(path);
    if (!dir_status.IsOk()) {
        return dir_status;
    }

    std::ofstream ofs(
        path.c_str(),
        std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return MakeStatus(
            base::ErrorCode::kOpenFailed,
            "Failed to open file for writing: " + path);
    }

    if (!content.empty()) {
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    if (!ofs.good()) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to write file: " + path);
    }

    return base::MakeSuccess();
}

base::Result<void> WriteAllBytes(
    const std::string& path,
    const std::vector<std::uint8_t>& content) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    base::Result<void> dir_status = EnsureParentDirectoryForFile(path);
    if (!dir_status.IsOk()) {
        return dir_status;
    }

    std::ofstream ofs(
        path.c_str(),
        std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return MakeStatus(
            base::ErrorCode::kOpenFailed,
            "Failed to open file for writing: " + path);
    }

    if (!content.empty()) {
        ofs.write(reinterpret_cast<const char*>(&content[0]),
                  static_cast<std::streamsize>(content.size()));
    }

    if (!ofs.good()) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to write file: " + path);
    }

    return base::MakeSuccess();
}

base::Result<void> AppendAllText(const std::string& path,
                                 const std::string& content) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    base::Result<void> dir_status = EnsureParentDirectoryForFile(path);
    if (!dir_status.IsOk()) {
        return dir_status;
    }

    std::ofstream ofs(
        path.c_str(),
        std::ios::out | std::ios::binary | std::ios::app);
    if (!ofs.is_open()) {
        return MakeStatus(
            base::ErrorCode::kOpenFailed,
            "Failed to open file for appending: " + path);
    }

    if (!content.empty()) {
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    if (!ofs.good()) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to append file: " + path);
    }

    return base::MakeSuccess();
}

base::Result<std::uint64_t> GetFileSize(const std::string& path) {
    if (path.empty()) {
        return MakeFailure<std::uint64_t>(
            base::ErrorCode::kInvalidArgument,
            "Path is empty");
    }

#if FOUNDATION_PLATFORM_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad) == 0) {
        return MakeFailure<std::uint64_t>(
            base::ErrorCode::kNotFound,
            "File not found: " + path);
    }

    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return MakeFailure<std::uint64_t>(
            base::ErrorCode::kInvalidArgument,
            "Path is a directory, not a file: " + path);
    }

    ULARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    return base::MakeResult(static_cast<std::uint64_t>(size.QuadPart));
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return MakeFailure<std::uint64_t>(
            base::ErrorCode::kNotFound,
            "File not found: " + path);
    }

    if (!S_ISREG(st.st_mode)) {
        return MakeFailure<std::uint64_t>(
            base::ErrorCode::kInvalidArgument,
            "Path is not a regular file: " + path);
    }

    return base::MakeResult(static_cast<std::uint64_t>(st.st_size));
#endif
}

base::Result<void> RemoveFile(const std::string& path) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    if (!Exists(path)) {
        return MakeStatus(
            base::ErrorCode::kNotFound,
            "File not found: " + path);
    }

    if (IsDirectory(path)) {
        return MakeStatus(
            base::ErrorCode::kInvalidArgument,
            "Path is a directory, not a file: " + path);
    }

    if (std::remove(path.c_str()) != 0) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to remove file: " + path);
    }

    return base::MakeSuccess();
}

base::Result<void> RemoveDirectoryIfEmpty(const std::string& path) {
    if (path.empty()) {
        return MakeStatus(base::ErrorCode::kInvalidArgument, "Path is empty");
    }

    if (!Exists(path)) {
        return MakeStatus(
            base::ErrorCode::kNotFound,
            "Directory not found: " + path);
    }

    if (!IsDirectory(path)) {
        return MakeStatus(
            base::ErrorCode::kInvalidArgument,
            "Path is not a directory: " + path);
    }

#if FOUNDATION_PLATFORM_WINDOWS
    if (::RemoveDirectoryA(path.c_str()) == 0) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to remove directory (directory may be non-empty): " + path);
    }
#else
    if (::rmdir(path.c_str()) != 0) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to remove directory (directory may be non-empty): " + path);
    }
#endif

    return base::MakeSuccess();
}

base::Result<void> CopyFileTo(const std::string& from,
                            const std::string& to,
                            bool overwrite) {
    if (from.empty() || to.empty()) {
        return MakeStatus(
            base::ErrorCode::kInvalidArgument,
            "Source path or destination path is empty");
    }

    if (LexicallyEqualPath(from, to)) {
        return MakeStatus(
            base::ErrorCode::kInvalidArgument,
            "Source path and destination path are the same");
    }

    if (!Exists(from)) {
        return MakeStatus(
            base::ErrorCode::kNotFound,
            "Source file not found: " + from);
    }

    if (!IsRegularFile(from)) {
        return MakeStatus(
            base::ErrorCode::kInvalidArgument,
            "Source path is not a regular file: " + from);
    }

    if (Exists(to)) {
        if (!overwrite) {
            return MakeStatus(
                base::ErrorCode::kAlreadyExists,
                "Destination file already exists: " + to);
        }
        if (IsDirectory(to)) {
            return MakeStatus(
                base::ErrorCode::kInvalidArgument,
                "Destination path is a directory: " + to);
        }
    }

    base::Result<void> dir_status = EnsureParentDirectoryForFile(to);
    if (!dir_status.IsOk()) {
        return dir_status;
    }

    std::ifstream ifs(from.c_str(), std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return MakeStatus(
            base::ErrorCode::kOpenFailed,
            "Failed to open source file: " + from);
    }

    std::ofstream ofs(
        to.c_str(),
        std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return MakeStatus(
            base::ErrorCode::kOpenFailed,
            "Failed to open destination file: " + to);
    }

    std::vector<char> buffer(kCopyBufferSize);
    while (ifs.read(&buffer[0], static_cast<std::streamsize>(buffer.size())) ||
        ifs.gcount() > 0) {
        const std::streamsize count = ifs.gcount();
        ofs.write(&buffer[0], count);
        if (!ofs.good()) {
            return MakeStatus(
                base::ErrorCode::kIoError,
                "Failed to write destination file: " + to);
        }
    }

    if (ifs.bad()) {
        return MakeStatus(
            base::ErrorCode::kIoError,
            "Failed to read source file: " + from);
    }

    return base::MakeSuccess();
}

}  // namespace filesystem
}  // namespace foundation