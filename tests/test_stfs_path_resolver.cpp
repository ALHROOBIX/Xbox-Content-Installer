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
#include "test_framework.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/installer/path_resolver.hpp"
#include "xbox/stfs/stfs_header.hpp"
#include "xbox/utils/path_utils.hpp"

#include <filesystem>

using namespace xbox;
using namespace xbox::installer;
using namespace xbox::path;

namespace fs = std::filesystem;

namespace {

// Build a minimal STFS header with given title_id, content_type, and content_id
stfs::StfsHeader make_header(u32 title_id, u32 content_type,
                              const std::array<u8, 20>& content_id) {
    stfs::StfsHeader h{};
    h.title_id = title_id;
    h.content_type = content_type;
    h.content_id = content_id;
    h.metadata_version = 1;
    h.volume_type = VolumeType::Stfs;
    h.profile_id.fill(0);
    return h;
}

} // namespace

TEST(PathResolver_DLCPath) {
    // Per Xenia: DLC (kMarketplaceContent = 0x00000002) forces xuid = 0
    // Path: content/0000000000000000/<title_id>/00000002/<file_name>/
    PathResolver resolver("content");
    auto h = make_header(0x415607ED, 0x00000002, {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD
    });
    auto r = resolver.resolve(h, "MyDLC.xzp");
    ASSERT_TRUE(r.is_ok());

    const auto& loc = r.value();
    EXPECT_EQ(loc.root_dir.string(), "content");
    EXPECT_EQ(loc.xuid_dir.string(), "content/0000000000000000");
    EXPECT_EQ(loc.title_dir.string(), "content/0000000000000000/415607ED");
    EXPECT_EQ(loc.content_type_dir.string(), "content/0000000000000000/415607ED/00000002");
    EXPECT_EQ(loc.file_name, "MyDLC");
    EXPECT_EQ(loc.content_id_dir.string(),
              "content/0000000000000000/415607ED/00000002/MyDLC");
    EXPECT_EQ(loc.xuid, 0ull);  // DLC forces xuid = 0
}

TEST(PathResolver_TUPath) {
    // TU (kInstaller = 0x000B0000) uses the profile_id from header as xuid
    PathResolver resolver("content");
    auto h = make_header(0x415607ED, 0x000B0000, {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD
    });
    // Set profile_id to a non-zero value (8 bytes BE)
    h.profile_id = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34};

    auto r = resolver.resolve(h, "TU_8.xzp");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().xuid_dir.string(), "content/0000000000001234");
    EXPECT_EQ(r.value().title_dir.string(), "content/0000000000001234/415607ED");
    EXPECT_EQ(r.value().content_type_dir.string(), "content/0000000000001234/415607ED/000B0000");
    EXPECT_EQ(r.value().file_name, "TU_8");
    EXPECT_EQ(r.value().content_id_dir.string(),
              "content/0000000000001234/415607ED/000B0000/TU_8");
}

TEST(PathResolver_DisabledPath) {
    PathResolver resolver("content");
    auto h = make_header(0x415607ED, 0x000B0000, {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23
    });
    auto r = resolver.resolve(h, "TU.bin");
    ASSERT_TRUE(r.is_ok());

    auto dp = resolver.disabled_path(r.value());
    EXPECT_EQ(dp.string(),
              "content/0000000000000000/415607ED/000B0000/TU.disabled");
}

TEST(PathResolver_EnsureInstallDir) {
    auto tmp = fs::temp_directory_path() / "xbox_test_resolve";
    fs::remove_all(tmp);
    PathResolver resolver(tmp);
    auto h = make_header(0x415607ED, 0x000B0000, {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23
    });
    auto r = resolver.ensure_install_dir(h, "TU.bin");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(fs::exists(r.value().content_id_dir));
    EXPECT_TRUE(fs::is_directory(r.value().content_id_dir));
    fs::remove_all(tmp);
}

TEST(PathResolver_ListInstalledEmpty) {
    auto tmp = fs::temp_directory_path() / "xbox_test_empty";
    fs::remove_all(tmp);
    PathResolver resolver(tmp);
    auto titles = resolver.list_installed_titles();
    ASSERT_TRUE(titles.is_ok());
    EXPECT_TRUE(titles.value().empty());
    fs::remove_all(tmp);
}

TEST(PathResolver_ListInstalledWithContent) {
    auto tmp = fs::temp_directory_path() / "xbox_test_list";
    fs::remove_all(tmp);

    // Manually create the directory structure matching Xenia's layout
    auto content_dir = tmp / "0000000000000000" / "415607ED" / "000B0000" / "MyTU";
    fs::create_directories(content_dir);

    PathResolver resolver(tmp);
    auto titles = resolver.list_installed_titles();
    ASSERT_TRUE(titles.is_ok());
    ASSERT_EQ(titles.value().size(), 1u);
    EXPECT_EQ(titles.value()[0], 0x415607EDu);

    auto entries = resolver.list_installed_for_title(0x415607ED);
    ASSERT_TRUE(entries.is_ok());
    ASSERT_EQ(entries.value().size(), 1u);
    EXPECT_EQ(entries.value()[0].content_type, 0x000B0000u);
    EXPECT_EQ(entries.value()[0].file_name, "MyTU");
    EXPECT_EQ(entries.value()[0].xuid, 0ull);
    EXPECT_FALSE(entries.value()[0].is_disabled);

    fs::remove_all(tmp);
}

TEST(PathResolver_ListInstalledDisabled) {
    auto tmp = fs::temp_directory_path() / "xbox_test_disabled";
    fs::remove_all(tmp);

    auto content_dir = tmp / "0000000000000000" / "415607ED" / "000B0000" / "MyTU.disabled";
    fs::create_directories(content_dir);

    PathResolver resolver(tmp);
    auto entries = resolver.list_installed_for_title(0x415607ED);
    ASSERT_TRUE(entries.is_ok());
    ASSERT_EQ(entries.value().size(), 1u);
    EXPECT_TRUE(entries.value()[0].is_disabled);
    EXPECT_EQ(entries.value()[0].file_name, "MyTU");

    fs::remove_all(tmp);
}

TEST(PathResolver_FindInstalledByName) {
    auto tmp = fs::temp_directory_path() / "xbox_test_find";
    fs::remove_all(tmp);

    auto content_dir = tmp / "0000000000000000" / "415607ED" / "000B0000" / "MyTU";
    fs::create_directories(content_dir);

    PathResolver resolver(tmp);
    auto r1 = resolver.find_installed(0x415607ED, "MyTU");
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.value().file_name, "MyTU");

    auto r2 = resolver.find_installed(0x415607ED, "My");
    ASSERT_TRUE(r2.is_ok());

    auto r3 = resolver.find_installed(0x415607ED, "nonexistent");
    EXPECT_FALSE(r3.is_ok());

    fs::remove_all(tmp);
}

TEST(PathResolver_FormatTitleId) {
    EXPECT_EQ(format_title_id(0x415607ED), "415607ED");
    EXPECT_EQ(format_title_id(0x00000001), "00000001");
    EXPECT_EQ(format_title_id(0xFFFFFFFF), "FFFFFFFF");
}

TEST(PathResolver_StripExtension) {
    PathResolver resolver("content");
    auto h = make_header(0x415607ED, 0x00000002, {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD
    });

    auto r1 = resolver.resolve(h, "test_dlc.xzp");
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.value().file_name, "test_dlc");

    auto r2 = resolver.resolve(h, "test_dlc");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value().file_name, "test_dlc");

    auto r3 = resolver.resolve(h, "/path/to/test_dlc.xzp");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(r3.value().file_name, "test_dlc");
}

TEST(PathResolver_XuidOverride) {
    PathResolver resolver("content");
    resolver.set_xuid_override(0xDEADBEEFCAFE1234ull);

    auto h = make_header(0x415607ED, 0x000B0000, {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD
    });

    auto r = resolver.resolve(h, "TU.xzp");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().xuid, 0xDEADBEEFCAFE1234ull);

    // When --xuid is explicitly set, it overrides even DLC's default xuid=0
    auto h_dlc = make_header(0x415607ED, 0x00000002, {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
        0xAA, 0xBB, 0xCC, 0xDD
    });
    auto r_dlc = resolver.resolve(h_dlc, "DLC.xzp");
    ASSERT_TRUE(r_dlc.is_ok());
    EXPECT_EQ(r_dlc.value().xuid, 0xDEADBEEFCAFE1234ull);  // override applies to DLC too
}

TEST(LicenseEntry_ActiveFlag) {
    stfs::LicenseEntry lic{};
    lic.license_flags = 0;
    EXPECT_FALSE(lic.is_active());

    lic.license_flags = 1;
    EXPECT_TRUE(lic.is_active());

    lic.license_flags = 0xFFFFFFFFu;
    EXPECT_TRUE(lic.is_active());
}

TEST(LicenseEntry_ComputeMask) {
    std::array<stfs::LicenseEntry, license::ENTRY_COUNT> licenses{};

    EXPECT_EQ(stfs::compute_license_mask(licenses), 0u);

    licenses[0].license_flags = 1;
    licenses[0].license_bits = 0x000000FFu;
    EXPECT_EQ(stfs::compute_license_mask(licenses), 0x000000FFu);

    licenses[1].license_flags = 1;
    licenses[1].license_bits = 0x0000FF00u;
    EXPECT_EQ(stfs::compute_license_mask(licenses), 0x0000FFFFu);

    licenses[2].license_flags = 0;
    licenses[2].license_bits = 0xFFFFFFFFu;
    EXPECT_EQ(stfs::compute_license_mask(licenses), 0x0000FFFFu);
}
