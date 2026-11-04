"""Ensure baseline comparison cannot hide changed runtime bytes."""
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import verify_v19_baseline as baseline
import resource_encoding_test as fixtures


class BaselineComparisonTests(unittest.TestCase):
    def test_only_timestamp_changes_are_accepted(self):
        data = fixtures.executable(fixtures.dialog())
        # inspect_release_exe also requires VERSIONINFO. The structural fixture
        # here isolates normalization; production calls retain full validation.
        from unittest.mock import patch
        import struct
        with patch.object(baseline.release, 'inspect_release_exe'):
            changed = bytearray(data)
            pe = struct.unpack_from('<I', data, 0x3c)[0]
            changed[pe+8:pe+12] = b'\x01\x02\x03\x04'
            self.assertTrue(baseline.compare(data, bytes(changed))['identical_except_pe_timestamps'])
            changed[-10] ^= 1
            with self.assertRaisesRegex(ValueError, 'outside PE timestamps'):
                baseline.compare(data, bytes(changed))

    def test_public_zip_hash_rejects_replacement(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'bad'
            p.write_bytes(b'untrusted replacement')
            with self.assertRaisesRegex(ValueError, 'ZIP checksum'):
                baseline.verify(p, p, p)
