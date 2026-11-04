#!/usr/bin/env python3
"""Meaningful release-package and executable-validation regression tests."""
from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cat_release", ROOT / "tools" / "release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
DIALOG_SPEC = importlib.util.spec_from_file_location(
    "cat_resource_fixtures", ROOT / "tests" / "resource_encoding_test.py")
dialog_fixtures = importlib.util.module_from_spec(DIALOG_SPEC)
DIALOG_SPEC.loader.exec_module(dialog_fixtures)


def version_block(key, value=b"", kind=0, children=()):
    data = bytearray(b"\0" * 6 + (key + "\0").encode("utf-16le"))
    data.extend(b"\0" * (-len(data) % 4))
    data.extend(value)
    for child in children:
        data.extend(b"\0" * (-len(data) % 4))
        data.extend(child)
    struct.pack_into("<HHH", data, 0, len(data), len(value) // (2 if kind else 1), kind)
    return bytes(data)


def executable(version="1.3.0", product="Yeopjari Cat", original="YeopjariCat.exe", update_dialog=None):
    major, minor, patch = release.version_tuple(version)
    strings = {"ProductName": product, "OriginalFilename": original, "FileVersion": version, "ProductVersion": version}
    entries = [version_block(key, (value + "\0").encode("utf-16le"), 1) for key, value in strings.items()]
    table = version_block("040904B0", kind=1, children=entries)
    string_info = version_block("StringFileInfo", kind=1, children=[table])
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x10000, major << 16 | minor, patch << 16,
                        major << 16 | minor, patch << 16, 0x3F, 0, 0x40004, 1, 0, 0, 0)
    version_blob = version_block("VS_VERSION_INFO", fixed, children=[string_info])
    payloads = [(16, 1, version_blob)]
    if update_dialog is not None:
        payloads.insert(0, (5, 201, update_dialog))
    root_size = 16 + 8 * len(payloads)
    resource = bytearray(root_size + 64 * len(payloads))
    struct.pack_into("<H", resource, 14, len(payloads))
    for index, (kind, identifier, payload) in enumerate(payloads):
        name_directory = root_size + index * 64
        language_directory, leaf = name_directory + 24, name_directory + 48
        struct.pack_into("<II", resource, 16 + index * 8, kind, 0x80000000 | name_directory)
        for offset, key, target in ((name_directory, identifier, 0x80000000 | language_directory),
                                    (language_directory, 0x409, leaf)):
            struct.pack_into("<H", resource, offset + 14, 1)
            struct.pack_into("<II", resource, offset + 16, key, target)
        struct.pack_into("<IIII", resource, leaf, 0x1000 + len(resource), len(payload), 0, 0)
        resource.extend(payload)
        resource.extend(b"\0" * (-len(resource) % 4))
    resource_size = len(resource)
    resource.extend(b"\0" * (-len(resource) % 512))
    data = bytearray(512) + resource
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<H", data, 0x98 + 68, 2)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 128, 0x1000, resource_size)
    section = 0x98 + 240
    data[section:section + 8] = b".rsrc\0\0\0"
    struct.pack_into("<IIII", data, section + 8, resource_size, 0x1000, len(resource), 512)
    return bytes(data)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="yeopjari-release-test-")
        self.root = Path(self.temporary.name)
        (self.root / "release").mkdir()
        (self.root / "README_KO.txt").write_text("옆자리 고양이\n", encoding="utf-8")
        (self.root / "release" / "THIRD_PARTY_NOTICES.txt").write_text("Test third-party notice\n", encoding="utf-8")
        self.exe = self.root / "release" / "YeopjariCat.exe"
        self.exe.write_bytes(executable())
        (self.root / "pet.ini").write_text("PRIVATE PET DATA", encoding="utf-8")
        (self.root / "preferences.ini").write_text("PRIVATE PREFERENCES", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self, **changes):
        args = dict(root=self.root, exe_path=self.exe, version="1.3.0",
                    feed_url="https://example.com/cat/latest.txt", download_base_url="https://example.com/cat/v1.3.0",
                    notes="고양이 업데이트\n츄르와 점프는 그대로예요. 🐈\n", output=self.root / "release output's folder")
        args.update(changes)
        result = release.prepare_release(**args)
        return args["output"], result

    def test_numeric_versions_and_malformed_versions(self):
        self.assertGreater(release.version_tuple("1.10.0"), release.version_tuple("1.9.99"))
        self.assertEqual(release.version_tuple("0.0.0"), (0, 0, 0))
        self.assertEqual(release.version_tuple("65535.65535.65535"), (65535, 65535, 65535))
        for invalid in ("1.3", "v1.3.0", "1.3.0.0", "01.3.0", "1.3.00", "-1.3.0", "1.3.0-beta", "1.3.0\n", "65536.0.0", "9" * 5000 + ".0.0"):
            with self.subTest(invalid=invalid[:30]), self.assertRaises(release.ReleaseError):
                release.version_tuple(invalid)

    def test_https_validation_and_injection(self):
        for valid in ("https://example.com/file.exe", "HTTPS://example.com:443/a?download=1", "https://127.0.0.1/a", "https://a-b.example/a%20b"):
            self.assertTrue(release.is_https_url(valid), valid)
        for invalid in ("http://example.com/a", "https://u:p@example.com/a", "https://example.com:80/a", "https://example.com:0443/a", "https://example.com/a#x", "https://example.com/a\nb", "https://example.com/a b", "https://example.com\\evil", "https://example.com/a%0d", "https://example.com/a%5c", "https://example.com/a%zz", "https://[::1]/a", "https://example..com/a", "https://example.com./a", "https://999.1.1.1/a", "https://127.00.0.1/a", "https://예시.com/a"):
            self.assertFalse(release.is_https_url(invalid), invalid)
        prefix = "https://example.com/"
        boundary = prefix + "a" * (8191 - len(prefix))
        self.assertEqual(release.require_feed_url(boundary), boundary)
        with self.assertRaisesRegex(release.ReleaseError, "8 KiB"):
            self.prepare(feed_url=boundary + "a")

    def test_manifest_exact_encoding_boundaries_and_bad_inputs(self):
        exe = executable()
        raw = release.make_manifest("1.3.0", "https://example.com/app.exe", exe, "Hello\n")
        parsed = release.parse_manifest(raw)
        self.assertEqual(parsed["notes_b64"], "SGVsbG8K")
        self.assertEqual(parsed["sha256"], hashlib.sha256(exe).hexdigest())
        self.assertEqual(release.parse_manifest(raw.replace(b"\n", b"\r\n")), parsed)
        self.assertEqual(release.parse_manifest(raw.rstrip(b"\n")), parsed)
        for changed in (raw + b"schema=1\n", raw.replace(b"schema=1", b"schema=2"), raw.replace(b"stable", b"beta"),
                        raw.replace(b"yeopjari-cat", b"another-app"), raw.replace(b"SGVsbG8K", b"Zh=="),
                        raw.replace(b"SGVsbG8K", base64.b64encode(b"\xff")), raw.replace(b"SGVsbG8K", base64.b64encode(b"a\0b")),
                        raw.replace(b"\n", b"\r\n", 1), raw + b"\n", b"x" * 65537):
            with self.subTest(changed=changed[:30]), self.assertRaises(release.ReleaseError):
                release.parse_manifest(changed)
        maximum = release.make_manifest("1.3.0", "https://example.com/app.exe", exe, "a" * 8192)
        self.assertEqual(len(release.parse_manifest(maximum)["notes"]), 8192)
        with self.assertRaises(release.ReleaseError):
            release.make_manifest("1.3.0", "https://example.com/app.exe", exe, "가" * 2731)

    def test_actual_pe_resource_verification(self):
        self.assertEqual(release.inspect_exe(executable(), "1.3.0")["ProductName"], "Yeopjari Cat")
        cases = [b"MZ" + b"\0" * 1022, executable()[:-1], executable(product="Other App"), executable(original="Other.exe"), executable("1.2.0")]
        for offset, fmt, replacement in ((0x84, "<H", 0x14C), (0x98, "<H", 0x10B), (0x98 + 68, "<H", 3), (0x98 + 128, "<I", 0xFFFFFF00), (0x98 + 132, "<I", 10000)):
            broken = bytearray(executable())
            struct.pack_into(fmt, broken, offset, replacement)
            cases.append(bytes(broken))
        for data in cases:
            with self.subTest(size=len(data)), self.assertRaises(release.ReleaseError):
                release.inspect_exe(data, "1.3.0")

    def test_user_zip_contains_only_public_files_and_same_binary(self):
        output, result = self.prepare()
        self.assertEqual(release.verify_release(output), result)
        with zipfile.ZipFile(output / "YeopjariCat-Windows-1.3.0.zip") as archive:
            self.assertEqual(archive.read("YeopjariCat.exe"), self.exe.read_bytes())
            self.assertEqual(archive.read("update-feed.txt"), b"https://example.com/cat/latest.txt\n")
            self.assertNotIn("pet.ini", archive.namelist())
            self.assertNotIn("preferences.ini", archive.namelist())
            self.assertFalse(any(name.startswith("source/") or "toolchain" in name for name in archive.namelist()))
        self.assertFalse((output / "publish-github.ps1").exists())

    def test_new_releases_require_compiled_korean_but_legacy_packages_remain_valid(self):
        for version, template in (("1.5.0", None), ("1.6.0", dialog_fixtures.dialog()),
                                  ("1.10.0", dialog_fixtures.dialog())):
            with self.subTest(version=version):
                self.exe.write_bytes(executable(version, update_dialog=template))
                output, result = self.prepare(version=version, output=self.root / version)
                self.assertEqual(release.verify_release(output), result)

    def test_mojibake_is_rejected_before_new_release_files_are_staged(self):
        garbled = "옆자리 고양이 업데이트".encode("utf-8").decode("latin-1")
        for version in ("1.6.0", "1.10.0"):
            with self.subTest(version=version):
                self.exe.write_bytes(executable(version, update_dialog=dialog_fixtures.dialog(title=garbled)))
                output = self.root / version
                with self.assertRaisesRegex(release.ReleaseError, "한글 리소스 검증"):
                    self.prepare(version=version, output=output)
                self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob(".yeopjari-stage-*")), [])

    def test_verify_rejects_mojibake_even_when_every_package_hash_matches(self):
        garbled = "옆자리 고양이 업데이트".encode("utf-8").decode("latin-1")
        self.exe.write_bytes(executable("1.6.0", update_dialog=dialog_fixtures.dialog(title=garbled)))
        # Model an otherwise well-formed package emitted by an old tool that
        # checked VERSIONINFO and hashes but never inspected RT_DIALOG.
        with mock.patch.object(release, "inspect_release_exe", side_effect=release.inspect_exe):
            output, _ = self.prepare(version="1.6.0")
        with self.assertRaisesRegex(release.ReleaseError, "한글 리소스 검증"):
            release.verify_release(output)

    def test_sha_and_zip_tampering_are_rejected(self):
        output, _ = self.prepare()
        exe = output / "YeopjariCat-1.3.0.exe"
        original = exe.read_bytes()
        exe.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        with self.assertRaisesRegex(release.ReleaseError, "SHA-256"):
            release.verify_release(output)
        exe.write_bytes(original)
        archive = output / "YeopjariCat-Windows-1.3.0.zip"
        archive.write_bytes(archive.read_bytes() + b"altered")
        with self.assertRaisesRegex(release.ReleaseError, "변경"):
            release.verify_release(output)

    def test_version_increase_and_existing_output(self):
        output, _ = self.prepare()
        with self.assertRaisesRegex(release.ReleaseError, "이미"):
            self.prepare()
        with self.assertRaisesRegex(release.ReleaseError, "큰 버전"):
            self.prepare(output=self.root / "other", previous_manifest=output / "latest.txt")
        self.assertFalse((self.root / "other").exists())
        self.exe.write_bytes(executable("1.4.0"))
        new, _ = self.prepare(version="1.4.0", output=self.root / "next", previous_manifest=output / "latest.txt")
        self.assertTrue((new / "latest.txt").exists())

    def test_bad_inputs_do_not_leave_release_directory(self):
        for changes in ({"version": "1.4.0"}, {"download_base_url": "https://example.com/?x=1"}, {"feed_url": "http://example.com/latest.txt"}):
            with self.subTest(changes=changes), self.assertRaises(release.ReleaseError):
                self.prepare(**changes)
        self.assertEqual(list(self.root.glob(".yeopjari-stage-*")), [])
        self.assertFalse((self.root / "release output's folder").exists())

    def test_github_script_is_draft_first_and_no_clobber(self):
        output, _ = self.prepare(repo="sample-owner/cat-releases")
        script = (output / "publish-github.ps1").read_text(encoding="utf-8-sig")
        self.assertIn("[switch]$Publish", script)
        self.assertIn("--draft --title", script)
        self.assertIn("if ($Publish)", script)
        self.assertIn("--draft=false --prerelease=false --latest", script)
        self.assertLess(script.index("$existing[0].isPrerelease"), script.index("& gh release upload"))
        self.assertLess(script.index("if ($remote.isPrerelease)"), script.index("& gh release upload"))
        self.assertIn("Get-FileHash", script)
        self.assertIn("already published", script)
        self.assertNotIn("--clobber", script)
        self.assertNotIn("auth token", script)


if __name__ == "__main__":
    unittest.main(verbosity=2)
