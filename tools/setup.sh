#!/usr/bin/env bash
# Linux x86_64 host; run from any directory. No application sources are rewritten.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
for tool in git curl tar xz python3 gcc g++ ar; do command -v "$tool" >/dev/null; done
test "$(uname -m)" = x86_64
mkdir -p .cache toolchain third_party release
archive=.cache/llvm-mingw-20261020.tar.xz
if [ ! -f "$archive" ]; then
 if ! curl -fL --retry 3 -o "$archive.part" https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz; then
  # The release redirect can cache a gateway failure. The immutable GitHub
  # asset API serves the same pinned bytes; the mandatory hash below applies
  # to either transport. No compiler version or trust check changes here.
  curl -fL --retry 3 -H 'Accept: application/octet-stream' -o "$archive.part" https://api.github.com/repos/mstorsjo/llvm-mingw/releases/assets/661639472
 fi
 printf '%s\n' "55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335  $archive.part" | sha256sum -c -
 mv "$archive.part" "$archive"
fi
printf '%s\n' "55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335  $archive" | sha256sum -c -
if [ ! -x toolchain/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang++ ]; then
 tar -xJf "$archive" -C toolchain
fi
if [ ! -d third_party/box2d ]; then
 git clone https://github.com/erincatto/box2d.git third_party/box2d
fi
test -z "$(git -C third_party/box2d status --porcelain)" || { echo 'Box2D has local changes; refusing to overwrite.' >&2; exit 1; }
git -C third_party/box2d checkout --detach 8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3
test "$(git -C third_party/box2d rev-parse HEAD)" = 8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3
cp THIRD_PARTY_NOTICES.txt release/THIRD_PARTY_NOTICES.txt
