#!/usr/bin/env python3
"""Encode a native locomotion capture at its declared sampling rate.

This tool never generates, interpolates or retimes a pose. The capture directory
must contain capture.json and contiguous frame-00000.png ... frames, emitted by
the application's real GDI+ renderer. Existing 20 Hz review media remain valid
but cannot silently be labelled as a 60 Hz capture.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def encode(directory: Path, output: Path) -> dict:
    directory = directory.resolve()
    metadata = json.loads((directory / 'capture.json').read_text(encoding='utf-8-sig'))
    require(metadata.get('schema') == 1, 'Capture schema 1 required')
    require(metadata.get('render_source') == 'Windows GDI+ / Box2D',
            'Native renderer provenance required')
    require(metadata.get('desktop_recording') is False,
            'An offscreen native capture must not be labelled a desktop recording')
    fps = metadata.get('fps')
    count = metadata.get('frame_count')
    width, height = metadata.get('canvas', [0, 0])
    require(type(fps) is int and 1 <= fps <= 120, 'Invalid capture FPS')
    require(type(count) is int and 1 <= count <= 36000, 'Invalid frame count')
    require(type(width) is int and type(height) is int and
            0 < width <= 4096 and 0 < height <= 4096 and width % 2 == height % 2 == 0,
            'Even, bounded canvas dimensions required')
    names = sorted(directory.glob('frame-*.png'))
    require(len(names) == count, 'Declared frame count differs from actual capture')
    digest = hashlib.sha256()
    for index, path in enumerate(names):
        require(path.name == f'frame-{index:05d}.png', 'Capture frames must be contiguous')
        data = path.read_bytes()
        require(data.startswith(b'\x89PNG\r\n\x1a\n') and len(data) >= 24,
                f'Invalid PNG: {path.name}')
        require(struct.unpack_from('>II', data, 16) == (width, height),
                f'Wrong native canvas: {path.name}')
        digest.update(path.name.encode('ascii') + b'\0')
        digest.update(hashlib.sha256(data).digest())
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        'ffmpeg', '-y', '-v', 'error', '-fflags', '+bitexact', '-framerate', str(fps),
        '-i', str(directory / 'frame-%05d.png'), '-frames:v', str(count),
        '-an', '-map_metadata', '-1', '-map_chapters', '-1',
        '-c:v', 'libx264', '-threads:v', '1', '-flags:v', '+bitexact',
        '-pix_fmt', 'yuv420p', '-crf', '18',
        '-movflags', '+faststart', str(output),
    ], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(output), '-f', 'null', '-'], check=True)
    probe = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,avg_frame_rate,nb_frames:format=duration',
        '-of', 'json', str(output),
    ], text=True))
    stream = probe['streams'][0]
    numerator, denominator = map(int, stream['avg_frame_rate'].split('/'))
    require(numerator == fps * denominator, 'Encoder changed the capture cadence')
    require(int(stream['nb_frames']) == count, 'Encoder dropped or added capture frames')
    require((stream['width'], stream['height']) == (width, height), 'Encoder changed canvas')
    require(abs(float(probe['format']['duration']) - count / fps) < .002,
            'Encoded duration differs from the native capture')
    proof = {**metadata, 'frames_sha256': digest.hexdigest(),
             'interpolated_frames': 0, 'retimed': False,
             'duration_seconds': count / fps,
             'video_sha256': hashlib.sha256(output.read_bytes()).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    print(json.dumps(encode(arguments.capture, arguments.output)))


if __name__ == '__main__':
    main()
