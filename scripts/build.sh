#!/bin/bash
set -e
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
mkdir -p "$BUILD_DIR"

BUILD_LINUX=1
BUILD_WINDOWS=0
BUILD_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --windows|windows|win)  BUILD_WINDOWS=1; BUILD_LINUX=0 ;;
        --tests|tests)          BUILD_TESTS=1 ;;
        --all|all)              BUILD_WINDOWS=1; BUILD_TESTS=1 ;;
        --linux-only|linux)     BUILD_LINUX=1; BUILD_WINDOWS=0 ;;
    esac
done

COMMON_FLAGS=(
    -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
    -Wconversion -Wsign-conversion -Wnull-dereference
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
    -O2 -DNDEBUG -I include
)

build_linux() {
    echo "=== Building for Linux ==="
    local CXX="${CXX:-g++}"
    local CXXFLAGS=("${COMMON_FLAGS[@]}")
    local PLATFORM_LIBS="-lpthread"
    local ZIP_LIBS=""
    if [ -f /usr/include/minizip/unzip.h ]; then
        ZIP_LIBS="-lminizip -lz"; CXXFLAGS+=(-DHAS_MINIZIP)
    fi
    local CORE_SOURCES=$(find src -name "*.cpp" -not -name "main.cpp" | sort)
    local OBJ_DIR="$BUILD_DIR/obj"; mkdir -p "$OBJ_DIR"
    echo "[1/3] Compiling..."
    for src in $CORE_SOURCES; do
        $CXX "${CXXFLAGS[@]}" -c "$src" -o "$OBJ_DIR/$(basename $src .cpp).o"
    done
    echo "[2/3] Static library..."
    ar rcs "$BUILD_DIR/libxbox_core.a" $OBJ_DIR/*.o
    echo "[3/3] Linking..."
    $CXX "${CXXFLAGS[@]}" src/main.cpp -o "$BUILD_DIR/xbox-install" \
        -L "$BUILD_DIR" -lxbox_core $PLATFORM_LIBS $ZIP_LIBS
    echo "✅ Linux: $BUILD_DIR/xbox-install"
}

build_windows() {
    echo "=== Building for Windows (cross-compile) ==="
    export PATH="/home/z/mingw-local/usr/bin:$PATH"
    local WIN_CXX="x86_64-w64-mingw32-g++"
    if ! command -v "$WIN_CXX" &>/dev/null; then
        echo "❌ MinGW not found"; return 1
    fi
    local WIN_SYSROOT="/home/z/mingw-local/usr/x86_64-w64-mingw32"
    local WIN_BUILD_DIR="${BUILD_DIR}-win"; mkdir -p "$WIN_BUILD_DIR/obj"
    local WIN_CXXFLAGS=("${COMMON_FLAGS[@]}" -static -static-libgcc -static-libstdc++
        -Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va
        -I"$WIN_SYSROOT/include")
    local MINIZIP_SRC=""
    local WIN_ZIP_LIBS="-lz"
    if [ -f "third_party/minizip/unzip.c" ]; then
        MINIZIP_SRC="third_party/minizip/unzip.c third_party/minizip/ioapi.c"
        WIN_CXXFLAGS+=(-DHAS_MINIZIP -DHAS_STDINT_H -Ithird_party/minizip)
    fi
    local WIN_PLATFORM_LIBS="-lws2_32 -lbcrypt -lwinpthread"
    local WIN_LDFLAGS="-L$WIN_SYSROOT/lib"
    local CORE_SOURCES=$(find src -name "*.cpp" -not -name "main.cpp" | sort)
    echo "[1/4] Compiling C++..."
    for src in $CORE_SOURCES; do
        $WIN_CXX "${WIN_CXXFLAGS[@]}" -c "$src" -o "$WIN_BUILD_DIR/obj/$(basename $src .cpp).o"
    done
    echo "[2/4] Compiling minizip..."
    if [ -n "$MINIZIP_SRC" ]; then
        local WIN_CC="x86_64-w64-mingw32-gcc"
        for src in $MINIZIP_SRC; do
            $WIN_CC -O2 -DNDEBUG -DHAS_MINIZIP -DHAS_STDINT_H -Ithird_party/minizip -I"$WIN_SYSROOT/include" -c "$src" -o "$WIN_BUILD_DIR/obj/$(basename $src .c).o"
        done
    fi
    echo "[3/4] Static library..."
    ar rcs "$WIN_BUILD_DIR/libxbox_core.a" $WIN_BUILD_DIR/obj/*.o
    echo "[4/4] Resources + Linking..."
    local WIN_RES_OBJ=""
    if command -v x86_64-w64-mingw32-windres &>/dev/null && [ -f "resources/version.rc" ]; then
        WIN_RES_OBJ="$WIN_BUILD_DIR/obj/version_res.o"
        x86_64-w64-mingw32-windres -I resources -I "$WIN_SYSROOT/include" resources/version.rc -o "$WIN_RES_OBJ"
        echo "  ✓ Manifest embedded"
    fi
    $WIN_CXX "${WIN_CXXFLAGS[@]}" src/main.cpp -o "$WIN_BUILD_DIR/xbox-install.exe" \
        -L "$WIN_BUILD_DIR" -lxbox_core $WIN_LDFLAGS $WIN_PLATFORM_LIBS $WIN_ZIP_LIBS $WIN_RES_OBJ
    x86_64-w64-mingw32-strip "$WIN_BUILD_DIR/xbox-install.exe" 2>/dev/null || true
    echo "✅ Windows: $WIN_BUILD_DIR/xbox-install.exe"
    if [ -d "/home/z/my-project/download" ]; then
        cp "$WIN_BUILD_DIR/xbox-install.exe" "/home/z/my-project/download/xbox-install.exe"
        cp resources/unblock.bat "/home/z/my-project/download/unblock.bat" 2>/dev/null || true
    fi
}

if [ "$BUILD_LINUX" = "1" ]; then build_linux; fi
if [ "$BUILD_WINDOWS" = "1" ]; then build_windows; fi
echo "=== Done ==="
