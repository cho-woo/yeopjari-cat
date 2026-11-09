#!/usr/bin/env python3
"""Publish the exact Windows-validated v2.0.20 command update as stable.

CI-only. Uses a draft/tag prepared at the validated source commit. It never
rebuilds, replaces a public release, or promotes the opt-in mesh walking study.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from urllib.request import Request, urlopen
import zipfile

import release
from verify_silent_exe import verify_silent_exe

REPO = 'cho-woo/yeopjari-cat'
VERSION = '2.0.20'
TAG = 'v' + VERSION
PREVIOUS_STABLE = 'v2.0.16'
ROOT = Path(__file__).resolve().parents[1]
FEED_URL = f'https://github.com/{REPO}/releases/latest/download/latest.txt'
BASE_URL = f'https://github.com/{REPO}/releases/download/{TAG}/'
SAVE_STEP = 'Execute Windows save, cursor and moving-window regression tests'
RENDER_STEP = 'Render real Windows GDI+ gait, contact and posture regressions'


def require(condition, message):
    if not condition:
        raise ValueError(message)


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


def exactly_one(paths, description):
    matches = list(paths)
    require(len(matches) == 1, f'Expected exactly one {description}; found {len(matches)}')
    return matches[0]


def validated_source(run_id, head):
    require(run_id.isdigit(), 'VALIDATION_RUN must identify the successful validation')
    run = api('actions/runs/' + run_id)
    require(run.get('path') == '.github/workflows/validate.yml' and
            run.get('status') == 'completed' and run.get('conclusion') == 'success',
            'The selected Validate workflow must have completed successfully')
    require(run.get('head_repository', {}).get('full_name') == REPO and
            run.get('head_branch') == 'main' and run.get('head_sha') == head,
            'Checkout must be exactly the validated main repository commit')
    # A single bounded page contains both jobs and their individual native steps.
    jobs = api('actions/runs/' + run_id + '/jobs?per_page=100')['jobs']
    require(len(jobs) < 100, 'Validation jobs exceed bounded inspection')
    for name in ('validate', 'windows-smoke'):
        job = exactly_one((j for j in jobs if j['name'] == name), name + ' job')
        require(job.get('status') == 'completed' and job.get('conclusion') == 'success',
                f'Missing successful {name} job')
        if name == 'windows-smoke':
            for step_name in (SAVE_STEP, RENDER_STEP):
                step = exactly_one((s for s in job['steps'] if s['name'] == step_name), step_name)
                require(step.get('status') == 'completed' and step.get('conclusion') == 'success',
                        f'Native Windows step did not pass: {step_name}')
    return run


def normal_walk_contract():
    manifest = json.loads((ROOT / 'assets/care-sprites/manifest.json').read_text(encoding='utf-8'))
    walk = exactly_one((c for c in manifest['clips'] if c['clip'] == 'walk' and c['character'] == 0),
                       'normal cream walk clip')
    require(walk['frame_count'] == len(walk['frames']) == 8, 'Normal walk must retain eight approved drawings')
    walk_path = ROOT / 'assets/care-sprites' / walk['source']
    require(sha(walk_path) == walk['sha256'], 'Normal walking artwork hash differs from its manifest')
    source = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
    require(re.search(r'\bbool\s+meshWalkReview\s*=\s*false\s*;', source),
            'Mesh walking must remain disabled by default')
    return {'normal_walk_frames': 8, 'normal_walk_sha256': walk['sha256'], 'mesh_default': False}


def manual_motion_contract(path):
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    require(report.get('schema') == 1 and report.get('test') == 'manual-motion-native' and
            report.get('status') == 'passed' and report.get('exit_code', 0) == 0 and
            report.get('version') == VERSION, 'Native manual-motion test did not pass for this version')
    require(report.get('mesh_walk_default') is False and report.get('approved_walk_frames') == 8,
            'Native report does not retain the approved default walk')
    require(report.get('menu_commands') == [2800, 2801, 2802] and
            report.get('menu_labels_checked') is True and
            isinstance(report.get('unsafe_states_checked'), int) and report['unsafe_states_checked'] > 0,
            'Native manual-motion menu or unsafe-state checks are missing')
    for key in ('pause_preference_preserved', 'suspension_clock_preserved'):
        require(report.get(key) is True, f'Native manual-motion check failed: {key}')
    characters = report.get('characters', [])
    require(len(characters) == 2 and {c.get('character') for c in characters} == {0, 8},
            'Native manual-motion report must cover both characters')
    for character in characters:
        for key in ('walk_distance', 'run_distance', 'walk_peak_speed', 'run_peak_speed', 'jump_max_lift'):
            value = character.get(key)
            require(isinstance(value, (int, float)) and math.isfinite(value) and value > 0,
                    f'Native manual-motion metric is missing or nonpositive: {key}')
        for key in ('jump_landed', 'moving_jump_braked'):
            require(character.get(key) is True, f'Native manual-motion check failed: {key}')
    return report


def archive_files(output, paths):
    seen = set()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, name in sorted(paths, key=lambda item: item[1]):
            require(path.is_file() and not path.is_symlink(), f'Invalid evidence file: {path}')
            require(name not in seen and not name.startswith('/') and '..' not in Path(name).parts,
                    f'Unsafe or duplicate evidence archive path: {name}')
            seen.add(name)
            info = zipfile.ZipInfo(name, (2026, 11, 9, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            with path.open('rb') as source, archive.open(info, 'w', force_zip64=True) as destination:
                shutil.copyfileobj(source, destination, 4 * 1024 * 1024)


def public_bytes(url, limit):
    # No token is sent: clients must be able to fetch these public URLs too.
    request = Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'YeopjariCat-release-verifier'})
    with urlopen(request, timeout=30) as response:
        data = response.read(limit + 1)
    require(len(data) <= limit, f'Public response exceeds size limit: {url}')
    return data


def verify_public_update(package, info, expected, sizes):
    last_error = ''
    for attempt in range(12):
        try:
            latest = api('releases/latest')
            require(latest['tag_name'] == TAG and not latest['draft'] and not latest['prerelease'],
                    'GitHub latest release is not the new stable update')
            raw = public_bytes(FEED_URL, release.MAX_MANIFEST)
            require(raw == (package / 'latest.txt').read_bytes(), 'Official latest feed has not reached the exact tested manifest')
            manifest = release.parse_manifest(raw)
            require(manifest['version'] == VERSION and manifest['download_url'] == info['download_url'],
                    'Official latest feed identifies the wrong version or executable')
            # Verify all public package assets and provenance, including the exact
            # EXE bytes referenced by the official feed, after public promotion.
            for name, digest in expected.items():
                data = public_bytes(BASE_URL + name, sizes[name])
                require(hashlib.sha256(data).hexdigest() == digest, f'Public download hash mismatch: {name}')
                if name == f'YeopjariCat-{VERSION}.exe':
                    require(len(data) == int(manifest['size_bytes']) and digest == manifest['sha256'],
                            'Public executable does not match the official latest feed')
            return
        except (OSError, ValueError, subprocess.CalledProcessError) as exc:
            last_error = f'{type(exc).__name__}: {exc}'
            print(f'Public verification attempt {attempt + 1}/12: {last_error}', flush=True)
            if attempt < 11:
                time.sleep(5)
    raise ValueError('Stable release was published, but public delivery verification failed: ' + last_error)


def publication_result(published, head, run_id, walk_contract, expected):
    result = {'release': published['url'], 'source_commit': head, 'validation_run': int(run_id),
              'stable_latest': TAG, 'official_feed': FEED_URL, 'public_delivery_verified': True,
              **walk_contract, 'asset_sha256': expected}
    print(json.dumps(result, indent=2), flush=True)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as output:
            output.write(f'Published [{TAG}]({published["url"]}) from `{head}`.\n\n'
                         f'Official [latest feed]({FEED_URL}) and public asset hashes verified.\n\n'
                         'Normal walk: 8 drawings. Mesh review default: disabled.\n')


def main():
    require(os.environ.get('GITHUB_ACTIONS') == 'true', 'This publisher runs only in GitHub Actions')
    require(os.environ.get('GITHUB_REPOSITORY') == REPO, 'Wrong repository')
    require(os.environ.get('GITHUB_REF') == 'refs/heads/main', 'Dispatch from main only')
    os.chdir(ROOT)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    require(re.fullmatch(r'[0-9a-f]{40}', head), 'Invalid source commit')
    run_id = os.environ.get('VALIDATION_RUN', '')
    validated_source(run_id, head)
    require(release.version_from_header(ROOT / 'src/app_version.h') == VERSION, 'Wrong source version')
    walk_contract = normal_walk_contract()
    stable_before = api('releases/latest')['tag_name']
    require(stable_before in (PREVIOUS_STABLE, TAG),
            f'Stable latest changed from {PREVIOUS_STABLE} to {stable_before}; reassess this publication')

    metadata = exactly_one(Path('verified-package').rglob('release-info.json'), 'package metadata')
    package = metadata.parent
    info = release.verify_release(package)
    require(info['version'] == VERSION and info['github_repository'] == REPO and
            info['feed_url'] == FEED_URL and info['download_url'] == BASE_URL + f'YeopjariCat-{VERSION}.exe',
            'Wrong verified package, feed or download URL')
    exe = package / f'YeopjariCat-{VERSION}.exe'
    verify_silent_exe(exe.read_bytes())
    native_root = Path('native-review')
    report = exactly_one(native_root.rglob('render-self-test.txt'), 'native render report')
    require(report.read_text(encoding='utf-8-sig').startswith('exit=0 '), 'Native render artifact does not report exit=0')
    manual_report = exactly_one(native_root.rglob('manual-motion-self-test.json'), 'native manual-motion report')
    manual_contract = manual_motion_contract(manual_report)

    history = json.loads(gh('release', 'list', '--repo', REPO, '--limit', '1000',
                           '--json', 'tagName,isDraft,isPrerelease'))
    require(len(history) < 1000, 'Release history exceeds bounded inspection')
    existing = exactly_one((r for r in history if r['tagName'] == TAG), 'pre-created release draft')
    require(not existing['isPrerelease'], 'The command update must be a stable release')
    already_public = not existing['isDraft']
    require(stable_before == (TAG if already_public else PREVIOUS_STABLE),
            'A published update may only be reverified while it is the latest stable release')
    for item in history:
        if not item['isDraft'] and not item['isPrerelease']:
            if already_public and item['tagName'] == TAG:
                continue
            require(item['tagName'].startswith('v') and
                    release.version_tuple(item['tagName'][1:]) < release.version_tuple(VERSION),
                    'Stable release history contains an equal or newer version')
    draft = json.loads(gh('release', 'view', TAG, '--repo', REPO, '--json', 'isDraft,isPrerelease,targetCommitish,assets,url'))
    require(draft['isDraft'] == (not already_public) and not draft['isPrerelease'] and draft['targetCommitish'] == head,
            'The stable release must target exactly the validated commit')
    tag_ref = exactly_one((r for r in api('git/matching-refs/tags/' + TAG)
                           if r['ref'] == 'refs/tags/' + TAG), 'pre-created release tag')
    require(tag_ref and api('commits/' + TAG)['sha'] == head, 'Release tag differs from the validated commit')

    with tempfile.TemporaryDirectory(prefix='motion-release-') as directory:
        stage = Path(directory)
        files = [package / name for name in info['files']] + [metadata]
        native_report = stage / 'render-self-test.txt'
        shutil.copyfile(report, native_report)
        files.append(native_report)
        native_manual_report = stage / 'manual-motion-self-test.json'
        shutil.copyfile(manual_report, native_manual_report)
        files.append(native_manual_report)
        # Include actual command evidence when the native test emitted it; no
        # encoder or unrelated mesh-review media is needed for this release.
        command_roots = [p for p in native_root.rglob('*')
                         if p.is_dir() and p.name in ('manual-command', 'manual-commands')]
        require(len(command_roots) <= 1, 'Ambiguous native manual-command evidence')
        command_hashes = {}
        if command_roots:
            command_root = command_roots[0]
            evidence_files = [(p, str(p.relative_to(command_root))) for p in command_root.rglob('*') if p.is_file()]
            require(evidence_files, 'Native manual-command evidence directory is empty')
            command_hashes = {name: sha(path) for path, name in evidence_files}
            evidence = stage / 'manual-command-evidence.zip'
            archive_files(evidence, evidence_files)
            files.append(evidence)
        proof = stage / 'windows-validation.json'
        proof.write_text(json.dumps({
            'version': VERSION, 'source_commit': head, 'validation_run': int(run_id),
            'validation_url': f'https://github.com/{REPO}/actions/runs/{run_id}',
            'native_self_test': 'passed', 'native_self_test_step': SAVE_STEP,
            'native_gdi_render_test': 'passed', 'native_gdi_render_step': RENDER_STEP,
            'native_render_report_sha256': sha(report), 'exe_sha256': sha(exe),
            'native_manual_motion_test': 'passed', 'native_manual_motion_report_sha256': sha(manual_report),
            'native_manual_motion': manual_contract,
            **walk_contract,
            'mesh_walk_production_approved': False, 'desktop_recording': False,
            'manual_command_evidence_sha256': command_hashes,
        }, indent=2) + '\n', encoding='utf-8')
        files.append(proof)
        sums = stage / 'motion-update-SHA256SUMS.txt'
        sums.write_text(''.join(sha(p) + '  ' + p.name + '\n' for p in sorted(files, key=lambda p: p.name)), encoding='ascii')
        files.append(sums)
        require(len({p.name for p in files}) == len(files), 'Duplicate release asset names')
        expected = {p.name: sha(p) for p in files}
        sizes = {p.name: p.stat().st_size for p in files}
        notes = stage / 'notes.md'
        notes.write_text((package / 'RELEASE_NOTES_KO.txt').read_text(encoding='utf-8') + '\n\n' +
                         '고양이 우클릭 → 행동시키기 → 걷기·달리기·점프하기를 선택할 수 있습니다.\n'
                         '고양이 우클릭 → 프로그램 업데이트 → 업데이트 확인…에서 설치할 수 있습니다.\n\n'
                         f'- [Windows ZIP]({BASE_URL}YeopjariCat-Windows-{VERSION}.zip)\n'
                         f'- [Windows EXE]({BASE_URL}YeopjariCat-{VERSION}.exe)\n'
                         f'- [검증 출처와 SHA-256]({BASE_URL}windows-validation.json)\n\n'
                         '기존의 온전한 고양이 그림으로 걷는 리듬을 다듬고, 턱시도냥의 무늬를 모든 동작에 맞춰 정돈했습니다. 기존 돌봄 기능과 성장 기록은 유지합니다.\n'
                         f'검증 소스: `{head}` · [Windows 실행 검사](https://github.com/{REPO}/actions/runs/{run_id}).\n',
                         encoding='utf-8')
        if already_public:
            # A CDN failure after promotion can be retried without touching any
            # public release metadata or bytes. Every expected asset must match.
            require({a['name'] for a in draft['assets']} == set(expected), 'Published release asset set differs')
            with tempfile.TemporaryDirectory(prefix='motion-public-verified-') as downloaded:
                gh('release', 'download', TAG, '--repo', REPO, '--dir', downloaded)
                require({p.name: sha(p) for p in Path(downloaded).iterdir()} == expected,
                        'Published release differs from the exact validated artifacts; nothing was changed')
            verify_public_update(package, info, expected, sizes)
            publication_result(draft, head, run_id, walk_contract, expected)
            return
        require({a['name'] for a in draft['assets']} <= set(expected), 'Unexpected assets in existing draft')
        with tempfile.TemporaryDirectory(prefix='motion-existing-') as previous:
            if draft['assets']:
                gh('release', 'download', TAG, '--repo', REPO, '--dir', previous)
            already = {p.name: sha(p) for p in Path(previous).iterdir()}
            for name, digest in already.items():
                require(expected[name] == digest, f'Draft asset differs and was not overwritten: {name}')
            for path in files:
                if path.name not in already:
                    gh('release', 'upload', TAG, '--repo', REPO, str(path))
        with tempfile.TemporaryDirectory(prefix='motion-verified-') as downloaded:
            gh('release', 'download', TAG, '--repo', REPO, '--dir', downloaded)
            require({p.name: sha(p) for p in Path(downloaded).iterdir()} == expected,
                    'Uploaded release files failed download hash verification')
        current = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                                '--json', 'isDraft,isPrerelease,targetCommitish'))
        require(current['isDraft'] and not current['isPrerelease'] and current['targetCommitish'] == head,
                'Draft metadata changed before publication')
        require(api('commits/' + TAG)['sha'] == head, 'Tag changed before publication')
        require(api('releases/latest')['tag_name'] == stable_before, 'Stable latest changed before publication')
        gh('release', 'edit', TAG, '--repo', REPO, '--notes-file', str(notes),
           '--title', f'옆자리 고양이 {VERSION} — 가벼운 걸음과 귀여운 턱시도냥',
           '--draft=false', '--prerelease=false', '--latest=true')
        published = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                                 '--json', 'isDraft,isPrerelease,targetCommitish,url'))
        require(not published['isDraft'] and not published['isPrerelease'] and
                published['targetCommitish'] == head, 'Published stable metadata differs')
        verify_public_update(package, info, expected, sizes)
        publication_result(published, head, run_id, walk_contract, expected)


if __name__ == '__main__':
    main()
