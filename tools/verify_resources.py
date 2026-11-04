#!/usr/bin/env python3
"""Check the actual Unicode update-dialog resource in a built Windows EXE.

This never loads or executes the program. It follows the PE resource directory
and Microsoft's DLGTEMPLATEEX/DLGITEMTEMPLATEEX layouts, instead of searching
for unrelated byte strings that might also occur in application code.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys


class ResourceError(ValueError):
    pass


EXPECTED_TITLE = "옆자리 고양이 업데이트"
EXPECTED_CONTROLS = {
    2010: "새 버전이 도착했어요",
    2013: "다운로드를 확인한 뒤 고양이가 잠깐 재시작돼요. 생일과 돌봄 기록은 이어집니다.",
    1: "업데이트",
    2: "나중에",
}


def _read(data: bytes, offset: int, length: int) -> bytes:
    if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
        raise ResourceError("Truncated or out-of-range resource data")
    return data[offset:offset + length]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack("<H", _read(data, offset, 2))[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack("<I", _read(data, offset, 4))[0]


def dialog_resources(executable: bytes) -> list[tuple[int, bytes]]:
    """Return every language variant of RT_DIALOG / 201 from an AMD64 PE32+."""
    if not 64 <= len(executable) <= 128 * 1024 * 1024 or executable[:2] != b"MZ":
        raise ResourceError("Expected a bounded Windows executable")
    pe = _u32(executable, 0x3C)
    if _read(executable, pe, 4) != b"PE\0\0" or _u16(executable, pe + 4) != 0x8664:
        raise ResourceError("Expected an AMD64 PE signature")
    count, optional_size = _u16(executable, pe + 6), _u16(executable, pe + 20)
    optional = pe + 24
    if not 1 <= count <= 96 or optional_size < 136:
        raise ResourceError("Invalid PE section or optional-header count")
    _read(executable, optional, optional_size)
    if _u16(executable, optional) != 0x20B or _u32(executable, optional + 108) < 3:
        raise ResourceError("Expected PE32+ with a resource directory")
    resource_rva, resource_size = struct.unpack("<II", _read(executable, optional + 128, 8))
    if not resource_rva or not 16 <= resource_size <= 128 * 1024 * 1024:
        raise ResourceError("Missing or invalid PE resource directory")
    sections = []
    for index in range(count):
        row = optional + optional_size + index * 40
        _read(executable, row, 40)
        sections.append((_u32(executable, row + 12), _u32(executable, row + 16), _u32(executable, row + 20)))

    def file_offset(rva: int, size: int) -> int:
        matches = []
        for start, raw_size, raw_start in sections:
            relative = rva - start
            if 0 <= relative <= raw_size and size <= raw_size - relative:
                _read(executable, raw_start + relative, size)
                matches.append(raw_start + relative)
        if len(matches) != 1:
            raise ResourceError("Resource RVA is outside a unique stored PE section")
        return matches[0]

    resource = _read(executable, file_offset(resource_rva, resource_size), resource_size)

    def entries(relative: int) -> list[tuple[int, int]]:
        _read(resource, relative, 16)
        size = _u16(resource, relative + 12) + _u16(resource, relative + 14)
        if size > 1024:
            raise ResourceError("Oversized resource directory")
        return [struct.unpack("<II", _read(resource, relative + 16 + index * 8, 8)) for index in range(size)]

    def directory(parent: int, identifier: int) -> int:
        matches = [value for key, value in entries(parent) if key == identifier]
        if len(matches) != 1 or not matches[0] & 0x80000000:
            raise ResourceError(f"Missing, duplicate or invalid resource directory {identifier}")
        return matches[0] & 0x7FFFFFFF

    languages = entries(directory(directory(0, 5), 201))
    if not 1 <= len(languages) <= 32 or len({lang for lang, _ in languages}) != len(languages):
        raise ResourceError("Missing, duplicate or excessive dialog languages")
    result = []
    for language, leaf in languages:
        if language & 0x80000000 or leaf & 0x80000000:
            raise ResourceError("Invalid dialog language leaf")
        rva, size, _, _ = struct.unpack("<IIII", _read(resource, leaf, 16))
        if not 26 <= size <= 65536:
            raise ResourceError("Invalid dialog resource size")
        result.append((language, _read(executable, file_offset(rva, size), size)))
    return result


def parse_dialog(data: bytes) -> dict:
    if len(data) < 26 or _u16(data, 0) != 1 or _u16(data, 2) != 0xFFFF:
        raise ResourceError("Expected an extended Unicode dialog template")
    style, count = _u32(data, 12), _u16(data, 16)
    if count > 128:
        raise ResourceError("Excessive dialog control count")
    position = 26

    def text_or_ordinal():
        nonlocal position
        first = _u16(data, position)
        position += 2
        if first == 0:
            return ""
        if first == 0xFFFF:
            value = _u16(data, position)
            position += 2
            return value
        start = position - 2
        while _u16(data, position) != 0:
            position += 2
        encoded = _read(data, start, position - start)
        position += 2
        try:
            return encoded.decode("utf-16le", errors="strict")
        except UnicodeError as exc:
            raise ResourceError("Invalid UTF-16 dialog text") from exc

    text_or_ordinal()  # menu
    text_or_ordinal()  # window class
    title = text_or_ordinal()
    if not isinstance(title, str):
        raise ResourceError("Dialog caption must be Unicode text")
    font = None
    if style & 0x40:  # DS_SETFONT (also included by DS_SHELLFONT)
        _read(data, position, 6)  # pointsize, weight, italic, charset
        position += 6
        font = text_or_ordinal()
        if not isinstance(font, str):
            raise ResourceError("Dialog font must be text")
    controls = {}
    for _ in range(count):
        position = (position + 3) & ~3
        _read(data, position, 24)
        identifier = _u32(data, position + 20)
        position += 24
        class_name = text_or_ordinal()
        control_text = text_or_ordinal()
        extra = _u16(data, position)
        position += 2
        _read(data, position, extra)
        position += extra
        if identifier in controls:
            raise ResourceError(f"Duplicate dialog control ID {identifier}")
        controls[identifier] = {"text": control_text, "class": class_name}
    if len(data) - position > 3 or any(data[position:]):
        raise ResourceError("Unexpected trailing dialog data")
    return {"title": title, "font": font, "controls": controls}


def verify_update_dialog(executable: bytes) -> dict:
    inspected = []
    for language, data in dialog_resources(executable):
        dialog = parse_dialog(data)
        if dialog["title"] != EXPECTED_TITLE:
            raise ResourceError(f"Update dialog caption is not correct Korean: {dialog['title']!r}")
        for identifier, expected in EXPECTED_CONTROLS.items():
            actual = dialog["controls"].get(identifier, {}).get("text")
            if actual != expected:
                raise ResourceError(f"Update dialog control {identifier} is not correct Korean: {actual!r}")
        inspected.append(language)
    return {"resource_type": "RT_DIALOG", "resource_id": 201, "languages": inspected,
            "korean_strings_verified": 1 + len(EXPECTED_CONTROLS), "passed": True}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    try:
        report = verify_update_dialog(args.executable.read_bytes())
    except (OSError, ResourceError) as exc:
        print("Update-dialog Unicode verification failed: " + str(exc), file=sys.stderr)
        return 1
    print(json.dumps(report, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
