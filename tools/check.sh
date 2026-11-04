#!/usr/bin/env bash
# Runs validation and prepares local packages; never publishes a release.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
bash tools/test.sh
bash build.sh
cp THIRD_PARTY_NOTICES.txt release/THIRD_PARTY_NOTICES.txt
version="$(python3 -c 'import sys; sys.path.insert(0,"tools"); import release; print(release.version_from_header(release.ROOT/"src/app_version.h"))')"
# Each run gets its own directory; release.py intentionally refuses overwrites.
mkdir -p release/publish
output="$(mktemp -d "release/publish/check-${version}-XXXXXX")/package"
python3 tools/release.py --exe release/YeopjariCat.exe --version "$version" --github cho-woo/yeopjari-cat --notes-file RELEASE_NOTES_KO.txt --output "$output"
python3 tools/release.py --verify "$output"
printf 'Verified local package: %s/%s\n' "$ROOT" "$output"
