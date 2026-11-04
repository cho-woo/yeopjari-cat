#!/usr/bin/env python3
"""Compare a rebuild with the immutable public v1.9.0 ZIP and update feed.

Only the COFF and IMAGE_DEBUG_DIRECTORY timestamps may differ; every other
byte, including code, embedded assets and save/update logic, must match.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import zipfile

import release

ZIP_SHA256 = '5012f225a338363f28d1f9e2f60e99dcf93b697209d487d71dc466c7b6967a5c'
FEED_SHA256 = '271a668859f74705026ee47d37eb5945b96f1990b408c81dbd0da17aa6a7f0e5'


def normalized_pe(data):
    release.inspect_release_exe(data, '1.9.0')
    result = bytearray(data)
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    debug_rva, debug_size = struct.unpack_from('<II', data, optional + 112 + 6 * 8)
    offsets = [pe + 8]
    if debug_size:
        if debug_size % 28 or debug_size > 28 * 128:
            raise ValueError('Invalid debug directory size')
        matches = []
        for index in range(count):
            at = optional + optional_size + index * 40
            _, rva, raw_size, raw_at = struct.unpack_from('<IIII', data, at + 8)
            if rva <= debug_rva and debug_rva + debug_size <= rva + raw_size:
                matches.append(raw_at + debug_rva - rva)
        if len(matches) != 1 or matches[0] + debug_size > len(data):
            raise ValueError('Invalid debug directory bounds')
        offsets.extend(range(matches[0] + 4, matches[0] + debug_size, 28))
    for at in offsets:
        result[at:at + 4] = b'\0' * 4
    return bytes(result), offsets


def compare(public, rebuilt):
    a, a_offsets = normalized_pe(public)
    b, b_offsets = normalized_pe(rebuilt)
    if a_offsets != b_offsets or a != b:
        raise ValueError('Rebuilt EXE differs outside PE timestamps')
    return {'byte_identical': public == rebuilt,
            'identical_except_pe_timestamps': True,
            'allowed_timestamp_offsets': a_offsets,
            'public_sha256': hashlib.sha256(public).hexdigest(),
            'rebuilt_sha256': hashlib.sha256(rebuilt).hexdigest()}


def verify(zip_path, feed_path, exe_path):
    zip_bytes, feed = zip_path.read_bytes(), feed_path.read_bytes()
    if hashlib.sha256(zip_bytes).hexdigest() != ZIP_SHA256:
        raise ValueError('Public v1.9.0 ZIP checksum mismatch')
    if hashlib.sha256(feed).hexdigest() != FEED_SHA256:
        raise ValueError('Public stable feed is not the pinned v1.9.0 feed')
    with zipfile.ZipFile(zip_path) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC failure')
        public = archive.read('YeopjariCat.exe')
        expected_feed = b'https://github.com/cho-woo/yeopjari-cat/releases/latest/download/latest.txt\n'
        if archive.read('update-feed.txt') != expected_feed:
            raise ValueError('ZIP update-feed mismatch')
    fields = dict(line.split('=', 1) for line in feed.decode('ascii').splitlines())
    notes = base64.b64decode(fields['notes_b64'], validate=True).decode('utf-8')
    expected = release.make_manifest('1.9.0', 'https://github.com/cho-woo/yeopjari-cat/releases/download/v1.9.0/YeopjariCat-1.9.0.exe', public, notes)
    if feed != expected:
        raise ValueError('Public feed does not describe the verified EXE')
    result = compare(public, exe_path.read_bytes())
    result.update(public_zip_verified=True, public_update_feed_verified=True)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-zip', type=Path, required=True)
    parser.add_argument('--public-feed', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=release.ROOT/'release/YeopjariCat.exe')
    args = parser.parse_args()
    print(json.dumps(verify(args.public_zip, args.public_feed, args.exe), indent=2))
