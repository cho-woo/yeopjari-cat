#!/usr/bin/env python3
"""Regression tests for the compiled update dialog, including the broken v1.5 EXE."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cat_resources", ROOT / "tools" / "verify_resources.py")
resources = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resources)


def utf16(text):
    return (text + "\0").encode("utf-16le")


def dialog(title=None, controls=None):
    """Construct a small real DLGTEMPLATEEX, with aligned DLGITEMTEMPLATEEXs."""
    if title is None:
        title = resources.EXPECTED_TITLE
    if controls is None:
        controls = [(2010, resources.EXPECTED_CONTROLS[2010]), (2012, ""),
                    (2011, ""), (2013, resources.EXPECTED_CONTROLS[2013]),
                    (1, resources.EXPECTED_CONTROLS[1]), (2, resources.EXPECTED_CONTROLS[2])]
    data = bytearray(struct.pack("<HHIIIHhhhh", 1, 0xFFFF, 0, 0, 0x40, len(controls), 0, 0, 360, 234))
    data.extend(b"\0\0\0\0" + utf16(title))  # no menu or window class
    data.extend(struct.pack("<HHBB", 9, 400, 0, 1) + utf16("Malgun Gothic"))
    for identifier, text in controls:
        data.extend(b"\0" * (-len(data) % 4))
        data.extend(struct.pack("<IIIhhhhI", 0, 0, 0, 12, 12, 336, 18, identifier))
        data.extend(struct.pack("<HH", 0xFFFF, 0x80 if identifier in (1, 2) else 0x82))
        data.extend(utf16(text) + b"\0\0")  # title, no extra creation data
    return bytes(data)


def executable(dialog_bytes):
    """Wrap the template in a PE32+ RT_DIALOG/201/en-US resource tree."""
    resource = bytearray(88)
    for offset, identifier, target in ((0, 5, 0x80000018), (24, 201, 0x80000030), (48, 0x409, 72)):
        struct.pack_into("<H", resource, offset + 14, 1)
        struct.pack_into("<II", resource, offset + 16, identifier, target)
    struct.pack_into("<IIII", resource, 72, 0x1000 + 88, len(dialog_bytes), 0, 0)
    resource.extend(dialog_bytes)
    resource_size = len(resource)
    resource.extend(b"\0" * (-len(resource) % 512))
    data = bytearray(512) + resource
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 128, 0x1000, resource_size)
    section = 0x98 + 240
    data[section:section + 8] = b".rsrc\0\0\0"
    struct.pack_into("<IIII", data, section + 8, resource_size, 0x1000, len(resource), 512)
    return bytes(data)


class ResourceEncodingTests(unittest.TestCase):
    def test_actual_korean_dialog_resource_passes(self):
        parsed = resources.parse_dialog(dialog())
        self.assertEqual(parsed["font"], "Malgun Gothic")
        self.assertEqual(parsed["controls"][1]["class"], 0x80)
        self.assertEqual(resources.verify_update_dialog(executable(dialog())), {
            "resource_type": "RT_DIALOG", "resource_id": 201, "languages": [0x409],
            "korean_strings_verified": 5, "passed": True})

    def test_unrelated_correct_strings_cannot_hide_a_broken_resource(self):
        garbled = resources.EXPECTED_TITLE.encode("utf-8").decode("latin-1")
        data = executable(dialog(title=garbled)) + utf16(resources.EXPECTED_TITLE)
        with self.assertRaisesRegex(resources.ResourceError, "caption is not correct Korean"):
            resources.verify_update_dialog(data)

    def test_each_static_control_is_checked_and_footer_must_have_an_id(self):
        correct = list(resources.EXPECTED_CONTROLS.items())
        for identifier in resources.EXPECTED_CONTROLS:
            broken = [(key, "garbled" if key == identifier else text) for key, text in correct]
            with self.subTest(identifier=identifier), self.assertRaisesRegex(resources.ResourceError, f"control {identifier}"):
                resources.verify_update_dialog(executable(dialog(controls=broken)))
        anonymous_footer = [(0xFFFFFFFF if key == 2013 else key, text) for key, text in correct]
        with self.assertRaisesRegex(resources.ResourceError, "control 2013"):
            resources.verify_update_dialog(executable(dialog(controls=anonymous_footer)))

    def test_truncated_and_malformed_templates_fail_cleanly(self):
        original = dialog()
        missing_terminator = struct.pack("<HHIIIHhhhh", 1, 0xFFFF, 0, 0, 0, 0, 0, 0, 1, 1) + b"\0\0\0\0" + "가".encode("utf-16le")
        invalid_utf16 = struct.pack("<HHIIIHhhhh", 1, 0xFFFF, 0, 0, 0, 0, 0, 0, 1, 1) + b"\0\0\0\0\x00\xd8\0\0"
        excess_controls = bytearray(original)
        struct.pack_into("<H", excess_controls, 16, 129)
        duplicate = dialog(controls=[(1, "업데이트"), (1, "나중에")])
        for name, data in (("fixed header", original[:25]), ("last control", original[:-1]),
                           ("unterminated title", missing_terminator), ("invalid UTF-16", invalid_utf16),
                           ("excess controls", bytes(excess_controls)), ("duplicate ID", duplicate)):
            with self.subTest(name=name), self.assertRaises(resources.ResourceError):
                resources.parse_dialog(data)

    def test_pe_resource_bounds_and_directory_identity_are_enforced(self):
        original = executable(dialog())
        cases = [original[:63], original[:550]]
        for offset, value in ((0x3C, 0xFFFFFF00), (0x98 + 128, 0xFFFFFF00),
                              (512 + 20, 0x8000FFFF), (512 + 24 + 16, 999),
                              (512 + 72, 0xFFFFFF00), (512 + 76, 65537)):
            data = bytearray(original)
            struct.pack_into("<I", data, offset, value)
            cases.append(bytes(data))
        for index, data in enumerate(cases):
            with self.subTest(case=index), self.assertRaises(resources.ResourceError):
                resources.verify_update_dialog(data)

    def test_previous_v1_5_executable_reproduces_reported_mojibake(self):
        baseline = ROOT / "release" / "github-ready-v1.5.0" / "YeopjariCat-1.5.0.exe"
        if not baseline.is_file():
            self.skipTest("The previous public v1.5.0 executable is not in this checkout")
        data = baseline.read_bytes()
        variants = resources.dialog_resources(data)
        self.assertTrue(variants)
        for language, raw in variants:
            with self.subTest(language=language):
                parsed = resources.parse_dialog(raw)
                self.assertTrue(parsed["title"].startswith("ì˜†ìž"), parsed["title"])
                self.assertNotEqual(parsed["controls"][1]["text"], "업데이트")
        with self.assertRaisesRegex(resources.ResourceError, "caption is not correct Korean"):
            resources.verify_update_dialog(data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
