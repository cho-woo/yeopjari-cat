#!/usr/bin/env python3
"""Compare archived sprite walking with the current production C++ walking rig.

Requires g++, Git and Pillow. Run from any directory; outputs are under build/.
The current path uses distance-driven Evaluate and Rig, including sole registration.
Pillow handles final bitmap placement: this is portable QA, not a Windows/GDI run.
Source art and saves are never modified. The comparison plays once over 56 logical pixels
so its moving ruler cannot teleport at an artificial loop seam.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw
from render_motion_preview import atlas
from render_expression_preview import companion_crops

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ('cat_animation.hpp', 'cat_deformation.hpp', 'cat_expression.hpp')
NAMES = ('Cream', 'Silver', 'Black', 'Calico', 'Siamese', 'Munchkin', 'Dog', 'Rabbit', 'Tuxedo', 'British')
P = ctypes.c_void_p
D = ctypes.c_double
DP = ctypes.POINTER(D)


def library(path, include, baseline=False):
    command = ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
               '-I' + str(include), str(ROOT / 'tools/walking_preview.cpp'), '-o', str(path)]
    if baseline:
        command.append('-DPREVIEW_BASELINE')
    subprocess.run(command, check=True)
    lib = ctypes.CDLL(str(path))
    lib.walking_preview_pose.argtypes = [D, DP]
    lib.walking_preview_pose.restype = ctypes.c_int
    if not baseline:
        signatures = {
            'create': ([P, ctypes.c_int, ctypes.c_int, ctypes.c_int], P),
            'destroy': ([P], None), 'raster': ([P, DP, D, D], ctypes.c_int),
            'pixels': ([P], P), 'width': ([P], ctypes.c_int), 'height': ([P], ctypes.c_int),
            'ground_inset': ([P], D), 'body_offset': ([P], D), 'benchmark': ([P, ctypes.c_int, D, D], D),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(lib, 'walking_preview_' + name)
            function.argtypes, function.restype = arguments, result
    return lib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', default='2fe5a17adea3478b492a3edddc442494cd62d063')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/previews/walking-final')
    parser.add_argument('--zoom', type=int, choices=(1, 3), default=1)
    parser.add_argument('--benchmark-frames', type=int, default=0)
    args = parser.parse_args()
    if args.benchmark_frames < 0:
        parser.error('--benchmark-frames must be nonnegative')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    archived = out / 'walking-baseline'
    archived.mkdir(exist_ok=True)
    commit = subprocess.check_output(['git', 'rev-parse', args.baseline_ref], cwd=ROOT, text=True).strip()
    before_hash = {}
    for name in HEADERS:
        data = subprocess.check_output(['git', 'show', f'{commit}:src/{name}'], cwd=ROOT)
        (archived / name).write_bytes(data)
        before_hash['src/' + name] = hashlib.sha256(data).hexdigest()
    for name in ('cat.png', 'cat-silver.png', 'companion-poses.png'):
        previous = subprocess.check_output(['git', 'show', f'{commit}:assets/{name}'], cwd=ROOT)
        if previous != (ROOT / 'assets' / name).read_bytes():
            raise RuntimeError('Comparison art differs from baseline: ' + name)
    before = library(out / 'walking_before.so', archived, True)
    after = library(out / 'walking_after.so', ROOT / 'src')
    old = [atlas('cat.png')[0], atlas('cat-silver.png')[0]]
    new = [companion_crops(1), companion_crops(2)]
    canonical = [frames[1] for frames in old] + new[1]
    scales = [110 / max(im.height for im in frames[:6]) for frames in old]
    scales += [110 / im.height for im in companion_crops(0)]
    rigs, benchmarks = [], {}
    try:
        for character, source in enumerate(canonical):
            raw = source.convert('RGBa').tobytes('raw', 'BGRa')
            buffer = ctypes.create_string_buffer(raw, len(raw))
            rig = after.walking_preview_create(buffer, source.width, source.height, character)
            if not rig:
                raise RuntimeError('Production Rig::Prepare failed: ' + NAMES[character])
            rigs.append(rig)
            if args.benchmark_frames:
                benchmarks[NAMES[character]] = after.walking_preview_benchmark(
                    rig, args.benchmark_frames, source.width * scales[character], source.height * scales[character])

        def draw(character, distance, proposed, zoom=1):
            scale = scales[character] * zoom
            canvas = Image.new('RGBA', (240 * zoom, 180 * zoom), '#edf0f4')
            pen = ImageDraw.Draw(canvas)
            center, ground = 110 * zoom, 145 * zoom
            pen.line((0, ground, canvas.width, ground), fill='#718396', width=zoom)
            for x in range(-50, 300, 7):
                px = round((x - distance % 7) * zoom)
                pen.line((px, ground, px, ground + 4 * zoom), fill='#9ba9b8', width=zoom)
            pose = (D * 4)()
            model = after if proposed else before
            if not model.walking_preview_pose(distance, pose):
                raise RuntimeError('Distance-driven animation rejected the preview displacement')
            if proposed:
                rig = rigs[character]
                source = canonical[character]
                if not after.walking_preview_raster(rig, pose, source.width * scales[character], source.height * scales[character]):
                    raise RuntimeError('Production Rig::Raster failed')
                width, height = after.walking_preview_width(rig), after.walking_preview_height(rig)
                raw = ctypes.string_at(after.walking_preview_pixels(rig), width * height * 4)
                image = Image.frombytes('RGBa', (width, height), raw, 'raw', 'BGRa')
                image = image.resize((round(width * scale), round(height * scale)), Image.Resampling.BICUBIC).convert('RGBA')
                inset = after.walking_preview_ground_inset(rig) * scale + after.walking_preview_body_offset(rig) * zoom
                canvas.alpha_composite(image, (round(center - image.width / 2), round(ground - image.height + inset)))
            else:
                def layer(frame):
                    source = old[character][frame] if character < 2 else new[frame % 2][character - 2]
                    image = source.convert('RGBa').resize((round(source.width * scale), round(source.height * scale)), Image.Resampling.BICUBIC)
                    result = Image.new('RGBa', canvas.size)
                    result.paste(image, (round(center - image.width / 2), round(ground - image.height)))
                    return result
                image = layer(int(pose[0]))
                if pose[1] >= 0:
                    image = Image.blend(image, layer(int(pose[1])), round(pose[2] * 255) / 255)
                canvas.alpha_composite(image.convert('RGBA'))
            label = 'new continuous rig' if proposed else 'previous sprite cycle'
            pen.text((6 * zoom, 6 * zoom), f'{NAMES[character]} | {label}', fill='#273a4b')
            pen.text((6 * zoom, 163 * zoom), f'root travel {distance:.1f}px / tracking camera', fill='#4c657b')
            return canvas.convert('RGB')

        zoom = args.zoom
        frames = []
        for index in range(65):
            distance = index * 28 / 32
            sheet = Image.new('RGB', (480 * zoom, 360 * zoom))
            for row, character in enumerate((0, 2)):
                for column, proposed in enumerate((False, True)):
                    sheet.paste(draw(character, distance, proposed, zoom), (column * 240 * zoom, row * 180 * zoom))
            frames.append(sheet)
        gif = f'old-new-{zoom}x.gif'
        frames[0].save(out / gif, save_all=True, append_images=frames[1:], duration=40)
        sheet = Image.new('RGB', (960, 540), 'white')
        for row, character in enumerate((0, 1, 2)):
            for sample, distance in enumerate((8.8, 20.)):
                for column, proposed in enumerate((False, True)):
                    sheet.paste(draw(character, distance, proposed), ((sample * 2 + column) * 240, row * 180))
        sheet.save(out / 'old-new-contact.png')
        sources = ('src/cat_locomotion_rig.hpp', 'src/cat_animation.hpp', 'src/main.cpp',
                   'tools/walking_preview.cpp', 'tools/render_walking_preview.py')
        manifest = {
            'renderer': 'Production C++ distance-driven Evaluate and Rig; Pillow final raster. Not Windows/GDI+.',
            'baseline_commit': commit, 'baseline_sha256': before_hash,
            'comparison': 'Old sprite cycle and new canonical limb rig, same art and root displacement.',
            'playback': 'One-shot 65 samples, 56px travel; tracking camera, moving 7px ruler.',
            'products': [gif, 'old-new-contact.png'], 'benchmark_ms_per_frame': benchmarks,
            'benchmark_frames': args.benchmark_frames,
            'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
        }
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(out)
        if benchmarks:
            print(json.dumps(benchmarks, indent=2))
    finally:
        for rig in rigs:
            after.walking_preview_destroy(rig)


if __name__ == '__main__':
    main()
