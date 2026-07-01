cmake_minimum_required(VERSION 3.16)

project(xbox_content_installer
    VERSION 1.0.0
    DESCRIPTION "Cross-platform CLI tool for installing Xbox 360 TU/DLC content (Xenia-compatible)"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global compile settings - enforce modern C++ and strict safety checks
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Compiler flags - maximum safety + performance
# ---------------------------------------------------------------------------
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
)

# Debug-only flags
add_compile_options(
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-ggdb3>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)

# Release-only flags - LTO + optimized for speed
add_compile_options(
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Release>:-flto>
)
add_link_options($<$<CONFIG:Release>:-flto>)

# Hardening flags (security)
if(NOT MSVC)
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
    add_link_options(-fPIE -pie -Wl,-z,relro,-z,now)
endif()

# MSVC-specific hardening
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /GS /Gs1048576)
endif()

# ---------------------------------------------------------------------------
# Threads (required for thread pool)
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Library target - all logic in static lib for testability
# ---------------------------------------------------------------------------
add_library(xbox_core STATIC
    src/core/errors.cpp
    src/core/logger.cpp
    src/crypto/sha1.cpp
    src/io/file_io.cpp
    src/io/memory_map.cpp
    src/io/buffered_writer.cpp
    src/concurrency/thread_pool.cpp
    src/stfs/stfs_header.cpp
    src/stfs/stfs_metadata.cpp
    src/stfs/stfs_volume.cpp
    src/stfs/stfs_hash.cpp
    src/stfs/stfs_file.cpp
    src/stfs/stfs_reader.cpp
    src/stfs/stfs_extractor.cpp
    src/stfs/svod_container.cpp
    src/installer/path_resolver.cpp
    src/installer/conflict_policy.cpp
    src/installer/installer.cpp
    src/cli/cli_parser.cpp
    src/cli/commands.cpp
    src/cli/progress.cpp
    src/utils/string_utils.cpp
    src/utils/path_utils.cpp
)

target_include_directories(xbox_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(xbox_core PUBLIC cxx_std_20)

target_link_libraries(xbox_core
    PUBLIC
        Threads::Threads
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(xbox_core PUBLIC bcrypt)
endif()

# ---------------------------------------------------------------------------
# Executable target
# ---------------------------------------------------------------------------
add_executable(xbox-install src/main.cpp)
target_link_libraries(xbox-install PRIVATE xbox_core)
set_target_properties(xbox-install PROPERTIES OUTPUT_NAME xbox-install)

# ---------------------------------------------------------------------------
# Tests (optional, controlled by BUILD_TESTING)
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()

    # Lightweight header-only test framework is bundled in tests/test_framework.hpp
    add_executable(test_xbox_core
        tests/test_main.cpp
        tests/test_endianness.cpp
        tests/test_sha1.cpp
        tests/test_stfs_header.cpp
        tests/test_stfs_path_resolver.cpp
        tests/test_thread_pool.cpp
        tests/test_string_utils.cpp
        tests/test_volume_descriptor.cpp
        tests/test_svod.cpp
    )
    target_link_libraries(test_xbox_core PRIVATE xbox_core)
    target_compile_definitions(test_xbox_core PRIVATE TESTING_BUILD=1)

    add_test(NAME xbox_core_tests COMMAND test_xbox_core)
endif()

# ---------------------------------------------------------------------------
# Install rules (optional, for system-wide install)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS xbox-install
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS xbox_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/xbox
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "==============================================")
message(STATUS "  xbox_content_installer v${PROJECT_VERSION}")
message(STATUS "==============================================")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform:      ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build testing: ${BUILD_TESTING}")
message(STATUS "==============================================")
