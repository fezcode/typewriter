#!/bin/sh
# Builds static SDL2 and SDL2_ttf (with vendored FreeType, no HarfBuzz) into
# deps/prefix so that `make STATIC=1` can produce a self-contained executable on
# Linux and macOS. Windows uses the MSYS2 static libraries instead.
#
# Usage:  sh scripts/build-deps.sh          (or: make deps)
#
# Environment overrides:
#   SDL2_VERSION       default 2.32.10
#   SDL2_TTF_VERSION   default 2.24.0
#   MACOS_ARCHS        default "arm64;x86_64" (universal binary), macOS only
#   MACOSX_DEPLOYMENT_TARGET  default 11.0, macOS only
#   JOBS               parallel build jobs, default = CPU count
#
# Requires: cmake, a C/C++ compiler, curl (or wget), tar.
# On Linux the SDL2 video/audio backends are enabled from the -dev packages
# present at build time and loaded dynamically at runtime, so install e.g.
#   libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
#   libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev
#   libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev libpipewire-0.3-dev
#   libdbus-1-dev libudev-dev libdrm-dev libgbm-dev libibus-1.0-dev
# before running this script.
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)
DEPS="$ROOT/deps"
PREFIX="$DEPS/prefix"

SDL2_VERSION=${SDL2_VERSION:-2.32.10}
SDL2_TTF_VERSION=${SDL2_TTF_VERSION:-2.24.0}
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}

for tool in cmake tar; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool is required" >&2; exit 1; }
done

download() { # url dest
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -q "$1" -O "$2"
  else
    echo "error: curl or wget is required" >&2; exit 1
  fi
}

# Platform-specific CMake settings
EXTRA_CMAKE=""
ARCHFLAGS=""
case "$(uname -s)" in
  Darwin)
    MACOS_ARCHS=${MACOS_ARCHS:-"arm64;x86_64"}
    MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
    EXTRA_CMAKE="-DCMAKE_OSX_ARCHITECTURES=$MACOS_ARCHS -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET"
    for a in $(echo "$MACOS_ARCHS" | tr ';' ' '); do ARCHFLAGS="$ARCHFLAGS -arch $a"; done
    ARCHFLAGS="$ARCHFLAGS -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
    ;;
esac

mkdir -p "$DEPS" "$PREFIX"

fetch() { # name version url
  dir="$DEPS/$1-$2"
  if [ ! -d "$dir" ]; then
    echo "==> Downloading $1 $2"
    download "$3" "$DEPS/$1-$2.tar.gz"
    tar -xzf "$DEPS/$1-$2.tar.gz" -C "$DEPS"
    rm -f "$DEPS/$1-$2.tar.gz"
  fi
}

fetch SDL2     "$SDL2_VERSION"     "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz"
fetch SDL2_ttf "$SDL2_TTF_VERSION" "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$SDL2_TTF_VERSION/SDL2_ttf-$SDL2_TTF_VERSION.tar.gz"

echo "==> Building SDL2 $SDL2_VERSION (static)"
cmake -S "$DEPS/SDL2-$SDL2_VERSION" -B "$DEPS/build-sdl2" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON \
  -DSDL_TEST=OFF \
  $EXTRA_CMAKE
cmake --build "$DEPS/build-sdl2" --parallel "$JOBS"
cmake --install "$DEPS/build-sdl2"

echo "==> Building SDL2_ttf $SDL2_TTF_VERSION (static, vendored FreeType)"
cmake -S "$DEPS/SDL2_ttf-$SDL2_TTF_VERSION" -B "$DEPS/build-sdl2_ttf" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF \
  -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_INSTALL=ON \
  $EXTRA_CMAKE
cmake --build "$DEPS/build-sdl2_ttf" --parallel "$JOBS"
cmake --install "$DEPS/build-sdl2_ttf"

# The vendored FreeType is normally installed alongside SDL2_ttf; make sure of it.
if [ ! -f "$PREFIX/lib/libfreetype.a" ]; then
  ft=$(find "$DEPS/build-sdl2_ttf" -name 'libfreetype*.a' | head -n 1)
  [ -n "$ft" ] || { echo "error: static FreeType not found" >&2; exit 1; }
  cp "$ft" "$PREFIX/lib/libfreetype.a"
fi

# Makefile fragment consumed by `make STATIC=1`
printf 'DEPS_ARCHFLAGS = %s\n' "$ARCHFLAGS" > "$PREFIX/deps.mk"

echo "==> Done. Static libraries installed in $PREFIX"
echo "    Now run:  make STATIC=1"
