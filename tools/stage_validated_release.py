#!/usr/bin/env python3
"""CI-only staging of already validated artifacts. Never publishes a release."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import release
from verify_silent_exe import verify_silent_exe


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def main():
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert os.environ['GITHUB_REPOSITORY'] == 'cho-woo/yeopjari-cat'
    assert os.environ['GITHUB_REF'] == 'refs/heads/main'
    metadata = list(Path('verified-package').rglob('release-info.json'))
    assert len(metadata) == 1
    package = metadata[0].parent
    release.verify_release(package)
    info = json.loads(metadata[0].read_text())
    version = release.version_from_header(release.ROOT / 'src/app_version.h')
    assert version == info['version'] == '2.0.17'
    exe = package / f'YeopjariCat-{version}.exe'
    verify_silent_exe(exe.read_bytes())
    reports = list(Path('native-review').rglob('render-self-test.txt'))
    captures = list(Path('native-review').rglob('two-cats-gdi.png'))
    assert len(reports) == len(captures) == 1
    assert reports[0].read_text(encoding='utf-8-sig').startswith('exit=0 ')
    assert captures[0].read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    stage = Path('review-assets')
    stage.mkdir()
    files = [package / name for name in info['files']]
    for path in [reports[0], captures[0]]:
        target = stage / path.name
        shutil.copyfile(path, target)
        files.append(target)
    # Native frames and raw measured counters travel together for local review.
    # Do not attach an obsolete earlier release's generated preview.
    archive = Path(shutil.make_archive(str(stage / 'native-walking-evidence'), 'zip', 'native-review'))
    files.append(archive)
    provenance = stage / 'windows-validation.json'
    provenance.write_text(json.dumps({
        'source_commit': os.environ['VALIDATED_SHA'],
        'validation_run': int(os.environ['VALIDATION_RUN']),
        'validation_url': 'https://github.com/cho-woo/yeopjari-cat/actions/runs/' + os.environ['VALIDATION_RUN'],
        'native_self_test': 'passed', 'native_gdi_render_test': 'passed',
        'exe_sha256': sha256(exe), 'preview_is_desktop_recording': False,
    }, indent=2) + '\n')
    files.append(provenance)
    hashes = {path.name: sha256(path) for path in files}
    sums = stage / 'review-SHA256SUMS.txt'
    sums.write_text(''.join(digest + '  ' + name + '\n' for name, digest in sorted(hashes.items())))
    files.append(sums)
    repo = 'cho-woo/yeopjari-cat'
    tag = 'v' + version
    history = json.loads(gh('release', 'list', '--repo', repo, '--limit', '1000', '--json', 'tagName,isDraft,isPrerelease'))
    assert len(history) < 1000
    existing = [item for item in history if item['tagName'] == tag]
    assert len(existing) <= 1
    if existing:
        assert existing[0]['isDraft'] and not existing[0]['isPrerelease']
        draft = json.loads(gh('release', 'view', tag, '--repo', repo, '--json', 'targetCommitish'))
        assert draft['targetCommitish'] == os.environ['VALIDATED_SHA']
    else:
        gh('release', 'create', tag, '--repo', repo, '--draft', '--target', os.environ['VALIDATED_SHA'],
           '--title', 'Yeopjari Cat ' + version, '--notes-file', str(package / 'RELEASE_NOTES_KO.txt'))
    gh('release', 'upload', tag, '--repo', repo, '--clobber', *map(str, files))
    with tempfile.TemporaryDirectory() as directory:
        gh('release', 'download', tag, '--repo', repo, '--dir', directory)
        actual = {p.name: sha256(p) for p in Path(directory).iterdir()}
        assert actual == {p.name: sha256(p) for p in files}
    draft = json.loads(gh('release', 'view', tag, '--repo', repo, '--json', 'isDraft,isPrerelease,targetCommitish,url'))
    assert draft['isDraft'] and not draft['isPrerelease']
    assert draft['targetCommitish'] == os.environ['VALIDATED_SHA']
    print('Verified unpublished draft:', draft['url'])
    print(json.dumps({'verified_asset_sha256': hashes}, indent=2))


if __name__ == '__main__':
    main()
