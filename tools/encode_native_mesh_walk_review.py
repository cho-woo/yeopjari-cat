#!/usr/bin/env python3
"""Encode native GDI+ mesh-walk evidence; never create or interpolate poses.

Input is the real executable's render-self-test/mesh-walk directory, its parent,
or a Windows artifact ZIP containing that directory. Comparison PNGs retain
60 output FPS. Contact PNGs sampled at60Hz are encoded at20FPS for3x slow.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import zipfile

from PIL import Image


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Evidence:
    def __init__(self, path):
        self.path = path
        self.archive = zipfile.ZipFile(path) if path.is_file() else None
        if self.archive:
            matches = [name for name in self.archive.namelist()
                       if name.endswith('mesh-walk/review.json')]
            if len(matches) != 1:
                raise ValueError('Expected exactly one native mesh-walk/review.json in artifact')
            self.prefix = matches[0][:-len('review.json')]
        else:
            self.folder = next((p for p in (path, path/'mesh-walk', path/'render-self-test/mesh-walk')
                                if (p/'review.json').is_file()), None)
            if self.folder is None:
                raise ValueError('Missing native mesh-walk/review.json')

    def read(self, name):
        return self.archive.read(self.prefix+name) if self.archive else (self.folder/name).read_bytes()

    def names(self, prefix):
        if self.archive:
            names = [n[len(self.prefix):] for n in self.archive.namelist() if n.startswith(self.prefix)]
        else:
            names = [p.name for p in self.folder.iterdir() if p.is_file()]
        return sorted(n for n in names if n.startswith(prefix) and n.endswith('.png'))


def encode(evidence, prefix, fps, size, output):
    names = evidence.names(prefix)
    if not names:
        raise ValueError(f'Missing native {prefix} PNGs')
    expected = [f'{prefix}{10000+i}.png' for i in range(len(names))]
    if names != expected:
        raise ValueError(f'Native {prefix} frames are missing, duplicated, or nonsequential')
    command = ['ffmpeg', '-y', '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'rgb24',
               '-video_size', f'{size[0]}x{size[1]}', '-framerate', str(fps), '-i', 'pipe:0',
               '-an', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(output)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    source = hashlib.sha256()
    try:
        for index, name in enumerate(names):
            data = evidence.read(name)
            source.update(name.encode()); source.update(data)
            image = Image.open(io.BytesIO(data))
            if image.size != size:
                raise ValueError(f'Unexpected native canvas size: {name}: {image.size}')
            image = image.convert('RGB')
            process.stdin.write(image.tobytes())
            if index in {0, len(names)//2, len(names)-1}:
                image.save(output.with_name(f'{output.stem}-{index:05d}.png'))
        process.stdin.close()
        error = process.stderr.read().decode(errors='replace')
        code = process.wait()
        if code:
            raise RuntimeError(f'FFmpeg failed: {error}')
    except BaseException:
        process.kill(); process.wait(); raise
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                       '-show_entries', 'stream=codec_name,pix_fmt,width,height,nb_frames,r_frame_rate',
                       '-show_entries', 'format=duration', '-of', 'json', str(output)], text=True))
    stream = probe['streams'][0]
    if (int(stream['nb_frames']) != len(names) or stream['r_frame_rate'] != f'{fps}/1' or
            stream['codec_name'] != 'h264' or stream['pix_fmt'] != 'yuv420p'):
        raise ValueError('Encoded video does not preserve expected frames, codec or output FPS')
    return {'file': output.name, 'frames': len(names), 'output_fps': fps,
            'duration_seconds': float(probe['format']['duration']),
            'native_png_sequence_sha256': source.hexdigest(), 'video_sha256': digest(output.read_bytes()),
            'probe': probe}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/native-mesh-walk-review'))
    args = parser.parse_args()
    evidence = Evidence(args.input)
    raw = evidence.read('review.json'); source = json.loads(raw)
    required = {'speed_logical': 22, 'physics_hz': 60, 'comparison_output_fps': 60,
                'contact_input_hz': 60, 'contact_slow_output_fps': 20, 'contact_scale': 3}
    if any(source.get(key) != value for key, value in required.items()):
        raise ValueError('Native review contract differs from the22px/s,60Hz comparison scenario')
    if not str(source.get('render', '')).startswith('actual GDI+ DrawCareSprite and Box2D'):
        raise ValueError('Input is not identified as the native application rendering path')
    args.output.mkdir(parents=True, exist_ok=True)
    compare = encode(evidence, 'compare-', 60, (900, 520), args.output/'native-mesh-walk-22.mp4')
    expected_duration = 3*max(source['old_stride'], source['new_stride'])/22
    if compare['duration_seconds']+1/60 < expected_duration:
        raise ValueError('Native comparison does not cover at least three cycles of both walks')
    contact = encode(evidence, 'contact-', 20, (1100, 760), args.output/'native-mesh-walk-contact-3x-slow.mp4')
    (args.output/'native-review.json').write_bytes(raw)
    (args.output/'native-contacts.csv').write_bytes(evidence.read('contacts.csv'))
    proof = {'source': source['render'], 'desktop_recording': False, 'poses_interpolated': False,
             'source_native_review_sha256': digest(raw), 'source_contract': source,
             'comparison': compare, 'contacts': contact}
    (args.output/'encoding.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    main()
