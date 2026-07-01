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
#include "xbox/core/errors.hpp"

#include <cstring>
#include <system_error>

namespace xbox {

std::string Error::make_system_message(std::string_view context, std::errc err) {
    auto sys_msg = std::make_error_code(err).message();
    std::string out;
    out.reserve(context.size() + sys_msg.size() + 4);
    out.append(context);
    out.append(": ");
    out.append(sys_msg);
    return out;
}

namespace {
constexpr std::string_view category_name(ErrorCategory c) noexcept {
    switch (c) {
        case ErrorCategory::Unknown:      return "unknown";
        case ErrorCategory::Io:           return "io";
        case ErrorCategory::Stfs:         return "stfs";
        case ErrorCategory::Crypto:       return "crypto";
        case ErrorCategory::Installer:    return "installer";
        case ErrorCategory::Cli:          return "cli";
        case ErrorCategory::Concurrency:  return "concurrency";
        case ErrorCategory::Internal:     return "internal";
    }
    return "?";
}

constexpr std::string_view code_name(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::Ok:                          return "ok";
        case ErrorCode::Unknown:                     return "unknown";
        case ErrorCode::FileNotFound:                return "file_not_found";
        case ErrorCode::FileOpenFailed:              return "file_open_failed";
        case ErrorCode::FileReadFailed:              return "file_read_failed";
        case ErrorCode::FileWriteFailed:             return "file_write_failed";
        case ErrorCode::FileSeekFailed:              return "file_seek_failed";
        case ErrorCode::FileStatFailed:              return "file_stat_failed";
        case ErrorCode::FileTooSmall:                return "file_too_small";
        case ErrorCode::PermissionDenied:            return "permission_denied";
        case ErrorCode::DiskFull:                    return "disk_full";
        case ErrorCode::PathTooLong:                 return "path_too_long";
        case ErrorCode::DirectoryCreateFailed:       return "directory_create_failed";
        case ErrorCode::DirectoryNotEmpty:           return "directory_not_empty";
        case ErrorCode::InvalidMagic:                return "invalid_magic";
        case ErrorCode::InvalidHeaderSize:           return "invalid_header_size";
        case ErrorCode::InvalidMetadataVersion:      return "invalid_metadata_version";
        case ErrorCode::InvalidVolumeDescriptor:     return "invalid_volume_descriptor";
        case ErrorCode::InvalidFileEntry:            return "invalid_file_entry";
        case ErrorCode::InvalidBlockIndex:           return "invalid_block_index";
        case ErrorCode::BlockOffsetOutOfBounds:      return "block_offset_out_of_bounds";
        case ErrorCode::HashTableMissing:            return "hash_table_missing";
        case ErrorCode::HashVerificationFailed:      return "hash_verification_failed";
        case ErrorCode::BlockStatusMismatch:         return "block_status_mismatch";
        case ErrorCode::UnsupportedFormat:           return "unsupported_format";
        case ErrorCode::Sha1InitFailed:              return "sha1_init_failed";
        case ErrorCode::Sha1UpdateFailed:            return "sha1_update_failed";
        case ErrorCode::Sha1FinalFailed:             return "sha1_final_failed";
        case ErrorCode::HashLengthMismatch:          return "hash_length_mismatch";
        case ErrorCode::ContentAlreadyInstalled:     return "content_already_installed";
        case ErrorCode::ContentNotInstalled:         return "content_not_installed";
        case ErrorCode::ConflictResolutionFailed:    return "conflict_resolution_failed";
        case ErrorCode::InvalidTitleId:              return "invalid_title_id";
        case ErrorCode::InvalidContentType:          return "invalid_content_type";
        case ErrorCode::InvalidContentId:            return "invalid_content_id";
        case ErrorCode::InstallAborted:              return "install_aborted";
        case ErrorCode::InvalidArgument:             return "invalid_argument";
        case ErrorCode::MissingRequiredArgument:     return "missing_required_argument";
        case ErrorCode::UnknownCommand:              return "unknown_command";
        case ErrorCode::AmbiguousCommand:            return "ambiguous_command";
        case ErrorCode::ThreadPoolStopped:           return "thread_pool_stopped";
        case ErrorCode::TaskExecutionFailed:         return "task_execution_failed";
        case ErrorCode::AssertionFailed:             return "assertion_failed";
        case ErrorCode::Unimplemented:               return "unimplemented";
        case ErrorCode::InvalidState:                return "invalid_state";
    }
    return "?";
}
} // namespace

std::string Error::to_string() const {
    std::string out;
    out.reserve(message_.size() + 32);
    out.append("[");
    out.append(category_name(category_));
    out.append(":");
    out.append(code_name(code_));
    out.append("] ");
    out.append(message_);
    return out;
}

} // namespace xbox
