#!/bin/sh
# Builds dist/scripts/ARO.asi with the mingw-w64 cross compiler (pacman -S mingw-w64-gcc).
set -e
cd "$(dirname "$0")"
CXX=i686-w64-mingw32-g++; CC=i686-w64-mingw32-gcc
IM=third_party/imgui; MZ=third_party/miniz
mkdir -p build dist/scripts
CXXFLAGS="-O2 -std=c++17 -Wall -Wno-unused-function -fno-strict-aliasing -Isrc -I$IM -I$IM/backends -I$MZ"
for f in imgui imgui_draw imgui_tables imgui_widgets backends/imgui_impl_win32 backends/imgui_impl_dx11; do
  o=build/$(basename $f).o; [ $o -nt $IM/$f.cpp ] || $CXX $CXXFLAGS -w -c $IM/$f.cpp -o $o; done
[ build/miniz_tinfl.o -nt $MZ/miniz_tinfl.c ] || $CC -O2 -I$MZ -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_APIS -c $MZ/miniz_tinfl.c -o build/miniz_tinfl.o
for f in main core cars races opponents menu lang probe; do $CXX $CXXFLAGS -c src/$f.cpp -o build/$f.o; done
$CXX -shared -s -static -static-libgcc -static-libstdc++ -o dist/scripts/ARO.asi \
  build/main.o build/core.o build/cars.o build/races.o build/opponents.o build/menu.o build/lang.o build/probe.o \
  build/imgui.o build/imgui_draw.o build/imgui_tables.o build/imgui_widgets.o build/imgui_impl_win32.o build/imgui_impl_dx11.o build/miniz_tinfl.o \
  -ld3d11 -ldxgi -ldxguid -ld3dcompiler_47 -ldwmapi -lgdi32 -limm32 -lws2_32 -luser32 -lkernel32
echo "built dist/scripts/ARO.asi"
