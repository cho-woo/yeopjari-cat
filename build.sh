#!/usr/bin/env bash
set -euo pipefail
DESKCAT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DESKCAT_TOOLCHAIN="${DESKCAT_TOOLCHAIN:-$DESKCAT_ROOT/toolchain/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64}"
DESKCAT_COMPILER="$DESKCAT_TOOLCHAIN/bin/x86_64-w64-mingw32-clang++"
DESKCAT_C_COMPILER="$DESKCAT_TOOLCHAIN/bin/x86_64-w64-mingw32-clang"
DESKCAT_ARCHIVER="$DESKCAT_TOOLCHAIN/bin/llvm-ar"
DESKCAT_RC="$DESKCAT_TOOLCHAIN/bin/x86_64-w64-mingw32-windres"
DESKCAT_BOX2D="$DESKCAT_ROOT/third_party/box2d"
DESKCAT_BOX2D_BUILD="$DESKCAT_ROOT/build/box2d"
mkdir -p "$DESKCAT_ROOT/build" "$DESKCAT_ROOT/release" "$DESKCAT_BOX2D_BUILD"
# Build the pinned upstream C17 engine only. Equivalent to BUILD_SHARED_LIBS=OFF,
# BOX2D_AVX2=OFF, BOX2D_SAMPLES=OFF and BOX2D_UNIT_TESTS=OFF; no fetched packages.
# Match upstream's deterministic floating-point setting. SSE2 is x64's baseline.
DESKCAT_BOX2D_OBJECTS=()
for DESKCAT_SOURCE in "$DESKCAT_BOX2D"/src/*.c; do
  DESKCAT_OBJECT="$DESKCAT_BOX2D_BUILD/$(basename -- "${DESKCAT_SOURCE%.c}").o"
  "$DESKCAT_C_COMPILER" -std=c17 -O2 -DNDEBUG -ffp-contract=off -mno-avx -mno-avx2 \
    -I"$DESKCAT_BOX2D/include" -I"$DESKCAT_BOX2D/src" \
    -c "$DESKCAT_SOURCE" -o "$DESKCAT_OBJECT"
  DESKCAT_BOX2D_OBJECTS+=("$DESKCAT_OBJECT")
done
"$DESKCAT_ARCHIVER" rcs "$DESKCAT_BOX2D_BUILD/libbox2d.a" "${DESKCAT_BOX2D_OBJECTS[@]}"
cd "$DESKCAT_ROOT/src"
# LLVM windres must receive the source encoding explicitly: the RC pragma alone
# does not select UTF-8 in this toolchain's preprocessing/resource pipeline.
"$DESKCAT_RC" --codepage=65001 resources.rc -O coff -o "$DESKCAT_ROOT/build/resources.o"
"$DESKCAT_COMPILER" -std=c++17 -O2 -static -municode -mwindows -Wall -Wextra \
  -I"$DESKCAT_BOX2D/include" main.cpp cat_physics.cpp "$DESKCAT_ROOT/build/resources.o" \
  "$DESKCAT_BOX2D_BUILD/libbox2d.a" \
  -lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -lcomdlg32 -lshlwapi -lwinhttp -lbcrypt -lversion -lwinmm -ldwmapi \
  -o "$DESKCAT_ROOT/release/YeopjariCat.exe"
python3 "$DESKCAT_ROOT/tools/verify_resources.py" "$DESKCAT_ROOT/release/YeopjariCat.exe"
printf '%s\n' "Built: $DESKCAT_ROOT/release/YeopjariCat.exe"
