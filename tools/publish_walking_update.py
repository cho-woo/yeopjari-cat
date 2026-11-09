#!/usr/bin/env python3
"""Publish the reviewed 2.0.16 bytes and downloadable, offline preview media."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

REPO = 'cho-woo/yeopjari-cat'
TAG = 'v2.0.16'
SOURCE = '8ae8734ab081d27eb9d5cec51fe082310e46df47'
RUN = 42559903373
EXE = 'YeopjariCat-2.0.16.exe'
EXE_SHA = 'e4b8151e8de3d6a1f393d8b985ed86aa50176e796141166be84ad44ca3ca709a'


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_preview(output):
    output.mkdir(parents=True, exist_ok=True)
    for name in ('walking-native.mp4', 'walking-native.gif'):
        shutil.copyfile(Path('docs/media') / name, output / name)
    with zipfile.ZipFile(output / 'walking-preview.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        def add(name, data):
            info = zipfile.ZipInfo(name, (2026, 11, 9, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
        html = Path('docs/walking-preview.html').read_text().replace(
            'href="WALKING_UPDATE.md"',
            'href="https://github.com/cho-woo/yeopjari-cat/blob/main/docs/WALKING_UPDATE.md"')
        add('index.html', html)
        add('README_KO.txt', '압축을 모두 풀고 index.html을 브라우저에서 여세요.\n'
                         '또는 media/walking-native.mp4를 동영상 플레이어로 여세요.\n'
                         '위: 이전 보행 로직 재현 / 아래: 개선된 보행. 실제 바탕화면 녹화는 아닙니다.\n')
        for name in ('walking-native.mp4', 'walking-native.gif', 'walking-dpi-100.png',
                     'walking-dpi-125.png', 'walking-dpi-150.png', 'walking-native.json'):
            add('media/' + name, (Path('docs/media') / name).read_bytes())
    files = sorted(output.iterdir())
    (output / 'preview-SHA256SUMS.txt').write_text(''.join(sha(p) + '  ' + p.name + '\n' for p in files))


def main():
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert os.environ.get('GITHUB_REPOSITORY') == REPO
    assert os.environ.get('GITHUB_REF') == 'refs/heads/main'
    run = json.loads(gh('api', f'repos/{REPO}/actions/runs/{RUN}'))
    assert run['conclusion'] == 'success' and run['head_sha'] == SOURCE
    subprocess.run(['git', 'diff', '--exit-code', SOURCE, 'HEAD', '--', 'src', 'assets',
                    'tests', 'build.sh', 'tools/setup.sh', 'tools/test.sh', 'tools/check.sh'], check=True)
    draft = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                          '--json', 'isDraft,isPrerelease,targetCommitish'))
    assert draft['isDraft'] and not draft['isPrerelease'] and draft['targetCommitish'] == SOURCE
    with tempfile.TemporaryDirectory() as directory:
        original = Path(directory) / 'original'
        original.mkdir()
        gh('release', 'download', TAG, '--repo', REPO, '--dir', str(original))
        original_hashes = {p.name: sha(p) for p in original.iterdir()}
        for sums in ('SHA256SUMS.txt', 'review-SHA256SUMS.txt'):
            for line in (original / sums).read_text().splitlines():
                digest, name = line.split('  ', 1)
                assert original_hashes[name] == digest, name
        assert original_hashes[EXE] == EXE_SHA
        provenance = json.loads((original / 'windows-validation.json').read_text())
        assert provenance['source_commit'] == SOURCE and provenance['validation_run'] == RUN
        assert provenance['native_self_test'] == provenance['native_gdi_render_test'] == 'passed'
        with zipfile.ZipFile(original / 'YeopjariCat-Windows-2.0.16.zip') as archive:
            assert hashlib.sha256(archive.read('YeopjariCat.exe')).hexdigest() == EXE_SHA
        media_proof = json.loads(Path('docs/media/walking-native.json').read_text())
        assert media_proof['source_archive_sha256'] == original_hashes['native-walking-evidence.zip']
        assert media_proof['duration_seconds'] == 10.5 and media_proof['frames'] == 210
        media = Path(directory) / 'media'
        prepare_preview(media)
        for file in media.iterdir():
            if file.name in original_hashes:
                assert original_hashes[file.name] == sha(file), file.name
            else:
                gh('release', 'upload', TAG, '--repo', REPO, str(file))
        expected = original_hashes | {p.name: sha(p) for p in media.iterdir()}
        verified = Path(directory) / 'verified'
        verified.mkdir()
        gh('release', 'download', TAG, '--repo', REPO, '--dir', str(verified))
        assert {p.name: sha(p) for p in verified.iterdir()} == expected
        base = f'https://github.com/{REPO}/releases/download/{TAG}/'
        notes = Path(directory) / 'release-notes.md'
        notes.write_text((original / 'RELEASE_NOTES_KO.txt').read_text() + '\n'
                         '고양이 우클릭 → 프로그램 업데이트 → 업데이트 확인…에서 설치할 수 있습니다.\n\n'
                         f'- [Windows 사용자 ZIP]({base}YeopjariCat-Windows-2.0.16.zip)\n'
                         f'- [보행 비교 MP4 다운로드]({base}walking-native.mp4)\n'
                         f'- [GIF 보기]({base}walking-native.gif)\n'
                         f'- [오프라인 미리보기 ZIP]({base}walking-preview.zip) — 압축을 풀고 index.html을 여세요.\n\n'
                         '영상은 Windows GDI+/Box2D 진단 출력입니다. 위쪽은 이전 보행 로직 재현, 아래쪽은 수정 코드입니다.\n')
        gh('release', 'edit', TAG, '--repo', REPO, '--notes-file', str(notes))
        # The user explicitly requested making the tested update downloadable.
        gh('release', 'edit', TAG, '--repo', REPO, '--draft=false', '--prerelease=false', '--latest')
        latest = json.loads(gh('api', f'repos/{REPO}/releases/latest'))
        assert latest['tag_name'] == TAG and not latest['draft']
        print(json.dumps({'tag': TAG, 'source_commit': SOURCE, 'asset_sha256': expected}, indent=2))


if __name__ == '__main__':
    main()
