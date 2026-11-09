#!/usr/bin/env python3
"""Publish an already Windows-validated review build; never change stable latest."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

REPO = 'cho-woo/yeopjari-cat'
TAG = 'v2.0.17'


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert os.environ.get('GITHUB_REPOSITORY') == REPO
    assert os.environ.get('GITHUB_REF') == 'refs/heads/main'
    draft = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                          '--json', 'isDraft,targetCommitish'))
    assert draft['isDraft']
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        original = root / 'original'
        original.mkdir()
        gh('release', 'download', TAG, '--repo', REPO, '--dir', str(original))
        hashes = {p.name: sha(p) for p in original.iterdir()}
        for line in (original / 'review-SHA256SUMS.txt').read_text().splitlines():
            digest, name = line.split('  ', 1)
            assert hashes[name] == digest
        proof = json.loads((original / 'windows-validation.json').read_text())
        assert proof['source_commit'] == draft['targetCommitish']
        run = json.loads(gh('api', f'repos/{REPO}/actions/runs/{proof["validation_run"]}'))
        assert run['conclusion'] == 'success' and run['head_sha'] == proof['source_commit']
        subprocess.run(['git', 'diff', '--exit-code', proof['source_commit'], 'HEAD', '--',
                        'src', 'assets', 'tests', 'build.sh'], check=True)
        media = Path('docs/walk-art-review')
        render = json.loads((media / 'stop-path-fixed.json').read_text())
        assert render['source_archive_sha256'] == hashes['native-walking-evidence.zip']
        assert render['new_accepted_art_frames'] == 0
        extras = [media / 'stop-path-fixed.mp4', media / 'stop-path-4s-slow.mp4',
                  media / 'candidate48-walk-art-constant-22.mp4',
                  media / 'candidate64-walk-art-constant-22.mp4']
        bundle = root / 'walk-art-review.zip'
        with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(media.rglob('*')):
                if path.is_file():
                    info = zipfile.ZipInfo(str(path.relative_to(media)), (2026, 11, 9, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, path.read_bytes())
        extras.append(bundle)
        sums = root / 'walk-review-SHA256SUMS.txt'
        sums.write_text(''.join(sha(p) + '  ' + p.name + '\n' for p in extras))
        extras.append(sums)
        for path in extras:
            if path.name in hashes:
                assert hashes[path.name] == sha(path)
            else:
                gh('release', 'upload', TAG, '--repo', REPO, str(path))
        verified = root / 'verified'
        verified.mkdir()
        gh('release', 'download', TAG, '--repo', REPO, '--dir', str(verified))
        expected = hashes | {p.name: sha(p) for p in extras}
        assert {p.name: sha(p) for p in verified.iterdir()} == expected
        notes = root / 'notes.md'
        notes.write_text((media / 'README.md').read_text())
        gh('release', 'edit', TAG, '--repo', REPO, '--notes-file', str(notes),
           '--draft=false', '--prerelease=true', '--latest=false')
        stable = json.loads(gh('api', f'repos/{REPO}/releases/latest'))
        assert stable['tag_name'] != TAG
        print(json.dumps({'review': TAG, 'source_commit': proof['source_commit'],
                          'stable_latest': stable['tag_name'], 'sha256': expected}, indent=2))


if __name__ == '__main__':
    main()
