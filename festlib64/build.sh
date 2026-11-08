#!/bin/sh
# Cross-build festlib.dll (x64) on Linux, or build on Windows in an MSYS2/Git-Bash
# shell, with llvm-mingw (https://github.com/mstorsjo/llvm-mingw), meson, cmake, ninja.
#
# Expected layout (ROOT = parent of this directory):
#   ROOT/festival      Festival 2.5.1 sources (your tree)
#   ROOT/speech_tools  git clone https://github.com/festvox/speech_tools (commit 63ff019)
#   ROOT/festlib64     this directory
#
# usage: ./build.sh /path/to/llvm-mingw
set -e
LLVM_MINGW=${1:?usage: build.sh /path/to/llvm-mingw}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
BIN=$LLVM_MINGW/bin
MESON=${MESON:-meson}

# 1. patches (skipped when already applied)
cd "$ROOT/speech_tools"
if [ -f siod/io.h ]; then git apply "$HERE/patches_speech_tools-2.5.1_win64.diff"; fi
cd "$ROOT/festival"
if ! grep -q FESTIVAL_NULL_DEVICE src/arch/festival/festival.cc; then
  patch -p1 < "$HERE/patches_festival-2.5.1_win64.diff"
fi
cp "$HERE/festival-cmake/CMakeLists.txt" "$ROOT/festival/CMakeLists.txt"

# 2. toolchain files
cat > "$HERE/win64.ini" <<EOT
[binaries]
c = '$BIN/x86_64-w64-mingw32-clang'
cpp = '$BIN/x86_64-w64-mingw32-clang++'
ar = '$BIN/llvm-ar'
strip = '$BIN/llvm-strip'
windres = '$BIN/x86_64-w64-mingw32-windres'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOT
cat > "$HERE/win64-toolchain.cmake" <<EOT
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $BIN/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER $BIN/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER $BIN/x86_64-w64-mingw32-windres)
set(CMAKE_AR $BIN/llvm-ar)
set(CMAKE_RANLIB $BIN/llvm-ranlib)
EOT

# 3. speech tools (static libraries)
cd "$ROOT/speech_tools"
[ -d build-win64 ] || $MESON setup build-win64 --cross-file "$HERE/win64.ini" \
  --buildtype=release -Ddefault_library=static -Dcpp_std=gnu++11 -Dc_std=gnu99 \
  "-Dcpp_args=-DSYSTEM_IS_WIN32 -w" "-Dc_args=-DSYSTEM_IS_WIN32 -w" \
  -Deditline=disabled -Dsiod_python=disabled -Dopenmp=disabled \
  -Daudio_alsa=disabled -Daudio_pulseaudio=disabled -Daudio_esd=disabled -Daudio_nas=disabled
ninja -C build-win64 libestools.a libestbase.a libeststring.a

# 4. Festival + festlib.dll
cmake -S "$HERE" -B "$HERE/build-win64" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HERE/win64-toolchain.cmake"
ninja -C "$HERE/build-win64"
# 5. helper process for the NVDA driver
$BIN/x86_64-w64-mingw32-clang++ -std=c++17 -O2 -municode -mwindows -static -s \
  -Wl,--no-insert-timestamp -o "$HERE/build-win64/festhost64.exe" "$HERE/host/festhost64.cpp"
echo "built: $HERE/build-win64/festlib.dll and festhost64.exe"
