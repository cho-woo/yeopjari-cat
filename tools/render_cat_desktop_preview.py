#!/usr/bin/env python3
"""Developer mock desktop: actual Controller trace plus production sprite poses.

Run after tools/test.sh. Requires developer-only Pillow and native G++/Box2D.
No source artwork is edited. This is not a Windows capture: window inputs are
scripted, and Pillow substitutes for GDI+ rasterization. Physics-to-art rules are
explicitly mirrored in cat_desktop_preview.cpp and identified in the manifest.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont
import render_motion_preview as sprites

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build' / 'previews' / 'desktop'
SCENES = ('rise', 'closed_inflight', 'closed_supported')
TITLES = {'rise': 'Jump to window', 'closed_inflight': 'Window closes during flight',
          'closed_supported': 'Window closes after landing'}
SIZE = (1000, 540)
GROUND = 470


def font(size):
    try:
        return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', size)
    except OSError:
        return ImageFont.load_default()


FONT, SMALL = font(17), font(13)


def collect():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    library = ROOT / 'build/native-box2d/libbox2d.a'
    if not library.exists():
        raise SystemExit('Run bash tools/test.sh to build native Box2D first.')
    binary = OUTPUT / 'cat_desktop_preview'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                    str(ROOT / 'tools/cat_desktop_preview.cpp'), str(ROOT / 'src/cat_physics.cpp'),
                    '-I' + str(ROOT / 'third_party/box2d/include'), str(library), '-lm', '-o', str(binary)], check=True)
    trace = OUTPUT / 'cat-desktop-traces.csv'
    with trace.open('w') as output:
        subprocess.run([str(binary)], stdout=output, check=True)
    scenes = {scene: [] for scene in SCENES}
    with trace.open() as source:
        for row in csv.DictReader(source):
            scene = row.pop('scenario')
            scenes[scene].append({key: float(value) for key, value in row.items()})
    return scenes


def desktop(scene, row, character):
    canvas = Image.new('RGBA', SIZE, '#e3ebf2')
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((0, 0, SIZE[0], 64), fill='#f7f9fb')
    draw.text((18, 10), 'DEVELOPER PREVIEW - NOT A WINDOWS CAPTURE', fill='#623e16', font=FONT)
    draw.text((18, 35), f'{TITLES[scene]} | {"Cream" if character == 0 else "Silver"} | t={row["time"]:.2f}s', fill='#243c50', font=FONT)
    draw.rounded_rectangle((30, 88, 310, 216), radius=7, fill='#d0dce6', outline='#b7c7d4')
    draw.text((48, 105), 'Mock desktop', fill='#657c8d', font=FONT)
    draw.text((48, 134), 'Real Box2D trajectory', fill='#657c8d', font=SMALL)
    draw.text((48, 157), 'Production atlas and C++ poses', fill='#657c8d', font=SMALL)
    if row['ledge_visible']:
        left, right = row['ledge_left'], row['ledge_right']
        top = GROUND - row['ledge_height']
        draw.rounded_rectangle((left, top, right, GROUND - 15), radius=6, fill='#ffffff', outline='#9eafbd', width=1)
        draw.rectangle((left, top, right, top + 29), fill='#c5d7e7')
        draw.line((left, top, right, top), fill='#507590', width=2)
        draw.text((left + 12, top + 6), 'Mock program window', fill='#304d64', font=SMALL)
        draw.text((right - 21, top + 6), 'x', fill='#344f63', font=SMALL)
        draw.text((left + 18, top + 52), 'Top edge = actual Box2D ledge', fill='#6a7b89', font=SMALL)
    else:
        draw.text((470, 285), 'Window has closed', fill='#9a6449', font=FONT)
    draw.rectangle((0, GROUND, SIZE[0], SIZE[1]), fill='#374a5c')
    draw.line((0, GROUND, SIZE[0], GROUND), fill='#152f44', width=2)
    draw.text((18, GROUND + 10), 'Taskbar / physical floor', fill='#e8f0f8', font=SMALL)
    draw.text((18, GROUND + 36), 'Scripted windows | Pillow rasterizer | no outfits, audio or Win32 integration', fill='#c1d1de', font=SMALL)
    status = 'WINDOW CONTACT' if row['on_platform'] else 'FLOOR CONTACT' if row['grounded'] else 'AIRBORNE'
    draw.text((580, GROUND + 10), f'{status}  lift={row["render_lift"]:.1f}  vy={row["vy"]:.1f}', fill='#ecf3f8', font=SMALL)
    return canvas


def render(renderer, scene, row, character):
    # Use the original developer renderer's actual atlas cleanup/crop, scale,
    # premultiplied blending, C++ deformation and ground-clearance functions.
    pose = [row[f'p{index}'] for index in range(20)]
    pose[1] += row['render_x'] - SIZE[0] / 2
    pose[2] += row['render_lift']
    pose = tuple(pose)
    layer = renderer.layer(character, int(pose[0]), pose)
    if pose[8] >= 0 and pose[9] > 0:
        second = renderer.layer(character, int(pose[8]), pose)
        weight = math.floor(min(1, max(0, pose[9])) * 255 + .5)
        layer = Image.blend(layer, second, weight / 255)
    result = desktop(scene, row, character)
    result.alpha_composite(layer.convert('RGBA'))
    if pose[19]:
        result.alpha_composite(renderer.head_layer(character, pose).convert('RGBA'))
    return result.convert('RGB')


def write_contact(renderer, scene, rows):
    times = [.45, .8, 1.3, 1.68, 2.6, 3.35]
    width, height = 320, 330
    sheet = Image.new('RGB', (width * len(times), height * 2), '#f7f9fb')
    for character in range(2):
        for column, time in enumerate(times):
            row = rows[round(time * 60)]
            frame = render(renderer, scene, row, character)
            center = row['render_x']
            bottom = GROUND - row['render_lift']
            crop = frame.crop((round(center - 80), round(bottom - 140), round(center + 80), round(bottom + 15)))
            crop = crop.resize((320, 310), Image.Resampling.NEAREST)
            sheet.paste(crop, (column * width, character * height + 20))
            draw = ImageDraw.Draw(sheet)
            draw.text((column * width + 8, character * height + 2),
                      f'{"Cream" if character == 0 else "Silver"} {time:.2f}s  f{int(row["p0"])}', fill='#263d4f', font=SMALL)
    sheet.save(OUTPUT / f'{scene}-cat-contact.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fps', type=int, default=20, choices=(10, 15, 20, 30, 60))
    args = parser.parse_args()
    rows = collect()
    sprites.SIZE, sprites.GROUND, sprites.ZOOM = SIZE, GROUND, 1
    sprites.OUTPUT = OUTPUT / 'sprite-cache'
    renderer = sprites.Renderer()
    for scene in SCENES:
        frames = []
        for index in range(0, len(rows[scene]), 60 // args.fps):
            image = Image.new('RGB', (SIZE[0] * 2, SIZE[1]))
            for character in range(2):
                image.paste(render(renderer, scene, rows[scene][index], character), (character * SIZE[0], 0))
            frames.append(image)
        target = OUTPUT / f'{scene}-cats.gif'
        frames[0].save(target, save_all=True, append_images=frames[1:], duration=1000 // args.fps, loop=0, disposal=2)
        frames[round(1.6 * args.fps)].save(OUTPUT / f'{scene}-desktop.png')
        write_contact(renderer, scene, rows[scene])
        print(target, flush=True)
    inputs = ['src/main.cpp', 'src/cat_physics.cpp', 'src/cat_physics.hpp', 'src/cat_animation.hpp',
              'src/cat_deformation.hpp', 'src/cat_render_geometry.hpp', 'tools/cat_desktop_preview.cpp',
              'tools/render_cat_desktop_preview.py', 'tools/motion_preview.cpp', 'tools/render_motion_preview.py',
              'build/native-box2d/libbox2d.a']
    inputs.extend('assets/' + name for name in renderer.sheets)
    manifest = {
        'description': 'Developer mock desktop; not a Windows capture',
        'physics': 'Actual cat::physics::Controller and native pinned Box2D, sampled at 60Hz',
        'art': 'Production PNG atlas, Evaluate, Warp, quantization and ground-clearance; Pillow bicubic rasterization',
        'adapter': 'Shared cat::animation::AirbornePose; grounded/landing rules explicitly mirrored from main.cpp::Render',
        'limits': ['Scripted window inputs, not real window discovery', 'No Win32/GDI+ execution',
                   'Default adult scale/body and no outfits/audio', 'Scenario adapter is not the full production state machine'],
        'fps': args.fps,
        'scenario_landings': {scene: [{'time': row['time'], 'x': row['x'], 'lift': row['lift'],
                                      'on_platform': bool(row['on_platform'])} for row in data if row['landed']]
                              for scene, data in rows.items()},
        'airborne_pose_frames': {scene: sorted({int(row['p0']) for row in data if not row['grounded']})
                                for scene, data in rows.items()},
        'airborne_crossfade_samples': {scene: sum(not row['grounded'] and row['p8'] >= 0 and row['p9'] > 0
                                                  for row in data) for scene, data in rows.items()},
        'sources_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in inputs},
    }
    (OUTPUT / 'cat-desktop-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
