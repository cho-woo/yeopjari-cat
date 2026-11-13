"""Compile a per-user NSIS installer from the release tool's verified user ZIP.

Unsigned builds are local reviews, prominently named and isolated. Release
builds require a trusted payload and an explicit certificate-provider signing
command for both the generated uninstaller and outer installer.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile
import release
from prepare_site_download import verify_signature

ROOT = Path(__file__).resolve().parents[1]
FILES = {'YeopjariCat.exe', 'update-feed.txt', 'README_KO.txt', 'CHANGELOG_KO.txt',
         'THIRD_PARTY_NOTICES.txt', 'DISTRIBUTION_KO.txt'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True, help='Verified release package directory containing release-info.json and the user ZIP.')
    parser.add_argument('--makensis', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--review-unsigned', action='store_true')
    parser.add_argument('--expected-subject')
    parser.add_argument('--sign-command', help='Explicit CA/token signing helper, followed by the quoted generated file path. Never include private keys, passwords or PINs here.')
    args = parser.parse_args()
    try:
        info = release.verify_release(args.package)
        version = info['version']
        if not args.review_unsigned and (not args.expected_subject or not args.sign_command):
            raise ValueError('공개 배포 빌드는 인증된 게시자와 인증기관 서명 도구가 필요합니다. 로컬 검토는 --review-unsigned를 사용하세요.')
        if args.output.exists():
            raise ValueError('Use a new output directory; existing installers are immutable.')
        args.output.mkdir(parents=True)
        with tempfile.TemporaryDirectory(prefix='yeopjari-installer-') as directory:
            payload = Path(directory)
            with zipfile.ZipFile(args.package / f'YeopjariCat-Windows-{version}.zip') as archive:
                if set(archive.namelist()) != FILES:
                    raise ValueError('User ZIP entries differ from the six expected files.')
                for name in sorted(FILES):
                    (payload / name).write_bytes(archive.read(name))
            if not args.review_unsigned:
                verify_signature(payload / 'YeopjariCat.exe', args.expected_subject)
            suffix = '-UNSIGNED-REVIEW' if args.review_unsigned else ''
            output = args.output.resolve() / f'YeopjariCat-Setup-{version}{suffix}.exe'
            arguments = [str(args.makensis.resolve()), '/V2', '/INPUTCHARSET', 'UTF8',
                f'/DAPP_VERSION={version}', f'/DPAYLOAD_DIR={payload}', f'/DOUTPUT_EXE={output}']
            if args.review_unsigned:
                arguments.append('/DUNSIGNED_REVIEW')
            else:
                arguments.append(f'/DSIGN_COMMAND={args.sign_command}')
            arguments.append(str(ROOT / 'packaging/windows/installer.nsi'))
            subprocess.run(arguments, check=True)
            if not args.review_unsigned:
                verify_signature(output, args.expected_subject)
            print(json.dumps({'version': version, 'installer': str(output),
                'public_distribution_allowed': False,
                'next_step': 'Complete installation/upgrade/uninstall tests and verify all three binaries with prepare_site_download.py before publication.'}, ensure_ascii=False))
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
