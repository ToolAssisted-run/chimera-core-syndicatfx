#!/bin/bash
# Rootless i386 toolchain for building the game as a static i386 ELF (the translator's input).
# The host gcc does the compiling (-m32); this script only supplies what a multilib install would:
# the 32-bit libgcc (unpacked from Ubuntu's lib32gcc-*-dev) and a static i386 musl libc.
# Output: build/i386tc/{gcc-lib,musl}. Idempotent.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TC=$ROOT/build/i386tc
MUSL_VER=1.2.5
MUSL_SHA256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
mkdir -p "$TC/src"
cd "$TC/src"

GCCV=$(gcc -dumpversion | cut -d. -f1)
if [ ! -f "$TC/gcc-lib/libgcc.a" ]; then
  if [ -f "/usr/lib/gcc/x86_64-linux-gnu/$GCCV/32/libgcc.a" ]; then
    mkdir -p "$TC/gcc-lib" && cp "/usr/lib/gcc/x86_64-linux-gnu/$GCCV/32/libgcc.a" "$TC/gcc-lib/"
  else
    apt-get download "lib32gcc-$GCCV-dev" >/dev/null
    rm -rf deb && mkdir deb && dpkg-deb -x lib32gcc-$GCCV-dev_*.deb deb
    mkdir -p "$TC/gcc-lib" && cp deb/usr/lib/gcc/x86_64-linux-gnu/$GCCV/32/libgcc.a "$TC/gcc-lib/"
  fi
fi

if [ ! -f "$TC/musl/lib/libc.a" ]; then
  [ -f musl-$MUSL_VER.tar.gz ] || curl -sSL -o musl-$MUSL_VER.tar.gz https://musl.libc.org/releases/musl-$MUSL_VER.tar.gz
  echo "$MUSL_SHA256  musl-$MUSL_VER.tar.gz" | sha256sum -c --quiet
  rm -rf musl-$MUSL_VER && tar xzf musl-$MUSL_VER.tar.gz
  cd musl-$MUSL_VER
  # plain i686 integer code: no SSE/MMX, no PIC (the translator's input must be fully relocated)
  CC="gcc -m32 -march=i686 -mno-sse -mno-mmx -fno-pic -fno-pie" CFLAGS="-O2 -fno-stack-protector" \
    ./configure --target=i386 --prefix="$TC/musl" --disable-shared > cfg.log 2>&1
  make -j"$(nproc)" AR=ar RANLIB=ranlib > make.log 2>&1
  make install AR=ar RANLIB=ranlib > install.log 2>&1
fi
# headers only: SDL2 (the shim implements the API) and OpenAL (bfsoundlib compiles against it; stubbed)
cd "$TC/src"
fetch() { [ -f "$2" ] || curl -sSL -o "$2" "$1"; echo "$3  $2" | sha256sum -c --quiet; }
if [ ! -d SDL2-2.30.12/include ]; then
  fetch https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz SDL2-2.30.12.tar.gz \
    ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea
  tar xzf SDL2-2.30.12.tar.gz SDL2-2.30.12/include
fi
if [ ! -d openal-soft-1.23.1/include ]; then
  fetch https://github.com/kcat/openal-soft/archive/refs/tags/1.23.1.tar.gz openal-soft-1.23.1.tar.gz \
    dfddf3a1f61059853c625b7bb03de8433b455f2f79f89548cbcbd5edca3d4a4a
  tar xzf openal-soft-1.23.1.tar.gz openal-soft-1.23.1/include
fi
echo "i386 toolchain ready in $TC"
