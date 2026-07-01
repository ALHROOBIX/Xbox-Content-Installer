// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ALHROOBIX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace xbox {

// ---------------------------------------------------------------------------
// High-level error categories
// ---------------------------------------------------------------------------
enum class ErrorCategory : u8 {
    Unknown = 0,
    Io,
    Stfs,
    Crypto,
    Installer,
    Cli,
    Concurrency,
    Internal,
};

// Specific error codes within each category.
// These are deliberately fine-grained to enable programmatic handling.
enum class ErrorCode : u32 {
    // Generic
    Ok = 0,
    Unknown,

    // I/O
    FileNotFound,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileSeekFailed,
    FileStatFailed,
    FileTooSmall,
    PermissionDenied,
    DiskFull,
    PathTooLong,
    DirectoryCreateFailed,
    DirectoryNotEmpty,

    // STFS
    InvalidMagic,
    InvalidHeaderSize,
    InvalidMetadataVersion,
    InvalidVolumeDescriptor,
    InvalidFileEntry,
    InvalidBlockIndex,
    BlockOffsetOutOfBounds,
    HashTableMissing,
    HashVerificationFailed,
    BlockStatusMismatch,
    UnsupportedFormat,

    // Crypto
    Sha1InitFailed,
    Sha1UpdateFailed,
    Sha1FinalFailed,
    HashLengthMismatch,

    // Installer
    ContentAlreadyInstalled,
    ContentNotInstalled,
    ConflictResolutionFailed,
    InvalidTitleId,
    InvalidContentType,
    InvalidContentId,
    InstallAborted,

    // CLI
    InvalidArgument,
    MissingRequiredArgument,
    UnknownCommand,
    AmbiguousCommand,

    // Concurrency
    ThreadPoolStopped,
    TaskExecutionFailed,

    // Internal (should never happen)
    AssertionFailed,
    Unimplemented,
    InvalidState,
};

// ---------------------------------------------------------------------------
// Error: lightweight value-only error type
// ---------------------------------------------------------------------------
class Error {
public:
    Error() = default;

    Error(ErrorCode code, ErrorCategory cat, std::string msg)
        : code_(code), category_(cat), message_(std::move(msg)) {}

    // Convenience constructor for system errors (errno/GetLastError)
    Error(ErrorCode code, ErrorCategory cat, std::string_view context, std::errc sys_err)
        : code_(code), category_(cat),
          message_(make_system_message(context, sys_err)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] ErrorCategory category() const noexcept { return category_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool is_ok() const noexcept { return code_ == ErrorCode::Ok; }

    // Explicit conversion to Result<T, Error> (avoids ambiguity in std::variant).
    // Use the Err{error} constructor directly when implicit conversion is needed.
    template <typename T>
    explicit operator Result<T, Error>() const & { return Result<T, Error>{Err<Error>{*this}}; }
    template <typename T>
    explicit operator Result<T, Error>() && { return Result<T, Error>{Err<Error>{std::move(*this)}}; }

    // Pretty-printed single-line representation
    [[nodiscard]] std::string to_string() const;

private:
    static std::string make_system_message(std::string_view context, std::errc err);

    ErrorCode    code_{ErrorCode::Unknown};
    ErrorCategory category_{ErrorCategory::Unknown};
    std::string  message_;
};

// Convenience constructors
template <typename... Args>
[[nodiscard]] Error make_error(ErrorCode code, ErrorCategory cat, std::string_view fmt_str, Args&&... args) {
    return Error{code, cat, std::vformat(fmt_str, std::make_format_args(args...))};
}

[[nodiscard]] inline Error make_io_error(ErrorCode code, std::string_view msg) {
    return Error{code, ErrorCategory::Io, std::string{msg}};
}

[[nodiscard]] inline Error make_stfs_error(ErrorCode code, std::string_view msg) {
    return Error{code, ErrorCategory::Stfs, std::string{msg}};
}

[[nodiscard]] inline Error make_installer_error(ErrorCode code, std::string_view msg) {
    return Error{code, ErrorCategory::Installer, std::string{msg}};
}

[[nodiscard]] inline Error make_cli_error(ErrorCode code, std::string_view msg) {
    return Error{code, ErrorCategory::Cli, std::string{msg}};
}

[[nodiscard]] inline Error make_crypto_error(ErrorCode code, std::string_view msg) {
    return Error{code, ErrorCategory::Crypto, std::string{msg}};
}

// Helper macros for concise error construction at call-sites.
#define XBOX_IO_ERROR(code, msg)        ::xbox::make_io_error(::xbox::ErrorCode::code, msg)
#define XBOX_STFS_ERROR(code, msg)      ::xbox::make_stfs_error(::xbox::ErrorCode::code, msg)
#define XBOX_INSTALL_ERROR(code, msg)   ::xbox::make_installer_error(::xbox::ErrorCode::code, msg)
#define XBOX_CLI_ERROR(code, msg)       ::xbox::make_cli_error(::xbox::ErrorCode::code, msg)
#define XBOX_CRYPTO_ERROR(code, msg)    ::xbox::make_crypto_error(::xbox::ErrorCode::code, msg)

// Try-style macro: unwrap a Result or propagate its error.
// Usage (as a statement, NOT as an expression):
//   XBOX_TRY(some_result_returning_function());   // discards success value
//   XBOX_TRY_ASSIGN(v, some_result_returning_function());  // assigns to v
#define XBOX_TRY(expr) \
    do { \
        auto _r = (expr); \
        if (!_r.is_ok()) { \
            return ::xbox::Err{std::move(_r.error())}; \
        } \
    } while (0)

// Variant that assigns the success value to `out`.
//   XBOX_TRY_ASSIGN(v, some_result_returning_function());
#define XBOX_TRY_ASSIGN(out, expr) \
    do { \
        auto _r = (expr); \
        if (!_r.is_ok()) { \
            return ::xbox::Err{std::move(_r.error())}; \
        } \
        out = std::move(_r).value(); \
    } while (0)

} // namespace xbox
