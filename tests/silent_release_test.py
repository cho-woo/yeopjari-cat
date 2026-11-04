#!/usr/bin/env python3
"""Exercise actual PE import/resource tables and the historical version gate."""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


silent = module("silent_executable", "tools/verify_silent_exe.py")
fixture = module("silent_version_fixture", "tests/release_tool_test.py")


def executable(version="2.0.1", extra=(), imports=(), delayed=()):
    base = fixture.executable(version, update_dialog=fixture.dialog_fixtures.dialog())
    # Recover the two known fixture blobs without calling the checker under test.
    original = []
    for i in range(2):
        directory = 32 + i * 64
        kind = struct.unpack_from("<I", base, 512 + 16 + i * 8)[0]
        name = struct.unpack_from("<I", base, 512 + directory + 16)[0]
        rva, size = struct.unpack_from("<II", base, 512 + directory + 48)
        original.append((kind, name, base[512 + rva - 0x1000:512 + rva - 0x1000 + size]))
    groups = {}
    for kind, name, payload in original + list(extra):
        groups.setdefault(kind, []).append((name, payload))
    kinds = sorted(groups, key=lambda key: (not isinstance(key, str), str(key)))
    resource = bytearray(16 + 8 * len(kinds))

    def append(payload, alignment=4):
        resource.extend(b"\0" * (-len(resource) % alignment))
        at = len(resource)
        resource.extend(payload)
        return at

    def encoded(key):
        if isinstance(key, int):
            return key
        return 0x80000000 | append(struct.pack("<H", len(key)) + key.encode("utf-16le"), 2)

    struct.pack_into("<HH", resource, 12, sum(isinstance(k, str) for k in kinds), sum(isinstance(k, int) for k in kinds))
    for index, kind in enumerate(kinds):
        names = groups[kind]
        directory = append(bytes(16 + 8 * len(names)))
        kind_key = encoded(kind)
        struct.pack_into("<II", resource, 16 + index * 8, kind_key, 0x80000000 | directory)
        struct.pack_into("<HH", resource, directory + 12,
                         sum(isinstance(n, str) for n, _ in names), sum(isinstance(n, int) for n, _ in names))
        for entry, (name, payload) in enumerate(names):
            language = append(bytes(24))
            leaf = append(bytes(16))
            name_key = encoded(name)
            struct.pack_into("<II", resource, directory + 16 + entry * 8, name_key, 0x80000000 | language)
            struct.pack_into("<HII", resource, language + 14, 1, 0x409, leaf)
            payload_at = append(payload)
            struct.pack_into("<IIII", resource, leaf, 0x1000 + payload_at, len(payload), 0, 0)
    resource_size = len(resource)
    header = bytearray(base[:512])
    for entries, delayed_flag in [(imports, False), (delayed, True)]:
        if not entries:
            continue
        width = 32 if delayed_flag else 20
        descriptors = append(bytes(width * (len(entries) + 1)), 8)
        for i, (dll, functions) in enumerate(entries):
            dll_at = append(dll.encode("ascii") + b"\0")
            thunks = append(bytes((len(functions) + 1) * 8), 8)
            for j, function in enumerate(functions):
                value = ((1 << 63) | function) if isinstance(function, int) else 0x1000 + append(b"\0\0" + function.encode("ascii") + b"\0", 2)
                struct.pack_into("<Q", resource, thunks + j * 8, value)
            values = (1, 0x1000 + dll_at, 0, 0x1000 + thunks, 0x1000 + thunks, 0, 0, 0) if delayed_flag else (
                0x1000 + thunks, 0, 0, 0x1000 + dll_at, 0x1000 + thunks)
            struct.pack_into("<" + "I" * len(values), resource, descriptors + i * width, *values)
        struct.pack_into("<II", header, 0x98 + 112 + (13 if delayed_flag else 1) * 8,
                         0x1000 + descriptors, width * (len(entries) + 1))
    resource.extend(b"\0" * (-len(resource) % 512))
    struct.pack_into("<II", header, 0x98 + 128, 0x1000, resource_size)
    struct.pack_into("<IIII", header, 0x98 + 240 + 8, len(resource), 0x1000, len(resource), 512)
    return bytes(header + resource)


class SilentReleaseTests(unittest.TestCase):
    def test_timer_resolution_imports_remain_available_without_playback(self):
        data = executable(imports=[("KERNEL32.dll", ["GetTickCount64"]),
                                   ("WINMM.dll", ["timeBeginPeriod", "timeEndPeriod"])])
        report = silent.verify_silent_exe(data)
        self.assertEqual(report["playback_import_count"], 0)
        self.assertEqual(report["import_dlls_checked"], 2)
        self.assertEqual(fixture.release.inspect_release_exe(data, "2.0.1")["FileVersion"], "2.0.1")
        delayed = executable(delayed=[("winmm.dll", ["timeBeginPeriod", "timeEndPeriod"])])
        self.assertEqual(silent.verify_silent_exe(delayed)["delay_import_dlls_checked"], 1)

    def test_all_retired_cue_resource_ids_are_rejected_even_without_wav_magic(self):
        for identifier in range(301, 315):
            with self.subTest(identifier=identifier), self.assertRaisesRegex(silent.SilentError, "audio resource"):
                silent.verify_silent_exe(executable(extra=[(10, identifier, b"unrecognised payload")]))

    def test_renamed_audio_payloads_and_custom_types_cannot_bypass_resource_check(self):
        for payload in [b"RIFF\x04\0\0\0WAVE", b"RF64\xff\xff\xff\xffWAVE", b"OggS\0", b"ID3\x04\0", b"fLaC", b"MThd", b"FORM\0\0\0\4AIFF"]:
            with self.subTest(payload=payload), self.assertRaisesRegex(silent.SilentError, "Audio payload"):
                silent.verify_silent_exe(executable(extra=[(10, "renamed", payload)]))
        for kind in ["WAVE", "audio", "MP3", "OGG", "SOUND"]:
            with self.subTest(kind=kind), self.assertRaisesRegex(silent.SilentError, "audio resource"):
                silent.verify_silent_exe(executable(extra=[(kind, 1, b"arbitrary")]))

    def test_playback_imports_rejected_in_normal_and_delay_tables(self):
        imports = [("winmm.dll", ["waveOutOpen"]), ("winmm.dll", ["PlaySoundW"]),
                   ("winmm.dll", ["sndPlaySoundA"]), ("winmm.dll", ["mciSendStringW"]),
                   ("winmm.dll", ["midiOutOpen"]), ("KERNEL32.dll", ["Beep"]),
                   ("USER32.dll", ["MessageBeep"]), ("dsound.dll", [1]),
                   ("xaudio2_9.dll", ["XAudio2Create"]), ("OpenAL32.dll", [1]),
                   ("mmdevapi.dll", [1]), ("winmm.dll", [1])]
        for library in imports:
            for delayed in [False, True]:
                with self.subTest(library=library, delayed=delayed), self.assertRaises(silent.SilentError):
                    silent.verify_silent_exe(executable(**{("delayed" if delayed else "imports"): [library]}))

    def test_unrelated_strings_are_not_treated_as_actual_imports_or_audio(self):
        unrelated = b"waveOutOpen\0PlaySoundW\0winmm.dll\0dsound.dll\0RIFF....WAVE"
        data = executable(extra=[(10, 109, b"\x89PNG\r\n\x1a\n" + unrelated)]) + unrelated
        self.assertTrue(silent.verify_silent_exe(data)["passed"])

    def test_historical_release_accepts_audio_but_new_version_is_rejected(self):
        for version in ["1.9.0", "2.0.0"]:
            data = executable(version, extra=[(10, 305, b"RIFF\x04\0\0\0WAVE")], imports=[("winmm.dll", ["waveOutOpen"])])
            self.assertEqual(fixture.release.inspect_release_exe(data, version)["FileVersion"], version)
        for changes in [{"extra": [(10, 305, b"retired")]}, {"imports": [("winmm.dll", ["waveOutWrite"])]}]:
            with self.subTest(changes=changes), self.assertRaisesRegex(fixture.release.ReleaseError, "무음"):
                fixture.release.inspect_release_exe(executable(**changes), "2.0.1")

    def test_consistently_rehashed_package_still_rejects_audio_executable(self):
        for changes in [{"extra": [(10, 900, b"RIFF\4\0\0\0WAVE")]},
                        {"imports": [("winmm.dll", ["waveOutWrite"])]}]:
            with self.subTest(changes=changes), tempfile.TemporaryDirectory(prefix="silent-package-") as temporary:
                root = Path(temporary)
                (root / "release").mkdir()
                (root / "README_KO.txt").write_text("무음 고양이\n", encoding="utf-8")
                (root / "release/THIRD_PARTY_NOTICES.txt").write_text("Fixture\n")
                exe = root / "release/app.exe"
                exe.write_bytes(executable())
                output = root / "package"
                info = fixture.release.prepare_release(root=root, exe_path=exe, version="2.0.1",
                    feed_url="https://example.com/latest.txt", download_base_url="https://example.com/v2.0.1",
                    notes="무음 배포\n", output=output)
                self.assertEqual(fixture.release.verify_release(output)["version"], "2.0.1")
                payload = executable(**changes)
                (output / "YeopjariCat-2.0.1.exe").write_bytes(payload)
                zip_path = output / "YeopjariCat-Windows-2.0.1.zip"
                with zipfile.ZipFile(zip_path) as archive:
                    entries = {name: archive.read(name) for name in archive.namelist()}
                entries["YeopjariCat.exe"] = payload
                with zipfile.ZipFile(zip_path, "w") as archive:
                    for name, value in entries.items():
                        archive.writestr(name, value)
                (output / "latest.txt").write_bytes(fixture.release.make_manifest(
                    "2.0.1", info["download_url"], payload, "무음 배포\n"))
                names = ["YeopjariCat-2.0.1.exe", "YeopjariCat-Windows-2.0.1.zip", "latest.txt", "RELEASE_NOTES_KO.txt"]
                info["files"] = {name: hashlib.sha256((output / name).read_bytes()).hexdigest() for name in names}
                sums = "".join(info["files"][name] + "  " + name + "\n" for name in names).encode("ascii")
                (output / "SHA256SUMS.txt").write_bytes(sums)
                info["files"]["SHA256SUMS.txt"] = hashlib.sha256(sums).hexdigest()
                (output / "release-info.json").write_text(json.dumps(info))
                with self.assertRaisesRegex(fixture.release.ReleaseError, "무음"):
                    fixture.release.verify_release(output)

    def test_malformed_directories_and_missing_terminators_fail_closed(self):
        base = executable(imports=[("KERNEL32.dll", ["GetTickCount64"])])
        cases = [base[:63], base[:-1]]
        for offset, value in [(0x3C, 0xFFFFFF00), (0x98 + 120, 0xFFFFFF00),
                              (0x98 + 124, 20), (0x98 + 128, 0xFFFFFF00),
                              (512 + 20, 0x80000000), (0x98 + 108, 99)]:
            data = bytearray(base)
            struct.pack_into("<I", data, offset, value)
            cases.append(bytes(data))
        for i, data in enumerate(cases):
            with self.subTest(case=i), self.assertRaises(silent.SilentError):
                silent.verify_silent_exe(data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
