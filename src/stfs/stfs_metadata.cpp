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
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/stfs/stfs_header.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/utils/string_utils.hpp"

#include <cstring>

namespace xbox::stfs {

bool is_stfs_magic(const void* first_4_bytes) noexcept {
    return identify_package_type(first_4_bytes) != PackageType::Unknown;
}

Result<u32, Error> quick_read_title_id(std::span<const byte> buffer) {
    if (buffer.size() < stfs::off::TITLE_ID + 4) {
        return XBOX_STFS_ERROR(InvalidHeaderSize,
            "buffer too small to read title ID");
    }
    return endian::read_u32_be(buffer.data() + stfs::off::TITLE_ID);
}

Result<u32, Error> quick_read_content_type(std::span<const byte> buffer) {
    if (buffer.size() < stfs::off::CONTENT_TYPE + 4) {
        return XBOX_STFS_ERROR(InvalidHeaderSize, "buffer too small to read content type");
    }
    return endian::read_u32_be(buffer.data() + stfs::off::CONTENT_TYPE);
}

Result<std::array<u8, 20>, Error> quick_read_content_id(std::span<const byte> buffer) {
    if (buffer.size() < stfs::off::CONTENT_ID + 20) {
        return XBOX_STFS_ERROR(InvalidHeaderSize, "buffer too small to read content ID");
    }
    std::array<u8, 20> id{};
    std::memcpy(id.data(), buffer.data() + stfs::off::CONTENT_ID, 20);
    return id;
}

Result<u32, Error> quick_read_metadata_version(std::span<const byte> buffer) {
    if (buffer.size() < stfs::off::METADATA_VERSION + 4) {
        return XBOX_STFS_ERROR(InvalidHeaderSize, "buffer too small to read metadata version");
    }
    return endian::read_u32_be(buffer.data() + stfs::off::METADATA_VERSION);
}

Result<std::string, Error> quick_read_display_name(std::span<const byte> buffer) {
    if (buffer.size() < stfs::off::DISPLAY_NAME + 12 * 0x80) {
        return XBOX_STFS_ERROR(InvalidHeaderSize, "buffer too small to read display name");
    }
    const u8* names = reinterpret_cast<const u8*>(buffer.data()) + stfs::off::DISPLAY_NAME;
    for (int i = 0; i < 12; ++i) {
        auto decoded = str::utf16be_cstr_to_utf8(names + i * 0x80, 0x80);
        if (!decoded.empty()) return decoded;
    }
    return std::string{};
}

std::string classify_content_type(u32 content_type) noexcept {
    switch (content_type) {
        case content_type::SAVED_GAME:          return "Saved Game";
        case content_type::MARKETPLACE_CONTENT: return "DLC / Marketplace Content";
        case content_type::PUBLISHER:           return "Publisher";
        case content_type::IPTV_PAUSE_BUFFER:   return "IPTV Pause Buffer";
        case content_type::XBOX_360_TITLE:      return "Xbox 360 Title";
        case content_type::XBOX_ORIGINAL_GAME:  return "Xbox Original Game";
        case content_type::INSTALLED_GAME:      return "Installed Game";
        case content_type::GAMER_PICTURE:       return "Gamer Picture";
        case content_type::THEME:               return "Theme";
        case content_type::CACHE_FILE:          return "Cache File";
        case content_type::STORAGE_DOWNLOAD:    return "Storage Download";
        case content_type::XBOX_SAVED_GAME:     return "Xbox Saved Game";
        case content_type::XBOX_DOWNLOAD:       return "Xbox Download";
        case content_type::GAME_DEMO:           return "Game Demo";
        case content_type::AVATAR_ITEM:         return "Avatar Item";
        case content_type::VIDEO:               return "Video";
        case content_type::GAME_TITLE:          return "Game Title";
        case content_type::ARCADE_TITLE:        return "Arcade Title";
        case content_type::GAME_TRAILER:        return "Game Trailer";
        case content_type::TITLE_UPDATE:        return "Title Update (TU) / Installer";
        case content_type::LICENSE_STORE:       return "License Store";
        case content_type::MOVIE:               return "Movie";
        case content_type::GAME_VIDEO:          return "Game Video";
        case content_type::TV:                  return "TV";
        case content_type::MUSIC_VIDEO:         return "Music Video";
        case content_type::PODCAST_VIDEO:       return "Podcast Video";
        case content_type::VIRAL_VIDEO:         return "Viral Video";
        case content_type::COMMUNITY_GAME:      return "Community Game";
        case content_type::XNA:                 return "XNA";
        default: return "Unknown (0x" + str::format_hex_u32(content_type) + ")";
    }
}

} // namespace xbox::stfs
