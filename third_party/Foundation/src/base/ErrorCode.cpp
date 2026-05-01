#include "foundation/base/ErrorCode.h"

namespace foundation {
namespace base {

const char* ErrorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk: return "Ok";
        case ErrorCode::kUnknown: return "Unknown";
        case ErrorCode::kInvalidArgument: return "InvalidArgument";
        case ErrorCode::kInvalidState: return "InvalidState";
        case ErrorCode::kNotFound: return "NotFound";
        case ErrorCode::kAlreadyExists: return "AlreadyExists";
        case ErrorCode::kPermissionDenied: return "PermissionDenied";
        case ErrorCode::kNotSupported: return "NotSupported";
        case ErrorCode::kTimeout: return "Timeout";
        case ErrorCode::kCancelled: return "Cancelled";
        case ErrorCode::kOutOfMemory: return "OutOfMemory";
        case ErrorCode::kOutOfRange: return "OutOfRange";
        case ErrorCode::kOverflow: return "Overflow";
        case ErrorCode::kBufferTooSmall: return "BufferTooSmall";
        case ErrorCode::kIoError: return "IoError";
		//case ErrorCode::kOpenFailed: return "OpenFailed";
        case ErrorCode::kParseError: return "ParseError";
        case ErrorCode::kEndOfFile: return "EndOfFile";
        case ErrorCode::kChecksumMismatch: return "ChecksumMismatch";
        case ErrorCode::kBusy: return "Busy";
        case ErrorCode::kDisconnected: return "Disconnected";
        case ErrorCode::kRejected: return "Rejected";
        case ErrorCode::kShutdownInProgress: return "ShutdownInProgress";
        case ErrorCode::kQueueFull: return "QueueFull";
        case ErrorCode::kQueueEmpty: return "QueueEmpty";
        case ErrorCode::kQueueClosed: return "QueueClosed";
        case ErrorCode::kLibraryLoadFailed: return "LibraryLoadFailed";
        case ErrorCode::kSymbolNotFound: return "SymbolNotFound";
        case ErrorCode::kAbiMismatch: return "AbiMismatch";
        case ErrorCode::kVersionMismatch: return "VersionMismatch";
        case ErrorCode::kCreateFailed: return "CreateFailed";
        case ErrorCode::kOperationFailed: return "OperationFailed";
        default: return "UnknownErrorCode";
    }
}

}  // namespace base
}  // namespace foundation
