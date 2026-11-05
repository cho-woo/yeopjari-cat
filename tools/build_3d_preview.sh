#!/usr/bin/env bash
# Separate opt-in binary/package. Does not touch Release files or update feeds.
set -euo pipefail
DESKCAT_PREVIEW_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DESKCAT_PREVIEW_ROOT"
DESKCAT_PREVIEW_MODEL="${1:-assets/3d/cream-kitten/cat3d.bin}"
if [[ ! -f "$DESKCAT_PREVIEW_MODEL" ]]; then
  printf 'Missing 3D preview model: %s\n' "$DESKCAT_PREVIEW_MODEL" >&2
  exit 1
fi
DESKCAT_EXPERIMENTAL_3D=1 bash build.sh
python3 tools/verify_silent_exe.py build/3d-preview/YeopjariCat-3D-Preview.exe
python3 - "$DESKCAT_PREVIEW_MODEL" <<'PY'
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

base = Path('build/3d-preview')
package = base / 'package'
package.mkdir(exist_ok=True)
shutil.copy2(base / 'YeopjariCat-3D-Preview.exe', package)
shutil.copy2(sys.argv[1], package / 'cat3d.bin')
shutil.copy2('THIRD_PARTY_NOTICES.txt', package)
(package / '3D-preview.cmd').write_bytes(
    b'@echo off\r\ncd /d "%~dp0"\r\n'
    b'start "" "%~dp0YeopjariCat-3D-Preview.exe" --3d-preview --3d-model "%~dp0cat3d.bin"\r\n')
(package / 'README_KO.txt').write_text(
    '옆자리 고양이 3D 시험판 — Windows 10/11 x64\n\n'
    '압축을 풀고 3D-preview.cmd를 실행하세요. 크림 치즈냥의 일부 동작을 3D로 표시합니다.\n'
    '현재 시험 동작: 기본 자세, 걷기, 달리기, 살금살금, 배 까기, 공중 자세, 착지, 귀, 앞발.\n'
    '그 외 캐릭터와 동작은 기존 2D 표시를 사용합니다. 동작은 자동으로 발생합니다.\n'
    '밥과 츄르 등은 우클릭 메뉴에서 사용합니다. 시험판은 전역 단축키를 등록하지 않습니다.\n\n'
    '기록 폴더: %LOCALAPPDATA%\\YeopjariCat-3D-Preview\n'
    '정식 프로그램의 기록 폴더와 별개이며 정식 업데이트를 확인하거나 설치하지 않습니다.\n'
    '3D 모델 외형, 모든 행동 전환, 성장·체형 표현과 하드웨어별 성능의 완성판이 아닙니다.\n\n'
    '3D 초기화 실패 시 exe와 cat3d.bin이 같은 폴더에 있는지 확인하세요.\n'
    '하드웨어 초기화가 실패하면 소프트웨어 WARP를 시도합니다.\n'
    '강제 WARP: YeopjariCat-3D-Preview.exe --3d-preview --3d-warp\n', encoding='utf-8')
names = ['YeopjariCat-3D-Preview.exe', 'cat3d.bin', '3D-preview.cmd',
         'README_KO.txt', 'THIRD_PARTY_NOTICES.txt']
manifest = {'kind': 'experimental-3d-preview', 'platform': 'Windows-10-11-x64',
            'official_release': False, 'files': {}}
for name in names:
    data = (package / name).read_bytes()
    manifest['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
(package / 'preview-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
archive = base / 'YeopjariCat-3D-Preview.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for name in [*names, 'preview-manifest.json']:
        z.write(package / name, name)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert set(z.namelist()) == {*names, 'preview-manifest.json'}
    for name, expected in manifest['files'].items():
        data = z.read(name)
        assert len(data) == expected['bytes']
        assert hashlib.sha256(data).hexdigest() == expected['sha256']
print('Verified experimental ZIP:', archive.resolve())
PY
