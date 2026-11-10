#!/usr/bin/env python3
"""Review the production C++ pointer reach at its actual display size.

The source is the exact canonical frame4 rectangle, cleaned with the same
alpha threshold as LoadAtlasResource. Pillow transports premultiplied pixels
and composites the contact boards. This is a portable diagnostic rendering,
not a Windows desktop recording or a substitute for visual art approval.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import math
import subprocess

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE_RECT = (56, 468, 385, 834)
SOURCE_SIZE = (151, 168)
MOMENTS = (0, 280, 520, 700, 1000, 1300, 1550)
COATS = (('Cream', 'assets/cat.png'), ('Tuxedo', 'assets/styled-sprites/c8-r101.png'), ('OddEye', 'assets/styled-sprites/c10-r101.png'))
KINDS = ('Tiptoe', 'BothPaws')


def canonical_crop(path):
    source = Image.open(path).convert('RGBA').crop(SOURCE_RECT)
    raw = bytearray(source.tobytes())
    for index in range(0, len(raw), 4):
        if raw[index + 3] <= 16:
            raw[index:index + 4] = b'\0\0\0\0'
    source = Image.frombytes('RGBA', source.size, bytes(raw))
    # Resample premultiplied pixels to avoid transparent RGB fringe halos.
    return source.convert('RGBa').resize(SOURCE_SIZE, Image.Resampling.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/cursor-reach-review')
    parser.add_argument('--video', action='store_true', help='also make a 60 fps portable proof MP4')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'cursor-reach-review.so'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    str(ROOT / 'tools/render_cursor_reach_review.cpp'), '-o', str(binary)], check=True)
    lib = ctypes.CDLL(str(binary))
    lib.cursor_reach_output_height.argtypes = [ctypes.c_int]
    lib.cursor_reach_output_height.restype = ctypes.c_int
    render = lib.cursor_reach_frame
    render.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                       ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double, ctypes.c_double,
                       ctypes.POINTER(ctypes.c_double)]
    render.restype = ctypes.c_int
    width, height = SOURCE_SIZE
    out_height = lib.cursor_reach_output_height(height)
    if not height <= out_height <= height * 2:
        raise RuntimeError('Production reach output height is invalid')
    sources = []
    for coat, source_path in COATS:
        image = canonical_crop(ROOT / source_path)
        image.convert('RGBA').save(out / (coat.lower() + '-source4.png'))
        raw = image.tobytes('raw', 'BGRa')
        sources.append((coat, source_path, ctypes.create_string_buffer(raw, len(raw))))

    set_arms=lib.cursor_reach_set_arms
    set_arms.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    set_body=lib.cursor_reach_set_body
    set_body.argtypes=[ctypes.c_void_p]
    parts=[]
    for name in ('forelimbs.png','forelimbs-tuxedo.png','forelimbs-odd-eye.png'):
        part_path=ROOT/'assets/cursor-rig'/name
        if not part_path.exists():part_path=ROOT/'assets/cursor-rig/forelimbs.png'
        painted=Image.open(part_path).convert('RGBA').convert('RGBa')
        pw,ph=painted.width//3,painted.height
        left=painted.crop((0,0,pw,ph)).tobytes('raw','BGRa')
        right=painted.crop((pw,0,pw*2,ph)).tobytes('raw','BGRa')
        body=painted.crop((pw*2,0,pw*3,ph)).tobytes('raw','BGRa')
        parts.append((ctypes.create_string_buffer(left),ctypes.create_string_buffer(right),ctypes.create_string_buffer(body),pw,ph))
    records = []

    def frame(source, action, elapsed, record=False):
        selected=next(index for index,entry in enumerate(sources) if source is entry[2])
        left,right,body,pw,ph=parts[selected];set_arms(left,right,pw,ph);set_body(body)
        destination = ctypes.create_string_buffer(width * out_height * 4)
        data = (ctypes.c_double * 14)()
        if not render(source, width, height, destination, width, out_height, action, elapsed, 18., 95., data):
            raise RuntimeError('Production cursor reach render failed')
        if not all(math.isfinite(value) for value in data):
            raise RuntimeError('Non-finite rig contact metadata')
        image = Image.frombytes('RGBa', (width, out_height), destination.raw, 'raw', 'BGRa').convert('RGBA')
        if record:
            return image, {'elapsed_ms': elapsed, 'controls': list(data[:4]),
                           'paw_uv': [list(data[4:6]), list(data[6:8])],
                           'hind_support_uv': [list(data[8:10]), list(data[10:12])],
                           'contact_index': int(data[12]), 'duration_ms': int(data[13])}
        return image

    def tile(image, label, zoom=1, contacts=None):
        # Root is the original source bottom, including the added top padding.
        # All actions share a fixed scale; no per-frame bbox normalization.
        canvas = Image.new('RGB', (180 * zoom, 175 * zoom), '#e3edf3')
        draw = ImageDraw.Draw(canvas)
        ground = 145 * zoom
        draw.line((0, ground, canvas.width, ground), fill='#819baa')
        scale = 110 / height
        scaled = image.resize((round(image.width * scale * zoom), round(image.height * scale * zoom)),
                              Image.Resampling.LANCZOS)
        origin_x, origin_y = (canvas.width - scaled.width) // 2, ground - scaled.height
        canvas.paste(scaled, (origin_x, origin_y), scaled)
        if contacts:
            padding = out_height - height
            for index, (x, y) in enumerate(contacts['paw_uv'] + contacts['hind_support_uv']):
                px = origin_x + x * width * scale * zoom
                py = origin_y + (y * height + padding) * scale * zoom
                radius = 2.5 * zoom
                color = ('#e34b68', '#288bd5', '#318f46', '#88712a')[index]
                draw.ellipse((px-radius, py-radius, px+radius, py+radius), outline=color, width=zoom)
        draw.text((5 * zoom, 151 * zoom), label, fill='#243648')
        return canvas

    normal = Image.new('RGB', (len(MOMENTS) * 180, len(COATS) * len(KINDS) * 175), '#e3edf3')
    detail = Image.new('RGB', (normal.width * 2, normal.height * 2), '#e3edf3')
    contacts = Image.new('RGB', detail.size, '#e3edf3')
    for coat_index, (coat, source_path, source) in enumerate(sources):
        for action, kind in enumerate(KINDS):
            row = coat_index * len(KINDS) + action
            action_records = []
            for column, elapsed in enumerate(MOMENTS):
                image, metadata = frame(source, action, elapsed, record=True)
                image.save(out / f'{coat.lower()}-{kind.lower()}-{elapsed:04d}.png')
                label = f'{coat} {kind} {elapsed}ms'
                normal.paste(tile(image, label), (column * 180, row * 175))
                detail.paste(tile(image, label, 2), (column * 360, row * 350))
                contacts.paste(tile(image, label, 2, metadata), (column * 360, row * 350))
                action_records.append(metadata)
            records.append({'coat': coat, 'source': source_path, 'action': kind, 'samples': action_records})
    normal.save(out / 'cursor-reach-110px.png')
    detail.save(out / 'cursor-reach-2x.png')
    contacts.save(out / 'cursor-reach-contacts-2x.png')

    if args.video:
        video_frames = out / 'video-frames'
        video_frames.mkdir(exist_ok=True)
        fps = 60
        for index in range(240):
            elapsed = round(index * 1000 / fps) % 2000
            board = Image.new('RGB', (720, len(COATS)*350), '#e3edf3')
            for coat_index, (coat, _, source) in enumerate(sources):
                for action, kind in enumerate(KINDS):
                    board.paste(tile(frame(source, action, elapsed), f'{coat} {kind} {elapsed}ms', 2),
                                (action * 360, coat_index * 350))
            board.save(video_frames / f'{index:04d}.png')
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', str(fps), '-i',
                        str(video_frames / '%04d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-movflags', '+faststart', str(out / 'cursor-reach-portable.mp4')], check=True)

    report = {'renderer': 'Production C++ AttackPose and cursor_rig Render; Pillow transport/layout',
              'windows_desktop_recording': False, 'source_rect_xyxy': SOURCE_RECT,
              'source_proxy_size': SOURCE_SIZE, 'display_source_height_logical_px': 110,
              'output_proxy_size': [width, out_height], 'root_fixed': True,
              'new_original_full_cat_frames': 0,
              'purpose_painted_parts_boards': 2,
              'parts_source': 'assets/cursor-rig/rig-project.json',
              'visual_approval': 'Not established by this diagnostic; inspect shoulders, paw joins and rear soles',
              'samples': records,
              'source_sha256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                                for _, path in COATS}}
    (out / 'proof.json').write_text(json.dumps(report, indent=2) + '\n')
    print(out / 'cursor-reach-110px.png')
    print(out / 'cursor-reach-2x.png')
    print(out / 'proof.json')


if __name__ == '__main__':
    main()
