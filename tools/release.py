#!/usr/bin/env python3
"""Build and verify a Yeopjari Cat release. Python standard library only.

This command prepares local files. It never creates a repository or publishes
anything. The optional generated PowerShell script performs GitHub publishing.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
from urllib.parse import urlsplit
import zipfile


ROOT = Path(__file__).resolve().parents[1]
MAX_MANIFEST = 65536
MAX_FEED_FILE = 8192
MAX_NOTES = 8192
MIN_EXE = 1024
MAX_EXE = 128 * 1024 * 1024
MANIFEST_KEYS = ("schema", "app_id", "channel", "version", "download_url", "sha256", "size_bytes", "notes_b64")


class ReleaseError(ValueError):
    pass


def default_executable(root: Path) -> Path:
    built = root / "release" / "YeopjariCat.exe"
    bundled = root.parent / "YeopjariCat.exe"
    return built if built.is_file() or not bundled.is_file() else bundled


def version_tuple(text: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", text):
        raise ReleaseError("버전은 앞에 0이 없는 세 숫자 형식이어야 합니다. 예: 1.3.0")
    if any(len(part) > 5 for part in text.split(".")):
        raise ReleaseError("버전의 각 숫자는 0~65535 범위여야 합니다.")
    parts = tuple(int(part) for part in text.split("."))
    if any(part > 65535 for part in parts):
        raise ReleaseError("버전의 각 숫자는 0~65535 범위여야 합니다.")
    return parts


def version_from_header(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    parts = []
    for suffix in ("MAJOR", "MINOR", "PATCH"):
        matches = re.findall(r"^\s*#define\s+YEOPJARI_VERSION_" + suffix + r"\s+(\d+)\s*$", text, re.M)
        if len(matches) != 1:
            raise ReleaseError(f"버전 헤더의 YEOPJARI_VERSION_{suffix}를 확인하세요.")
        parts.append(matches[0])
    result = ".".join(parts)
    version_tuple(result)
    return result


def is_https_url(url: str) -> bool:
    if not isinstance(url, str) or not 8 < len(url) <= MAX_MANIFEST:
        return False
    if url[:8].lower() != "https://":
        return False
    allowed = "-._~:/?@!$&'()*+,;=%"
    if any(not (c.isascii() and (c.isalnum() or c in allowed)) for c in url):
        return False
    pos = 0
    while pos < len(url):
        if url[pos] == "%":
            if pos + 2 >= len(url) or not re.fullmatch(r"[0-9a-fA-F]{2}", url[pos + 1:pos + 3]):
                return False
            byte = int(url[pos + 1:pos + 3], 16)
            if byte < 32 or byte in (127, 92):
                return False
            pos += 3
        else:
            pos += 1
    authority = re.split(r"[/?]", url[8:], maxsplit=1)[0]
    if not authority or "@" in authority or "%" in authority:
        return False
    if ":" in authority:
        if authority.count(":") != 1:
            return False
        host, port = authority.split(":")
        if port != "443":
            return False
    else:
        host = authority
    if not host or len(host) > 253:
        return False
    if all(c in "0123456789." for c in host):
        labels = host.split(".")
        return len(labels) == 4 and all(re.fullmatch(r"0|[1-9][0-9]{0,2}", x) and int(x) <= 255 for x in labels)
    return all(1 <= len(label) <= 63 and re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?", label)
               for label in host.split("."))


def require_url(url: str) -> str:
    if not is_https_url(url):
        raise ReleaseError("배포 주소는 인증 정보가 없는 유효한 HTTPS 주소여야 합니다. 기본 포트 또는 443만 지원합니다.")
    return url


def require_feed_url(url: str) -> str:
    require_url(url)
    if len((url + "\n").encode("ascii")) > MAX_FEED_FILE:
        raise ReleaseError("업데이트 확인 주소 파일은 줄바꿈을 포함해 8 KiB 이하여야 합니다.")
    return url


def require_notes(notes: str) -> str:
    try:
        encoded = notes.encode("utf-8", errors="strict")
    except UnicodeError as exc:
        raise ReleaseError("업데이트 내용은 UTF-8 텍스트여야 합니다.") from exc
    if len(encoded) > MAX_NOTES or "\0" in notes:
        raise ReleaseError("업데이트 내용은 NUL 문자 없이 UTF-8 8 KiB 이하여야 합니다.")
    return notes


def make_manifest(version: str, download_url: str, exe: bytes, notes: str) -> bytes:
    version_tuple(version)
    require_url(download_url)
    require_notes(notes)
    if not MIN_EXE <= len(exe) <= MAX_EXE:
        raise ReleaseError("실행 파일 크기는 1 KiB~128 MiB여야 합니다.")
    values = ("1", "yeopjari-cat", "stable", version, download_url,
              hashlib.sha256(exe).hexdigest(), str(len(exe)),
              base64.b64encode(notes.encode("utf-8")).decode("ascii"))
    result = "".join(f"{key}={value}\n" for key, value in zip(MANIFEST_KEYS, values)).encode("ascii")
    if len(result) > MAX_MANIFEST:
        raise ReleaseError("업데이트 안내 파일이 64 KiB를 넘었습니다.")
    return result


def parse_manifest(raw: bytes) -> dict[str, str]:
    if not raw or len(raw) > MAX_MANIFEST:
        raise ReleaseError("업데이트 안내 파일 크기가 올바르지 않습니다.")
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ReleaseError("업데이트 안내 파일에 잘못된 문자가 있습니다.") from exc
    if "\r" in text:
        if "\r" in text.replace("\r\n", "") or "\n" in text.replace("\r\n", ""):
            raise ReleaseError("업데이트 안내 파일의 줄바꿈 형식이 섞여 있습니다.")
        text = text.replace("\r\n", "\n")
    if text.endswith("\n"):
        text = text[:-1]
    result: dict[str, str] = {}
    for line in text.split("\n"):
        key, separator, value = line.partition("=")
        if not separator or key not in MANIFEST_KEYS or key in result:
            raise ReleaseError("업데이트 안내 파일에 중복되거나 잘못된 항목이 있습니다.")
        result[key] = value
    if set(result) != set(MANIFEST_KEYS):
        raise ReleaseError("업데이트 안내 파일의 필수 항목이 없습니다.")
    if result["schema"] != "1" or result["app_id"] != "yeopjari-cat" or result["channel"] != "stable":
        raise ReleaseError("지원하지 않는 앱 또는 배포 채널입니다.")
    version_tuple(result["version"])
    require_url(result["download_url"])
    if not re.fullmatch(r"[0-9a-f]{64}", result["sha256"]):
        raise ReleaseError("SHA-256 형식이 올바르지 않습니다.")
    if not re.fullmatch(r"[1-9][0-9]{0,8}", result["size_bytes"]):
        raise ReleaseError("실행 파일 크기 형식이 올바르지 않습니다.")
    if not MIN_EXE <= int(result["size_bytes"]) <= MAX_EXE:
        raise ReleaseError("실행 파일 크기가 허용 범위를 벗어났습니다.")
    try:
        decoded = base64.b64decode(result["notes_b64"], validate=True)
        if base64.b64encode(decoded).decode("ascii") != result["notes_b64"]:
            raise ValueError("noncanonical base64")
        result["notes"] = require_notes(decoded.decode("utf-8", errors="strict"))
    except (binascii.Error, UnicodeError, ValueError) as exc:
        raise ReleaseError("업데이트 내용 인코딩이 올바르지 않습니다.") from exc
    return result


def _slice(data: bytes, start: int, size: int) -> bytes:
    if start < 0 or size < 0 or start + size > len(data):
        raise ReleaseError("실행 파일 구조가 잘렸거나 올바르지 않습니다.")
    return data[start:start + size]


def _u16(data: bytes, start: int) -> int:
    return struct.unpack("<H", _slice(data, start, 2))[0]


def _u32(data: bytes, start: int) -> int:
    return struct.unpack("<I", _slice(data, start, 4))[0]


def _version_block(data: bytes, start: int, limit: int, depth: int = 0):
    if depth > 5 or start + 6 > limit:
        raise ReleaseError("실행 파일의 버전 리소스가 올바르지 않습니다.")
    length, value_length, kind = struct.unpack("<HHH", _slice(data, start, 6))
    end = start + length
    if length < 8 or end > limit or kind not in (0, 1):
        raise ReleaseError("실행 파일의 버전 블록 길이가 올바르지 않습니다.")
    position = start + 6
    key_start = position
    while position + 2 <= end and data[position:position + 2] != b"\0\0":
        position += 2
    if position + 2 > end:
        raise ReleaseError("버전 리소스 이름이 끝나지 않았습니다.")
    try:
        key = data[key_start:position].decode("utf-16le")
    except UnicodeError as exc:
        raise ReleaseError("버전 리소스 이름이 올바르지 않습니다.") from exc
    position = (position + 2 + 3) & ~3
    value_bytes = value_length * (2 if kind == 1 else 1)
    if position + value_bytes > end:
        raise ReleaseError("버전 리소스 값이 블록 밖에 있습니다.")
    value = data[position:position + value_bytes]
    position = (position + value_bytes + 3) & ~3
    children = []
    while position + 6 <= end:
        if not any(data[position:end]):
            break
        child = _version_block(data, position, end, depth + 1)
        children.append(child)
        position = (position + child[4] + 3) & ~3
    return key, kind, value, children, length


def inspect_exe(data: bytes, expected_version: str) -> dict[str, str]:
    """Validate PE32+ x64 GUI and real VERSIONINFO, without loading executable code."""
    expected = version_tuple(expected_version)
    if not MIN_EXE <= len(data) <= MAX_EXE or data[:2] != b"MZ":
        raise ReleaseError("유효한 Windows 실행 파일이 아닙니다.")
    pe = _u32(data, 0x3C)
    if _slice(data, pe, 4) != b"PE\0\0" or _u16(data, pe + 4) != 0x8664:
        raise ReleaseError("Windows x64 PE 실행 파일만 배포할 수 있습니다.")
    sections = _u16(data, pe + 6)
    optional_size = _u16(data, pe + 20)
    optional = pe + 24
    if not 1 <= sections <= 96 or optional_size < 136 or _u16(data, optional) != 0x20B:
        raise ReleaseError("PE32+ 헤더가 올바르지 않습니다.")
    _slice(data, optional, optional_size)
    if _u16(data, optional + 68) != 2 or _u32(data, optional + 108) < 3:
        raise ReleaseError("Windows GUI 실행 파일 또는 리소스 디렉터리를 찾지 못했습니다.")
    resource_rva, resource_size = struct.unpack("<II", _slice(data, optional + 128, 8))
    if not resource_rva or not resource_size:
        raise ReleaseError("실행 파일에 버전 리소스가 없습니다.")
    section_table = optional + optional_size
    section_rows = []
    for index in range(sections):
        row = section_table + index * 40
        _slice(data, row, 40)
        section_rows.append((_u32(data, row + 12), _u32(data, row + 16), _u32(data, row + 20)))

    def file_offset(rva: int, size: int) -> int:
        for start, raw_size, raw_start in section_rows:
            offset = rva - start
            if 0 <= offset and offset + size <= raw_size:
                _slice(data, raw_start + offset, size)
                return raw_start + offset
        raise ReleaseError("리소스 주소가 실행 파일의 저장된 섹션 안에 없습니다.")

    resource_start = file_offset(resource_rva, resource_size)
    resource_data = _slice(data, resource_start, resource_size)

    def entries(relative: int):
        _slice(resource_data, relative, 16)
        count = _u16(resource_data, relative + 12) + _u16(resource_data, relative + 14)
        if count > 1024:
            raise ReleaseError("리소스 디렉터리가 지나치게 큽니다.")
        return [struct.unpack("<II", _slice(resource_data, relative + 16 + i * 8, 8)) for i in range(count)]

    blobs = []
    for name, target in entries(0):
        if name != 16:
            continue
        if not target & 0x80000000:
            raise ReleaseError("버전 리소스 디렉터리가 올바르지 않습니다.")
        for _, name_target in entries(target & 0x7FFFFFFF):
            if not name_target & 0x80000000:
                raise ReleaseError("버전 리소스 이름 디렉터리가 올바르지 않습니다.")
            for _, leaf in entries(name_target & 0x7FFFFFFF):
                if leaf & 0x80000000:
                    raise ReleaseError("버전 리소스가 너무 깊습니다.")
                value_rva, size, _, _ = struct.unpack("<IIII", _slice(resource_data, leaf, 16))
                if not 52 <= size <= 65536 or len(blobs) >= 32:
                    raise ReleaseError("버전 리소스 크기 또는 개수가 올바르지 않습니다.")
                blobs.append(_slice(data, file_offset(value_rva, size), size))
    if not blobs:
        raise ReleaseError("실행 파일의 VERSIONINFO를 찾지 못했습니다.")
    canonical = None
    for blob in blobs:
        key, kind, value, children, _ = _version_block(blob, 0, len(blob))
        if key != "VS_VERSION_INFO" or kind != 0 or len(value) != 52:
            raise ReleaseError("실행 파일의 고정 버전 정보가 올바르지 않습니다.")
        fixed = struct.unpack("<13I", value)
        if fixed[0] != 0xFEEF04BD or fixed[1] != 0x10000 or fixed[9] != 1:
            raise ReleaseError("실행 파일의 제품 유형이 올바르지 않습니다.")
        numeric = (fixed[2] >> 16, fixed[2] & 65535, fixed[3] >> 16, fixed[3] & 65535)
        product = (fixed[4] >> 16, fixed[4] & 65535, fixed[5] >> 16, fixed[5] & 65535)
        if numeric != expected + (0,) or product != expected + (0,):
            raise ReleaseError(f"실행 파일의 실제 버전 {numeric}이 배포 버전 {expected_version}과 다릅니다. 먼저 새 버전으로 빌드하세요.")
        strings = {}
        for child in children:
            if child[0] == "StringFileInfo":
                for table in child[3]:
                    for entry in table[3]:
                        if entry[1] != 1 or len(entry[2]) % 2:
                            raise ReleaseError("문자열 버전 정보가 올바르지 않습니다.")
                        try:
                            text = entry[2].decode("utf-16le").rstrip("\0")
                        except UnicodeError as exc:
                            raise ReleaseError("문자열 버전 정보를 읽을 수 없습니다.") from exc
                        if "\0" in text or (entry[0] in strings and strings[entry[0]] != text):
                            raise ReleaseError("서로 다른 중복 제품 정보가 있습니다.")
                        strings[entry[0]] = text
        required = {"ProductName": "Yeopjari Cat", "OriginalFilename": "YeopjariCat.exe",
                    "FileVersion": expected_version, "ProductVersion": expected_version}
        if any(strings.get(key) != expected_value for key, expected_value in required.items()):
            raise ReleaseError("실행 파일의 제품 이름, 파일 이름 또는 버전 문자열이 옆자리 고양이와 일치하지 않습니다.")
        if canonical is not None and strings != canonical:
            raise ReleaseError("실행 파일의 언어별 제품 정보가 일치하지 않습니다.")
        canonical = strings
    return canonical or {}


def inspect_release_exe(data: bytes, expected_version: str) -> dict[str, str]:
    """Validate release identity and, from v1.6, the actual Korean dialog."""
    details = inspect_exe(data, expected_version)
    # Use the verified VERSIONINFO value. Older published packages and fixtures
    # remain verifiable, while every new release must contain correct UTF-16.
    if version_tuple(details["FileVersion"]) >= (1, 6, 0):
        spec = importlib.util.spec_from_file_location(
            "yeopjari_release_resources", Path(__file__).with_name("verify_resources.py"))
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        try:
            verifier.verify_update_dialog(data)
        except verifier.ResourceError as exc:
            raise ReleaseError("업데이트 창의 한글 리소스 검증에 실패했습니다: " + str(exc)) from exc
    return details


def github_repo(text: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9_.-]{1,100}", text):
        raise ReleaseError("--github에는 URL 대신 소유자/저장소 형식을 입력하세요.")
    owner, name = text.split("/")
    if owner.endswith("-") or name in (".", ".."):
        raise ReleaseError("GitHub 저장소 이름이 올바르지 않습니다.")
    return text


def _powershell_quote(text: str) -> str:
    return "'" + text.replace("'", "''") + "'"


def publisher_script(repo: str, version: str, hashes: dict[str, str]) -> str:
    """Draft first; resume only matching assets; never overwrite a published tag."""
    header = "\n".join((
        "# Run without -Publish to upload a draft; use -Publish to publish after verification.",
        "[CmdletBinding()] param([switch]$Publish)",
        "Set-StrictMode -Version Latest", "$ErrorActionPreference = 'Stop'",
        "$Repo = " + _powershell_quote(repo), "$RepoTarget = 'https://github.com/' + $Repo", "$Version = " + _powershell_quote(version),
        "$Tag = 'v' + $Version", "$Hashes = [ordered]@{",
        *["  " + _powershell_quote(name) + " = " + _powershell_quote(digest) for name, digest in hashes.items()],
        "}", ""))
    return header + r'''
function Invoke-GhJson([string[]]$Arguments) {
    $text = & gh @Arguments
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI failed. Check the repository and sign-in." }
    return (($text -join "`n") | ConvertFrom-Json)
}
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'Install GitHub CLI from https://cli.github.com/ and run gh auth login first.'
}
foreach ($name in $Hashes.Keys) {
    $path = Join-Path $PSScriptRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release file: $name" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hashes[$name]) {
        throw "Local file changed after preparation: $name. Prepare a fresh release."
    }
}
$repository = Invoke-GhJson -Arguments @('api', '--hostname', 'github.com', ('repos/' + $Repo))
if ($repository.private) { throw 'The updater requires a public download repository. This script will not change repository visibility.' }
if ($repository.archived) { throw 'This repository is archived.' }
# Repository role metadata can report push=false for a GitHub Actions
# installation token that has contents:write. The draft write below is the
# authoritative permission check; an API rejection still stops publication.
$releases = @(Invoke-GhJson -Arguments @('release', 'list', '--repo', $RepoTarget, '--limit', '1000', '--json', 'tagName,isDraft,isPrerelease'))
if ($releases.Count -ge 1000) { throw 'This repository has too many releases for this bounded publisher. Review the version history manually.' }
$existing = @($releases | Where-Object { $_.tagName -eq $Tag })
if ($existing.Count -gt 1) { throw 'Ambiguous existing release.' }
if ($existing.Count -eq 1 -and -not $existing[0].isDraft) {
    throw "Version $Tag is already published. Increase the version; published files will not be overwritten."
}
if ($existing.Count -eq 1 -and $existing[0].isPrerelease) {
    throw "The existing $Tag draft is a prerelease. Review it and use a stable draft before publishing stable updates."
}
foreach ($release in $releases) {
    if (-not $release.isDraft -and -not $release.isPrerelease) {
        if ($release.tagName -notmatch '^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
            throw "Stable release tag $($release.tagName) is not a three-part version. Review it before using this publisher."
        }
        $publishedVersion = [version]($release.tagName -replace '^v', '')
        if ($publishedVersion -ge [version]$Version) { throw "Cannot publish $Version over stable version $publishedVersion." }
    }
}
if ($existing.Count -eq 0) {
    & gh release create $Tag --repo $RepoTarget --draft --title ('Yeopjari Cat ' + $Version) --notes-file (Join-Path $PSScriptRoot 'RELEASE_NOTES_KO.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Draft creation failed. No release was published.' }
}
$remote = Invoke-GhJson -Arguments @('release', 'view', $Tag, '--repo', $RepoTarget, '--json', 'isDraft,isPrerelease,assets')
if (-not $remote.isDraft) { throw 'Release is no longer a draft; stopping.' }
if ($remote.isPrerelease) { throw 'Release is a prerelease; stopping before uploads to the stable channel.' }
foreach ($asset in @($remote.assets)) {
    if (-not $Hashes.Contains($asset.name)) { throw "Unexpected draft asset: $($asset.name). Review the draft before continuing." }
}
& gh release edit $Tag --repo $RepoTarget --notes-file (Join-Path $PSScriptRoot 'RELEASE_NOTES_KO.txt')
if ($LASTEXITCODE -ne 0) { throw 'Cannot write draft release notes. Check publishing permission; no assets were uploaded.' }
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('Yeopjari-release-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temporary)
try {
    foreach ($name in $Hashes.Keys) {
        if (-not @($remote.assets | Where-Object { $_.name -eq $name }).Count) {
            & gh release upload $Tag (Join-Path $PSScriptRoot $name) --repo $RepoTarget
            if ($LASTEXITCODE -ne 0) { throw 'Upload failed. The release remains a draft. Re-run to resume.' }
        }
        & gh release download $Tag --repo $RepoTarget --dir $temporary --pattern $name
        if ($LASTEXITCODE -ne 0) { throw "Could not verify uploaded asset: $name. The release remains a draft." }
        $downloaded = Join-Path $temporary $name
        if ((Get-FileHash -LiteralPath $downloaded -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hashes[$name]) {
            throw "Draft asset differs: $name. It was not overwritten. Review or remove that draft asset first."
        }
    }
    if ($Publish) {
        & gh release edit $Tag --repo $RepoTarget --draft=false --prerelease=false --latest
        if ($LASTEXITCODE -ne 0) { throw 'Publishing failed. Inspect the draft before retrying.' }
        Write-Host "Published $Tag. Existing clients can discover it through latest.txt."
    } else {
        Write-Host "Uploaded and verified draft $Tag. Run this script again with -Publish after reviewing it."
    }
    & gh release view $Tag --repo $RepoTarget --json url --jq '.url'
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
'''


def prepare_release(*, root: Path, exe_path: Path, version: str, feed_url: str,
                    download_base_url: str, notes: str, output: Path,
                    repo: str | None = None, previous_manifest: Path | None = None) -> dict:
    version_tuple(version)
    require_feed_url(feed_url)
    require_url(download_base_url)
    if urlsplit(download_base_url).query:
        raise ReleaseError("--download-base-url에는 쿼리 문자열을 넣을 수 없습니다.")
    if repo is not None:
        github_repo(repo)
    require_notes(notes)
    if previous_manifest is not None:
        previous = parse_manifest(previous_manifest.read_bytes())
        if version_tuple(version) <= version_tuple(previous["version"]):
            raise ReleaseError("기존 배포보다 큰 버전을 지정하세요. 같은 버전이나 이전 버전으로 배포할 수 없습니다.")
    if output.exists():
        raise ReleaseError("출력 폴더가 이미 있습니다. 기존 릴리스를 덮어쓰지 않고 멈췄습니다. 새 출력 폴더를 지정하세요.")
    if not MIN_EXE <= exe_path.stat().st_size <= MAX_EXE:
        raise ReleaseError("실행 파일 크기가 허용 범위를 벗어났습니다.")
    exe = exe_path.read_bytes()
    inspect_release_exe(exe, version)
    readme = (root / "README_KO.txt").read_bytes()
    notices = (root / "release" / "THIRD_PARTY_NOTICES.txt").read_bytes()
    asset_name = f"YeopjariCat-{version}.exe"
    zip_name = f"YeopjariCat-Windows-{version}.zip"
    download_url = require_url(download_base_url.rstrip("/") + "/" + asset_name)
    manifest = make_manifest(version, download_url, exe, notes)
    distribution = ("옆자리 고양이 업데이트 안내\n\n"
                    "ZIP 압축을 완전히 풀고 YeopjariCat.exe를 실행하세요.\n"
                    "update-feed.txt를 실행 파일과 같은 폴더에 그대로 두세요.\n"
                    "고양이 또는 알림 영역 아이콘 우클릭 → 업데이트 확인에서 새 버전을 받을 수 있습니다.\n"
                    "자동 확인은 새 버전을 알리며, 적용은 사용자가 선택합니다.\n"
                    "1.2 이하 사용자는 기존 고양이를 종료하고 이번 ZIP을 한 번 수동으로 받아 실행하세요.\n"
                    "생일·성장·캐릭터 선택 기록은 %LOCALAPPDATA%\\YeopjariCat에 계속 보관됩니다.\n"
                    "받는 사람에게 Python이나 GitHub 계정은 필요하지 않습니다.\n\n"
                    f"배포 버전: {version}\n업데이트 확인 주소: {feed_url}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".yeopjari-stage-", dir=output.parent))
    try:
        (temporary / asset_name).write_bytes(exe)
        (temporary / "RELEASE_NOTES_KO.txt").write_text(notes, encoding="utf-8", newline="\n")
        with zipfile.ZipFile(temporary / zip_name, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, content in (("YeopjariCat.exe", exe), ("update-feed.txt", (feed_url + "\n").encode("ascii")),
                                  ("README_KO.txt", readme), ("CHANGELOG_KO.txt", notes.encode("utf-8")),
                                  ("THIRD_PARTY_NOTICES.txt", notices), ("DISTRIBUTION_KO.txt", distribution.encode("utf-8"))):
                archive.writestr(name, content)
        # Publishable manifest is generated only after all download assets exist.
        (temporary / "latest.txt").write_bytes(manifest)
        hash_names = [asset_name, zip_name, "latest.txt", "RELEASE_NOTES_KO.txt"]
        hashes = {name: hashlib.sha256((temporary / name).read_bytes()).hexdigest() for name in hash_names}
        checksums = "".join(f"{digest}  {name}\n" for name, digest in hashes.items())
        (temporary / "SHA256SUMS.txt").write_text(checksums, encoding="ascii", newline="\n")
        hashes["SHA256SUMS.txt"] = hashlib.sha256(checksums.encode("ascii")).hexdigest()
        info = {"app_id": "yeopjari-cat", "version": version, "feed_url": feed_url,
                "download_url": download_url, "github_repository": repo, "files": hashes}
        (temporary / "release-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if repo:
            (temporary / "publish-github.ps1").write_text(publisher_script(repo, version, hashes), encoding="utf-8-sig", newline="\r\n")
        verify_release(temporary)
        os.rename(temporary, output)
        return info
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def verify_release(directory: Path) -> dict:
    manifest = parse_manifest((directory / "latest.txt").read_bytes())
    version = manifest["version"]
    asset_name = f"YeopjariCat-{version}.exe"
    zip_name = f"YeopjariCat-Windows-{version}.zip"
    if urlsplit(manifest["download_url"]).path.rsplit("/", 1)[-1] != asset_name:
        raise ReleaseError("다운로드 주소의 파일 이름이 배포 버전과 다릅니다.")
    path = directory / asset_name
    if path.stat().st_size != int(manifest["size_bytes"]):
        raise ReleaseError("실행 파일 크기가 업데이트 안내와 다릅니다.")
    exe = path.read_bytes()
    if hashlib.sha256(exe).hexdigest() != manifest["sha256"]:
        raise ReleaseError("실행 파일 SHA-256이 업데이트 안내와 다릅니다.")
    inspect_release_exe(exe, version)
    info = json.loads((directory / "release-info.json").read_text(encoding="utf-8"))
    if info.get("app_id") != "yeopjari-cat" or info.get("version") != version or info.get("download_url") != manifest["download_url"]:
        raise ReleaseError("배포 정보가 업데이트 안내와 다릅니다.")
    require_feed_url(info["feed_url"])
    expected_names = {asset_name, zip_name, "latest.txt", "RELEASE_NOTES_KO.txt", "SHA256SUMS.txt"}
    if set(info["files"]) != expected_names:
        raise ReleaseError("배포 파일 목록이 올바르지 않습니다.")
    for name, digest in info["files"].items():
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ReleaseError(f"배포 파일이 변경되었습니다: {name}")
    if (directory / "RELEASE_NOTES_KO.txt").read_bytes().decode("utf-8") != manifest["notes"]:
        raise ReleaseError("업데이트 내용이 안내 파일과 다릅니다.")
    sums = "".join(f"{info['files'][name]}  {name}\n" for name in (asset_name, zip_name, "latest.txt", "RELEASE_NOTES_KO.txt"))
    if (directory / "SHA256SUMS.txt").read_bytes() != sums.encode("ascii"):
        raise ReleaseError("체크섬 목록이 배포 정보와 다릅니다.")
    with zipfile.ZipFile(directory / zip_name) as archive:
        expected_zip = {"YeopjariCat.exe", "update-feed.txt", "README_KO.txt", "CHANGELOG_KO.txt", "THIRD_PARTY_NOTICES.txt", "DISTRIBUTION_KO.txt"}
        if set(archive.namelist()) != expected_zip or len(archive.namelist()) != len(expected_zip):
            raise ReleaseError("사용자 ZIP의 파일 목록 또는 무결성이 올바르지 않습니다.")
        limits = {"YeopjariCat.exe": MAX_EXE, "update-feed.txt": MAX_MANIFEST, "README_KO.txt": 1024 * 1024,
                  "CHANGELOG_KO.txt": MAX_NOTES, "THIRD_PARTY_NOTICES.txt": 4 * 1024 * 1024, "DISTRIBUTION_KO.txt": MAX_MANIFEST}
        if any(entry.file_size > limits[entry.filename] for entry in archive.infolist()) or archive.testzip() is not None:
            raise ReleaseError("사용자 ZIP의 파일 크기 또는 무결성이 올바르지 않습니다.")
        if archive.read("YeopjariCat.exe") != exe:
            raise ReleaseError("사용자 ZIP의 실행 파일이 업데이트용 실행 파일과 다릅니다.")
        if archive.read("update-feed.txt") != (info["feed_url"] + "\n").encode("ascii"):
            raise ReleaseError("사용자 ZIP의 업데이트 주소가 다릅니다.")
        if archive.read("CHANGELOG_KO.txt") != manifest["notes"].encode("utf-8"):
            raise ReleaseError("사용자 ZIP의 업데이트 내용이 다릅니다.")
    return info


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="옆자리 고양이 사용자 배포본 준비 및 검증. 공개 업로드는 수행하지 않습니다.")
    parser.add_argument("--exe", type=Path, default=default_executable(ROOT))
    parser.add_argument("--version", help="생략하면 src/app_version.h에서 읽습니다.")
    parser.add_argument("--github", help="공개 다운로드용 저장소: 소유자/저장소")
    parser.add_argument("--feed-url", help="직접 호스팅할 latest.txt의 고정 HTTPS 주소")
    parser.add_argument("--download-base-url", help="이번 버전의 EXE를 올릴 HTTPS 폴더 주소")
    parser.add_argument("--notes-file", type=Path, default=ROOT / "RELEASE_NOTES_KO.txt")
    parser.add_argument("--output", type=Path, help="새 출력 폴더. 기본값: release/publish/v버전")
    parser.add_argument("--previous-manifest", type=Path, help="이전 latest.txt와 비교해 버전 증가를 검사합니다.")
    parser.add_argument("--verify", type=Path, metavar="DIRECTORY", help="이미 준비한 배포 폴더의 모든 파일을 검증합니다.")
    args = parser.parse_args(argv)
    try:
        if args.verify:
            info = verify_release(args.verify)
            print(f"검증 완료: 옆자리 고양이 {info['version']} / {args.verify.resolve()}")
            return 0
        version = args.version or version_from_header(ROOT / "src" / "app_version.h")
        version_tuple(version)
        if args.github:
            if args.feed_url or args.download_base_url:
                raise ReleaseError("--github와 직접 호스팅 주소 옵션을 함께 사용할 수 없습니다.")
            repo = github_repo(args.github)
            feed = f"https://github.com/{repo}/releases/latest/download/latest.txt"
            base = f"https://github.com/{repo}/releases/download/v{version}"
        else:
            if not args.feed_url or not args.download_base_url:
                raise ReleaseError("--github 소유자/저장소 또는 --feed-url과 --download-base-url을 지정하세요.")
            repo, feed, base = None, args.feed_url, args.download_base_url
        notes = args.notes_file.read_text(encoding="utf-8-sig")
        output = args.output or ROOT / "release" / "publish" / ("v" + version)
        info = prepare_release(root=ROOT, exe_path=args.exe, version=version, feed_url=feed,
                               download_base_url=base, notes=notes, output=output,
                               repo=repo, previous_manifest=args.previous_manifest)
        print(f"배포 파일 준비 완료: {output.resolve()}")
        print(f"사용자 ZIP: YeopjariCat-Windows-{info['version']}.zip")
        print("아직 공개 배포하지 않았습니다.")
        if repo:
            print("publish-github.ps1 실행 → 검증된 초안 업로드; 같은 스크립트에 -Publish 지정 → 공개 배포")
        else:
            print("버전별 EXE를 먼저 업로드하고, 검증 후 고정 주소의 latest.txt를 마지막에 교체하세요.")
        return 0
    except (ReleaseError, OSError, UnicodeError, zipfile.BadZipFile, json.JSONDecodeError, KeyError, TypeError) as exc:
        print(f"배포 준비 중단: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
