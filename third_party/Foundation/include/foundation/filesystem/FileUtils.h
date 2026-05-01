#ifndef FOUNDATION_FILESYSTEM_FILEUTILS_H_
#define FOUNDATION_FILESYSTEM_FILEUTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "foundation/base/Export.h"
#include "foundation/base/Result.h"

namespace foundation {
namespace filesystem {

// 查询类接口：无异常、无错误消息返回，适合高频判断场景。
FOUNDATION_API bool Exists(const std::string& path);
FOUNDATION_API bool IsDirectory(const std::string& path);
FOUNDATION_API bool IsRegularFile(const std::string& path);

// 目录创建。
// 语义类似 mkdir -p：
// - 目录已存在 -> 成功
// - 中间父目录不存在 -> 自动递归创建
// - 路径为空 -> kInvalidArgument
// - 路径存在但不是目录 -> kAlreadyExists
FOUNDATION_API base::Result<void> CreateDirectories(const std::string& path);

// 文件读取。
// ReadAllText 按二进制方式读取，不做编码转换；适合 UTF-8 / ANSI 等由上层自行解释。
// ReadAllBytes 用于二进制文件。
FOUNDATION_API base::Result<std::string> ReadAllText(const std::string& path);
FOUNDATION_API base::Result<std::vector<std::uint8_t> > ReadAllBytes(const std::string& path);

// 文件写入。
// 写入前会自动创建父目录。
// WriteAllXXX 为覆盖写；AppendAllText 为追加写。
FOUNDATION_API base::Result<void> WriteAllText(const std::string& path,
                                               const std::string& content);
FOUNDATION_API base::Result<void> WriteAllBytes(
    const std::string& path,
    const std::vector<std::uint8_t>& content);
FOUNDATION_API base::Result<void> AppendAllText(const std::string& path,
                                                const std::string& content);

// 文件信息。
FOUNDATION_API base::Result<std::uint64_t> GetFileSize(const std::string& path);

// 删除。
// RemoveFile 仅删除文件；若 path 指向目录，则返回 kInvalidArgument。
// RemoveDirectory 仅删除空目录；若目录非空，返回 kIoError。
FOUNDATION_API base::Result<void> RemoveFile(const std::string& path);
FOUNDATION_API base::Result<void> RemoveDirectoryIfEmpty(const std::string& path);

// 文件复制。
// - overwrite = false 且目标已存在 -> kAlreadyExists
// - 自动创建目标父目录
// - 仅支持普通文件复制，不支持目录复制
FOUNDATION_API base::Result<void> CopyFileTo(const std::string& from,
                                           const std::string& to,
                                           bool overwrite);

}  // namespace filesystem
}  // namespace foundation

#endif  // FOUNDATION_FILESYSTEM_FILEUTILS_H_