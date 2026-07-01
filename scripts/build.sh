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
    echo "=== Building for Linux (fully static) ==="
    local CXX="${CXX:-g++}"
    local CXXFLAGS=("${COMMON_FLAGS[@]}")

    # CRITICAL: Static linking flags for maximum portability across Linux distros.
    #
    # Without these, the binary requires the EXACT same libstdc++/glibc version
    # as the build machine. For example, building on Debian Trixie (GCC 14,
    # GLIBCXX_3.4.35) produces a binary that fails on Linux Mint (GCC 12,
    # GLIBCXX_3.4.30) with:
    #   ./xbox-install: /lib/x86_64-linux-gnu/libstdc++.so.6: version
    #   `GLIBCXX_3.4.35' not found (required by ./xbox-install)
    #
    # Solution: statically link EVERYTHING (libstdc++, libgcc, pthread, zlib,
    # minizip) into the binary. The result is a self-contained executable that
    # runs on ANY Linux x86_64 system regardless of installed library versions.
    #
    # Flags:
    #   -static              = fully static binary (glibc + everything)
    #   -static-libgcc       = static libgcc (exception handling, etc.)
    #   -static-libstdc++    = static libstdc++ (C++ standard library)
    #   -lpthread -ldl       = thread + dynamic loading (statically linked)
    #
    # Note: -static may produce warnings about getpwnam/gethostbyname.
    # This is safe for our CLI tool — we don't do network ops or user lookups.
    local STATIC_FLAGS="-static -static-libgcc -static-libstdc++"
    local PLATFORM_LIBS="-lpthread -ldl"

    # For static linking, we need to bundle zlib and minizip source directly
    # (system shared libraries won't work with -static)
    local ZLIB_SRC=""
    local ZIP_FLAGS=""
    local ZIP_LIBS=""

    # Check for bundled zlib source (preferred for static builds)
    if [ -d "third_party/zlib" ] && [ -f "third_party/zlib/zlib.h" ]; then
        echo "  Using bundled zlib source (static)"
        ZLIB_SRC=$(find third_party/zlib -name "*.c" -not -name "example*" -not -name "minigzip*" -not -name "infcover*" | sort)
        ZIP_FLAGS="-Ithird_party/zlib -DHAS_ZLIB"
    elif [ -f "/usr/lib/x86_64-linux-gnu/libz.a" ] || [ -f "/usr/lib/libz.a" ]; then
        echo "  Using system static zlib"
        ZIP_LIBS="-lz"
    else
        echo "  WARNING: No static zlib found — building without ZIP support"
        echo "    Install: sudo apt install zlib1g-dev (Debian/Ubuntu)"
        echo "    Or:      sudo dnf install zlib-static (Fedora)"
    fi

    # Check for minizip — bundle source for static builds
    if [ -f "third_party/minizip/unzip.c" ]; then
        echo "  Using bundled minizip source (static)"
        ZIP_FLAGS="$ZIP_FLAGS -DHAS_MINIZIP -Ithird_party/minizip"
        ZLIB_SRC="$ZLIB_SRC third_party/minizip/unzip.c third_party/minizip/ioapi.c"
    elif [ -f /usr/include/minizip/unzip.h ]; then
        echo "  WARNING: system minizip is shared — building without ZIP for static"
        echo "    Copy minizip source to third_party/minizip/ for ZIP support"
    fi

    CXXFLAGS+=($ZIP_FLAGS)

    local CORE_SOURCES=$(find src -name "*.cpp" -not -name "main.cpp" | sort)
    local OBJ_DIR="$BUILD_DIR/obj"; mkdir -p "$OBJ_DIR"
    echo "[1/4] Compiling C++..."
    for src in $CORE_SOURCES; do
        $CXX "${CXXFLAGS[@]}" -c "$src" -o "$OBJ_DIR/$(basename $src .cpp).o"
    done

    echo "[2/4] Compiling zlib + minizip (C)..."
    local ZLIB_OBJS=""
    if [ -n "$ZLIB_SRC" ]; then
        for src in $ZLIB_SRC; do
            local obj="$OBJ_DIR/z_$(basename $src .c).o"
            $CXX "${CXXFLAGS[@]}" -x c -c "$src" -o "$obj" 2>/dev/null || \
                gcc -O2 -DNDEBUG -c "$src" -o "$obj" 2>/dev/null || true
            [ -f "$obj" ] && ZLIB_OBJS="$ZLIB_OBJS $obj"
        done
    fi

    echo "[3/4] Static library..."
    ar rcs "$BUILD_DIR/libxbox_core.a" $OBJ_DIR/*.o

    echo "[4/4] Linking (fully static)..."
    $CXX "${CXXFLAGS[@]}" $STATIC_FLAGS src/main.cpp -o "$BUILD_DIR/xbox-install" \
        -L "$BUILD_DIR" -lxbox_core $PLATFORM_LIBS $ZIP_LIBS
    strip "$BUILD_DIR/xbox-install" 2>/dev/null || true

    # Verify it's fully static
    if ldd "$BUILD_DIR/xbox-install" 2>&1 | grep -q "not a dynamic executable"; then
        echo "✅ Linux (fully static): $BUILD_DIR/xbox-install"
        echo "   Runs on ANY Linux x86_64 system (no library dependencies)"
    else
        echo "⚠️  Linux binary still has dynamic dependencies:"
        ldd "$BUILD_DIR/xbox-install" 2>&1 | head -5
    fi
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
