#!/usr/bin/env python3
"""Publish exact Windows-validated v2.0.19 review bytes and editable sources.

CI-only. Native rendering/numerical evidence is not visual production approval.
The existing stable release and its updater feed remain unchanged.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

import release
from verify_silent_exe import verify_silent_exe

REPO = 'cho-woo/yeopjari-cat'
VERSION = '2.0.19'
TAG = 'v' + VERSION
STABLE = 'v2.0.16'
ROOT = Path(__file__).resolve().parents[1]


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def api(path):
    return json.loads(gh('api', f'repos/{REPO}/' + path))


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(4 * 1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def exactly_one(paths, description):
    matches = list(paths)
    require(len(matches) == 1, f'Expected exactly one {description}; found {len(matches)}')
    return matches[0]


def archive_files(output, paths):
    """Keep source-relative paths and deterministic metadata; omit no art bytes."""
    seen = set()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, name in sorted(paths, key=lambda item: item[1]):
            require(path.is_file() and not path.is_symlink(), f'Invalid archive input: {path}')
            require(name not in seen and not name.startswith('/') and '..' not in Path(name).parts,
                    f'Unsafe or duplicate archive name: {name}')
            seen.add(name)
            info = zipfile.ZipInfo(name, (2026, 11, 9, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            with path.open('rb') as source, archive.open(info, 'w', force_zip64=True) as destination:
                shutil.copyfileobj(source, destination, 4 * 1024 * 1024)


def editable_sources(output):
    required = [
        'assets/cream-mesh-source/authoring.blend',
        'assets/cream-mesh-source/original-walk-run.png',
        'assets/cream-mesh-source/hidden-parts-generated.png',
        'assets/cream-mesh-source/source-provenance.json',
        'tools/cream_mesh_rig/author.py', 'tools/cream_mesh_rig/motion.py',
        'tools/cream_mesh_rig/review.py',
        'assets/review/cream-mesh-walk.png',
        'assets/review/cream-mesh-walk.manifest.json',
        'assets/review/cream-mesh-walk.contacts.json',
    ]
    for name in required:
        path = ROOT / name
        require(path.is_file() and path.stat().st_size > 0, f'Missing actual authored source: {name}')
    blend = ROOT / required[0]
    with blend.open('rb') as source:
        magic = source.read(8)
    require(blend.stat().st_size > 4096 and
            (magic.startswith(b'BLENDER') or magic.startswith(b'\x1f\x8b') or
             magic.startswith(b'\x28\xb5\x2f\xfd')), 'authoring.blend is not a Blender source container')
    trees = ['assets/cream-mesh-source', 'tools/cream_mesh_rig', 'assets/review', 'docs/new-rig']
    files = []
    for tree in trees:
        folder = ROOT / tree
        require(folder.is_dir(), f'Missing editable-source directory: {tree}')
        for path in folder.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix not in {'.pyc', '.blend1'}:
                files.append((path, str(path.relative_to(ROOT))))
    for name in ('README.md', 'RELEASE_NOTES_KO.txt', 'THIRD_PARTY_NOTICES.txt',
                 'tools/calibrate_walk_contacts.py', 'tools/import_care_sprites.py', 'tools/import_mesh_walk_review.py',
                 'assets/care-sprites/manifest.json', 'assets/care-sprites/c0-walk.png',
                 'tools/encode_native_mesh_walk_review.py'):
        require((ROOT / name).is_file(), f'Missing source instructions/tool: {name}')
        files.append((ROOT / name, name))
    archive_files(output, files)
    return {name: sha(path) for path, name in files}


def main():
    require(os.environ.get('GITHUB_ACTIONS') == 'true', 'This publisher runs only in GitHub Actions')
    require(os.environ.get('GITHUB_REPOSITORY') == REPO, 'Wrong repository')
    require(os.environ.get('GITHUB_REF') == 'refs/heads/main', 'Dispatch from main only')
    os.chdir(ROOT)
    run_id = os.environ.get('VALIDATION_RUN', '')
    require(run_id.isdigit(), 'VALIDATION_RUN must identify the successful validation')
    run = api('actions/runs/' + run_id)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    require(run.get('path') == '.github/workflows/validate.yml' and
            run.get('status') == 'completed' and run.get('conclusion') == 'success',
            'The Validate workflow must have succeeded')
    require(run['head_repository']['full_name'] == REPO and run['head_branch'] == 'main' and
            run['head_sha'] == head,
            'Checkout must be exactly the validated repository commit')
    jobs = api('actions/runs/' + run_id + '/jobs')['jobs']
    for name in ('validate', 'windows-smoke'):
        require(any(job['name'] == name and job['conclusion'] == 'success' for job in jobs),
                f'Missing successful {name} job')
    require(release.version_from_header(ROOT / 'src/app_version.h') == VERSION, 'Wrong source version')
    stable_before = api('releases/latest')['tag_name']
    require(stable_before == STABLE, 'Stable latest changed; review publication must be reassessed')

    metadata = exactly_one(Path('verified-package').rglob('release-info.json'), 'package metadata')
    package = metadata.parent
    info = release.verify_release(package)
    require(info['version'] == VERSION and info['github_repository'] == REPO, 'Wrong verified package')
    exe = package / f'YeopjariCat-{VERSION}.exe'
    verify_silent_exe(exe.read_bytes())
    native_root = Path('native-review')
    report = exactly_one(native_root.rglob('render-self-test.txt'), 'native render report')
    require(report.read_text(encoding='utf-8-sig').startswith('exit=0 '), 'Native rendering failed')
    mesh_report = exactly_one((p for p in native_root.rglob('review.json') if p.parent.name == 'mesh-walk'),
                              'real mesh-walk/review.json')
    native_contract = json.loads(mesh_report.read_text())
    require(str(native_contract.get('render', '')).startswith('actual GDI+ DrawCareSprite and Box2D'),
            'Mesh evidence must come from the executable rendering path')

    history = json.loads(gh('release', 'list', '--repo', REPO, '--limit', '1000',
                           '--json', 'tagName,isDraft,isPrerelease'))
    require(len(history) < 1000, 'Release history exceeds bounded inspection')
    existing = [item for item in history if item['tagName'] == TAG]
    require(len(existing) <= 1 and all(item['isDraft'] for item in existing),
            'An existing public release will never be overwritten')
    if existing:
        remote = json.loads(gh('release', 'view', TAG, '--repo', REPO, '--json', 'targetCommitish'))
        require(remote['targetCommitish'] == head, 'Existing draft belongs to a different commit')
    tag_refs = [ref for ref in api('git/matching-refs/tags/' + TAG) if ref['ref'] == 'refs/tags/' + TAG]
    require(len(tag_refs) <= 1, 'Ambiguous existing release tag')
    if tag_refs:
        require(api('commits/' + TAG)['sha'] == head, 'Existing tag differs from the validated commit')

    with tempfile.TemporaryDirectory(prefix='mesh-release-') as directory:
        stage = Path(directory)
        encoded = stage / 'encoded'
        subprocess.run([sys.executable, str(ROOT / 'tools/encode_native_mesh_walk_review.py'),
                        str(mesh_report.parent), '--output', str(encoded)], check=True)
        files = [package / name for name in info['files']]
        # A review launcher selects the opt-in art explicitly. Double-clicking
        # the ordinary executable intentionally retains the approved old walk.
        review_zip = stage / f'YeopjariCat-Mesh-Review-{VERSION}.zip'
        with zipfile.ZipFile(package / f'YeopjariCat-Windows-{VERSION}.zip') as original, \
                zipfile.ZipFile(review_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name in original.namelist():
                archive.writestr(name, original.read(name))
            archive.writestr('Launch-Mesh-Walk-Review.cmd',
                '@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0YeopjariCat.exe" --mesh-walk-review\r\n')
            archive.writestr('READ-MESH-REVIEW.txt',
                'UNAPPROVED visual study, not a completed natural gait.\r\n'
                'Quit the existing cat using its tray menu first.\r\n'
                'Double-click Launch-Mesh-Walk-Review.cmd for the opt-in mesh study.\r\n'
                'Ordinary YeopjariCat.exe keeps the approved original eight drawings.\r\n'
                'Use YeopjariCat.exe --render-mesh-walk-review to output native GDI+ diagnostic PNGs.\r\n'
                'See docs/new-rig/README.md in cream-mesh-editable-source.zip for defects and editing instructions.\r\n')
        files.append(review_zip)
        files.extend([encoded / 'native-mesh-walk-22.mp4', encoded / 'native-mesh-walk-contact-3x-slow.mp4'])
        evidence = stage / 'native-mesh-walk-evidence.zip'
        archive_files(evidence, [(p, str(p.relative_to(native_root))) for p in native_root.rglob('*') if p.is_file()])
        files.append(evidence)
        source_zip = stage / 'cream-mesh-editable-source.zip'
        source_hashes = editable_sources(source_zip)
        files.append(source_zip)
        encoding = stage / 'mesh-walk-encoding.json'
        shutil.copyfile(encoded / 'encoding.json', encoding)
        files.append(encoding)
        native_report = stage / 'render-self-test.txt'
        shutil.copyfile(report, native_report)
        files.append(native_report)
        proof = stage / 'mesh-walk-validation.json'
        proof.write_text(json.dumps({
            'version': VERSION, 'source_commit': head, 'validation_run': int(run_id),
            'validation_url': f'https://github.com/{REPO}/actions/runs/{run_id}',
            'native_self_test': 'passed', 'native_gdi_render_test': 'passed',
            'native_mesh_review': native_contract, 'exe_sha256': sha(exe),
            'desktop_recording': False, 'production_approved': False,
            'visual_quality_status': 'unapproved review, separate from successful numerical/native checks',
            'editable_source_sha256': source_hashes,
        }, indent=2) + '\n')
        files.append(proof)
        sums = stage / 'mesh-review-SHA256SUMS.txt'
        sums.write_text(''.join(sha(p) + '  ' + p.name + '\n' for p in sorted(files, key=lambda p: p.name)))
        files.append(sums)
        require(len({p.name for p in files}) == len(files), 'Duplicate release asset names')
        expected = {p.name: sha(p) for p in files}
        base = f'https://github.com/{REPO}/releases/download/{TAG}/'
        notes = stage / 'notes.md'
        notes.write_text(f'''# v{VERSION} 편집 가능한 메시 보행 검토본 — 시각 승인 전

실제 Windows GDI+와 Box2D 경로에서 출력한 검토 자료입니다. 수치 검사·실행 검사가 통과해도 자연스러운 외형·털 이음새까지 승인됐다는 뜻은 아닙니다. **production_approved=false**이며 정식 자동 업데이트는 {STABLE}을 유지합니다.

- [검토 모드 실행용 Windows ZIP]({base}YeopjariCat-Mesh-Review-{VERSION}.zip)
- [실제 크기·22논리px/s·최소3주기 비교]({base}native-mesh-walk-22.mp4)
- [접지점·지면 표시 3배 확대/3배 느린 검토]({base}native-mesh-walk-contact-3x-slow.mp4)
- [다시 편집하고 출력하는 원본·.blend·부품·스크립트 ZIP]({base}cream-mesh-editable-source.zip)
- [Windows 원본 PNG·접지 CSV·검사 자료]({base}native-mesh-walk-evidence.zip)
- [검증 출처·SHA-256]({base}mesh-walk-validation.json)

**자연스러운 보행 완성본이 아닙니다.** 짧은 접지 클립에 남은 관절·털 이음새 결함을 확인하고 이어서 편집하기 위한 미승인 자료입니다. 기존 프로그램을 종료하고 검토 ZIP을 완전히 푼 뒤 `Launch-Mesh-Walk-Review.cmd`를 실행합니다. 일반 실행은 승인된8장 보행을 유지합니다.

비교 영상은60FPS, Box2D는60Hz입니다. 접지 영상은60Hz 출력 프레임을20FPS로 인코딩해3배 느리게 보여줍니다. 영상은 앱의 실제 DrawCareSprite로 만든 진단 렌더링이며 **실제 바탕화면 녹화가 아닙니다**.

검증 소스: `{head}` · [Windows 실행 검사](https://github.com/{REPO}/actions/runs/{run_id}).
''')
        if not existing:
            gh('release', 'create', TAG, '--repo', REPO, '--draft', '--prerelease', '--target', head,
               '--title', f'Yeopjari Cat {VERSION} — 메시 보행 시각 검토본 · 미승인', '--notes-file', str(notes))
        remote = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                              '--json', 'isDraft,targetCommitish,assets'))
        require(remote['isDraft'] and remote['targetCommitish'] == head, 'Release changed during preparation')
        require({a['name'] for a in remote['assets']} <= set(expected), 'Unexpected existing draft assets')
        with tempfile.TemporaryDirectory(prefix='mesh-existing-') as previous:
            if remote['assets']:
                gh('release', 'download', TAG, '--repo', REPO, '--dir', previous)
            already = {p.name: sha(p) for p in Path(previous).iterdir()}
            for name, digest in already.items():
                require(expected[name] == digest, f'Draft asset differs and was not overwritten: {name}')
            for path in files:
                if path.name not in already:
                    gh('release', 'upload', TAG, '--repo', REPO, str(path))
        with tempfile.TemporaryDirectory(prefix='mesh-verified-') as downloaded:
            gh('release', 'download', TAG, '--repo', REPO, '--dir', downloaded)
            require({p.name: sha(p) for p in Path(downloaded).iterdir()} == expected,
                    'Uploaded release files failed download hash verification')
        require(api('releases/latest')['tag_name'] == stable_before, 'Stable changed before review publication')
        gh('release', 'edit', TAG, '--repo', REPO, '--notes-file', str(notes), '--draft=false',
           '--prerelease=true', '--latest=false')
        published = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                                 '--json', 'isDraft,isPrerelease,targetCommitish,url'))
        require(not published['isDraft'] and published['isPrerelease'] and
                published['targetCommitish'] == head, 'Published review metadata differs')
        require(api('releases/latest')['tag_name'] == stable_before, 'Stable latest must remain unchanged')
        print(json.dumps({'review': published['url'], 'validated_source': head,
                          'stable_latest': stable_before, 'production_approved': False,
                          'desktop_recording': False, 'asset_sha256': expected}, indent=2))


if __name__ == '__main__':
    main()
