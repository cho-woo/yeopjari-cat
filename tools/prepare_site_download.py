"""Enable a website download only after all three exact binaries verify on Windows.

No signing keys, certificate purchases or publication are performed here.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from urllib.parse import urlsplit
import release

ROOT = Path(__file__).resolve().parents[1]


def verify_signature(path: Path, expected_subject: str) -> dict:
    shell = shutil.which('pwsh') or shutil.which('powershell')
    if not shell:
        raise ValueError('Windows PowerShell의 실제 인증서 검증이 필요합니다.')
    process = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-File',
        str(ROOT / 'tools/verify_windows_signature.ps1'), '-Path', str(path.resolve()),
        '-ExpectedSubject', expected_subject], capture_output=True, text=True,
        encoding='utf-8', errors='replace')
    if process.returncode:
        raise ValueError(f'{path.name}: 공개 다운로드를 열 수 없습니다. 공인 코드 서명·게시자·타임스탬프 검증 실패.\n{process.stderr.strip()}')
    result = json.loads(process.stdout.lstrip('\ufeff'))
    if result['status'] != 'valid' or result['subject'] != expected_subject:
        raise ValueError('Unexpected signature verification result.')
    # Confirm the file did not change between trust verification and hashing.
    if hashlib.sha256(path.read_bytes()).hexdigest() != result['sha256']:
        raise ValueError('File changed during signature verification.')
    return result


def installer_url(url: str, version: str) -> str:
    release.require_url(url)
    parsed = urlsplit(url)
    filename = f'YeopjariCat-Setup-{version}.exe'
    expected_path = f'/cho-woo/yeopjari-cat/releases/download/v{version}/{filename}'
    if parsed.query or parsed.fragment or parsed.port:
        raise ValueError('Use a stable HTTPS download URL without a query or fragment.')
    if not ((parsed.hostname == 'github.com' and parsed.path == expected_path) or
            (parsed.hostname == 'downloads.valuewander.com' and parsed.path == f'/{version}/{filename}')):
        raise ValueError('Download URL must identify this exact immutable release installer.')
    return url


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--uninstaller', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--expected-subject', required=True, help='Exact Subject of the verified public certificate; not an arbitrary brand name.')
    parser.add_argument('--url', required=True)
    parser.add_argument('--output', type=Path, required=True, help='Website dist/release.json; untouched if verification fails.')
    args = parser.parse_args()
    try:
        release.version_tuple(args.version)
        installer_url(args.url, args.version)
        release.inspect_release_exe(args.application.read_bytes(), args.version)
        proof = {name: verify_signature(path, args.expected_subject) for name, path in
                 [('application', args.application), ('uninstaller', args.uninstaller), ('installer', args.installer)]}
        result = {'schema': 1, 'status': 'ready', 'version': args.version, 'url': args.url,
            'sha256': proof['installer']['sha256'], 'bytes': proof['installer']['bytes'],
            'publisher': proof['installer']['publisher_name'],
            'signature_verified_at': datetime.now(timezone.utc).isoformat(),
            'signature_proof': proof}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        temporary.replace(args.output)
        print(f'Verified app, uninstaller and installer. Prepared {args.output}')
        return 0
    except (ValueError, OSError, KeyError, json.JSONDecodeError) as error:
        print(str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
