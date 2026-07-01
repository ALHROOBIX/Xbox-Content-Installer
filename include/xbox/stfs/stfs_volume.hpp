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
#include "xbox/stfs/stfs_header.hpp"

#include <span>

namespace xbox::stfs {

// Parse a 0x24-byte volume descriptor from a buffer.
[[nodiscard]] Result<VolumeDescriptor, Error> parse_volume_descriptor(
    std::span<const byte> buffer);

// Parse the volume descriptor from a full STFS header buffer.
// Reads from offset stfs::off::VOLUME_DESCRIPTOR.
[[nodiscard]] Result<VolumeDescriptor, Error> parse_volume_descriptor_from_header(
    std::span<const byte> header_buffer);

} // namespace xbox::stfs
