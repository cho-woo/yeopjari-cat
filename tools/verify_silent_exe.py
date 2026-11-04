#!/usr/bin/env python3
"""Reject audio resources and playback imports in an actual AMD64 PE32+ file.

This reads the import, delay-import and resource directories without executing
the program. Version gating belongs to release.inspect_release_exe: historical
sound-enabled releases remain verifiable, while v2.0.1 onward must be silent.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys


class SilentError(ValueError):
    pass


def read(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise SilentError("Truncated or out-of-range PE data")
    return data[offset:offset + size]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack("<H", read(data, offset, 2))[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack("<I", read(data, offset, 4))[0]


class PE:
    def __init__(self, data: bytes):
        self.data = data
        if not 64 <= len(data) <= 128 * 1024 * 1024 or data[:2] != b"MZ":
            raise SilentError("Expected a bounded Windows executable")
        pe = u32(data, 0x3C)
        if read(data, pe, 4) != b"PE\0\0" or u16(data, pe + 4) != 0x8664:
            raise SilentError("Expected an AMD64 PE signature")
        count, size = u16(data, pe + 6), u16(data, pe + 20)
        self.optional = pe + 24
        read(data, self.optional, size)
        if not 1 <= count <= 96 or size < 136 or u16(data, self.optional) != 0x20B:
            raise SilentError("Invalid PE32+ header")
        self.directory_count = u32(data, self.optional + 108)
        if not 3 <= self.directory_count <= 16 or 112 + self.directory_count * 8 > size:
            raise SilentError("Invalid PE data-directory count")
        self.image_base = struct.unpack("<Q", read(data, self.optional + 24, 8))[0]
        self.sections = []
        for i in range(count):
            row = self.optional + size + i * 40
            read(data, row, 40)
            start, length, raw = u32(data, row + 12), u32(data, row + 16), u32(data, row + 20)
            read(data, raw, length)
            self.sections.append((start, length, raw))

    def offset(self, rva: int, size: int) -> int:
        offsets = []
        for start, length, raw in self.sections:
            relative = rva - start
            if 0 <= relative <= length and size <= length - relative:
                read(self.data, raw + relative, size)
                offsets.append(raw + relative)
        if len(offsets) != 1:
            raise SilentError("RVA is outside a unique stored PE section")
        return offsets[0]

    def directory(self, index: int) -> tuple[int, int]:
        if index >= self.directory_count:
            return 0, 0
        rva, size = struct.unpack("<II", read(self.data, self.optional + 112 + index * 8, 8))
        if bool(rva) != bool(size):
            raise SilentError("Incomplete PE directory")
        if rva:
            self.offset(rva, size)
        return rva, size

    def text(self, rva: int) -> str:
        result = bytearray()
        for i in range(512):
            value = self.data[self.offset(rva + i, 1)]
            if not value:
                try:
                    return result.decode("ascii")
                except UnicodeError as exc:
                    raise SilentError("Invalid import name") from exc
            if value < 32 or value > 126:
                raise SilentError("Invalid import name")
            result.append(value)
        raise SilentError("Unterminated import name")


def audio_dll(name: str) -> bool:
    name = name.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return name in {"winmmbase.dll", "dsound.dll", "openal32.dll",
                    "mmdevapi.dll", "audioses.dll", "audioclient.dll", "msacm32.dll",
                    "avrt.dll", "wmp.dll", "wmploc.dll", "mf.dll", "mfplat.dll",
                    "mfreadwrite.dll", "windows.media.audio.dll"} or name.startswith(
                        ("xaudio2", "xactengine", "api-ms-win-mm-", "api-ms-win-core-beep-"))


def audio_function(name: str) -> bool:
    name = name.lower()
    return name in {"beep", "messagebeep"} or name.startswith(
        ("playsound", "sndplaysound", "waveout", "midiout", "mcisend", "mciexecute",
         "directsoundcreate", "xaudio2create", "createaudioreverb", "createaudiovolumemeter"))


def import_names(pe: PE, delay: bool) -> list[dict]:
    rva, size = pe.directory(13 if delay else 1)
    if not rva:
        return []
    width = 32 if delay else 20
    result = []
    for index in range(min(1024, size // width)):
        values = struct.unpack("<" + "I" * (width // 4),
                               read(pe.data, pe.offset(rva + index * width, width), width))
        if not any(values):
            return result
        if delay:
            attributes = values[0]
            if attributes & ~1:
                raise SilentError("Unsupported delay-import attributes")
            def pointer(value):
                if not value or attributes & 1:
                    return value
                if value < pe.image_base:
                    raise SilentError("Invalid delay-import virtual address")
                return value - pe.image_base
            name_rva, thunk = pointer(values[1]), pointer(values[4] or values[3])
        else:
            name_rva, thunk = values[3], values[0] or values[4]
        if not name_rva or not thunk:
            raise SilentError("Missing import name or thunk table")
        dll = pe.text(name_rva)
        if not dll or audio_dll(dll):
            raise SilentError("Audio playback DLL import: " + dll)
        timer_library = dll.replace("\\", "/").rsplit("/", 1)[-1].lower() == "winmm.dll"
        symbols = []
        for entry in range(8192):
            value = struct.unpack("<Q", read(pe.data, pe.offset(thunk + entry * 8, 8), 8))[0]
            if not value:
                break
            if value & (1 << 63):
                if value & ~((1 << 63) | 0xFFFF):
                    raise SilentError("Invalid ordinal import")
                if timer_library:
                    raise SilentError("Unverifiable ordinal import from winmm.dll")
                continue
            if value > 0xFFFFFFFF:
                raise SilentError("Invalid import name RVA")
            symbol = pe.text(value + 2)
            if not symbol or audio_function(symbol):
                raise SilentError("Audio playback function import: " + symbol)
            if timer_library and symbol not in {"timeBeginPeriod", "timeEndPeriod"}:
                raise SilentError("Only animation timer resolution imports are allowed from winmm.dll: " + symbol)
            symbols.append(symbol)
        else:
            raise SilentError("Unterminated import thunk table")
        result.append({"dll": dll, "symbols": symbols, "delay": delay})
    raise SilentError("Unterminated import descriptor table")


def resource_leaves(pe: PE) -> list[tuple[object, object, bytes]]:
    rva, size = pe.directory(2)
    if not rva:
        raise SilentError("Missing resource directory")
    resource = read(pe.data, pe.offset(rva, size), size)

    def key(value):
        if not value & 0x80000000:
            return value
        at = value & 0x7FFFFFFF
        length = u16(resource, at)
        if not 1 <= length <= 512:
            raise SilentError("Invalid resource name length")
        try:
            return read(resource, at + 2, length * 2).decode("utf-16le")
        except UnicodeError as exc:
            raise SilentError("Invalid Unicode resource name") from exc

    def entries(at):
        read(resource, at, 16)
        count = u16(resource, at + 12) + u16(resource, at + 14)
        if count > 1024:
            raise SilentError("Excessive resource entries")
        rows = [struct.unpack("<II", read(resource, at + 16 + i * 8, 8)) for i in range(count)]
        names = [key(name) for name, _ in rows]
        if len(set(names)) != len(names):
            raise SilentError("Duplicate resource key")
        return list(zip(names, [target for _, target in rows]))

    result = []
    for kind, kinds in entries(0):
        if not kinds & 0x80000000:
            raise SilentError("Invalid resource type directory")
        for name, names in entries(kinds & 0x7FFFFFFF):
            if not names & 0x80000000:
                raise SilentError("Invalid resource name directory")
            for _, leaf in entries(names & 0x7FFFFFFF):
                if leaf & 0x80000000:
                    raise SilentError("Resource tree exceeds language depth")
                value_rva, length, _, _ = struct.unpack("<IIII", read(resource, leaf, 16))
                if len(result) >= 4096:
                    raise SilentError("Excessive resource leaves")
                result.append((kind, name, read(pe.data, pe.offset(value_rva, length), length)))
    return result


def verify_silent_exe(data: bytes) -> dict:
    pe = PE(data)
    imports = import_names(pe, False) + import_names(pe, True)
    leaves = resource_leaves(pe)
    for kind, name, payload in leaves:
        if (isinstance(kind, str) and kind.upper() in {"WAVE", "WAV", "MP3", "OGG", "AUDIO", "SOUND", "MIDI", "WMA"}) or (
                kind == 10 and isinstance(name, int) and 301 <= name <= 314):
            raise SilentError(f"Retired audio resource: {kind}/{name}")
        wave = payload[:4] in {b"RIFF", b"RF64"} and payload[8:12] == b"WAVE"
        aiff = payload[:4] == b"FORM" and payload[8:12] in {b"AIFF", b"AIFC"}
        if wave or aiff or payload.startswith((b"OggS", b"fLaC", b"ID3", b"MThd")):
            raise SilentError(f"Audio payload in resource: {kind}/{name}")
    return {"passed": True, "audio_resource_count": 0, "playback_import_count": 0,
            "resource_leaves_checked": len(leaves), "import_dlls_checked": len(imports),
            "delay_import_dlls_checked": sum(row["delay"] for row in imports)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    try:
        report = verify_silent_exe(args.executable.read_bytes())
    except (OSError, SilentError) as exc:
        print("Silent-executable verification failed: " + str(exc), file=sys.stderr)
        return 1
    print(json.dumps(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
