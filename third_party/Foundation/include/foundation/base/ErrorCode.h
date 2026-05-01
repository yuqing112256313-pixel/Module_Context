#ifndef FOUNDATION_BASE_ERRORCODE_H_
#define FOUNDATION_BASE_ERRORCODE_H_

#include "foundation/base/Export.h"

namespace foundation {
namespace base {

enum class ErrorCode : int {
    kOk = 0,

    kUnknown = 1,
    kInvalidArgument = 2,
    kInvalidState = 3,
    kNotFound = 4,
    kAlreadyExists = 5,
    kPermissionDenied = 6,
    kNotSupported = 7,
    kTimeout = 8,
    kCancelled = 9,

    kOutOfMemory = 10,
    kOutOfRange = 11,
    kOverflow = 12,
    kBufferTooSmall = 13,

    kIoError = 14,
    kOpenFailed = 14,
    kParseError = 15,
    kEndOfFile = 16,
    kChecksumMismatch = 17,

    kBusy = 18,
    kDisconnected = 19,
    kRejected = 20,
    kShutdownInProgress = 21,
    kQueueFull = 22,
    kQueueEmpty = 23,
    kQueueClosed = 24,

    kLibraryLoadFailed = 25,
    kLibraryLoadError = kLibraryLoadFailed,
    kSymbolNotFound = 26,
    kAbiMismatch = 27,
    kVersionMismatch = 28,
    kCreateFailed = 29,
    kOperationFailed = 30
};

FOUNDATION_API const char* ErrorCodeToString(ErrorCode code);

inline bool IsOk(ErrorCode code) {
    return code == ErrorCode::kOk;
}

inline bool IsError(ErrorCode code) {
    return code != ErrorCode::kOk;
}

inline int ErrorCodeValue(ErrorCode code) noexcept {
    return static_cast<int>(code);
}

}  // namespace base
}  // namespace foundation

#endif  // FOUNDATION_BASE_ERRORCODE_H_
