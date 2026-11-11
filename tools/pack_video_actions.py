#!/usr/bin/env python3
"""Bake the retained Dropshot videos into registered, transparent app atlases.

Authoring requires NumPy, OpenCV and Pillow. Runtime import and verification use
the standard library. Each character/action has one scale. Only the jump's
recorded screen translation is removed because Box2D supplies world movement.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image
from bake_character_looks import eye_material, white_coat

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/video-actions'
CELL = 320
ROOT_POINT = (160, 240)
REFERENCE_HEIGHT = 96
RESOURCE = {'groom': 113, 'punch': 117, 'jump': 119, 'knead': 440, 'reach': 441}
SOURCE_ACTION = {'groom': 'wipe', 'punch': 'punch', 'jump': 'jump', 'knead': 'knead', 'reach': 'reach'}
REACH_KEYS = {
    'cream': [(18, 300, 651, 369, 651), (36, 300, 651, 369, 651),
        (54, 243, 360, 378, 360), (72, 282, 228, 396, 306),
        (90, 228, 312, 360, 222), (108, 282, 249, 396, 330),
        (126, 252, 636, 348, 633), (141, 300, 651, 369, 651)],
    'tuxedo': [(18, 282, 693, 378, 693), (36, 282, 693, 378, 693),
        (54, 243, 366, 405, 303), (72, 282, 309, 402, 354),
        (90, 234, 357, 372, 309), (108, 285, 336, 408, 369),
        (126, 246, 660, 348, 678), (141, 282, 693, 378, 693)]}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def foreground(rgb):
    values = rgb.astype(np.float32)
    red, green, blue = values.transpose(2, 0, 1)
    backdrop = (green > np.maximum(red, blue) * 1.22 + 12) | (values.max(axis=2) < 12)
    count, labels, stats, _ = cv2.connectedComponentsWithStats((~backdrop).astype(np.uint8), 8)
    if count < 2:
        raise ValueError('No character foreground')
    mask = (labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])).astype(np.uint8)
    # Preserve dark eyes, mouth and fur enclosed by the character outline.
    exterior = (1 - mask).copy()
    cv2.floodFill(exterior, None, (0, 0), 2)
    mask[exterior == 1] = 1
    alpha = cv2.GaussianBlur(mask.astype(np.float32), (0, 0), .36)
    alpha[cv2.distanceTransform(mask, cv2.DIST_L2, 5) > 1.2] = 1
    alpha = np.rint(alpha.clip(0, 1) * 255).astype(np.uint8)
    alpha[alpha < 12] = 0
    # Remove green spill only on the antialiased outer edge.
    edge = (alpha > 0) & (alpha < 245)
    corrected = rgb.copy()
    corrected[:, :, 1][edge] = np.minimum(green, np.maximum(red, blue))[edge].astype(np.uint8)
    corrected[alpha == 0] = 0
    return Image.fromarray(np.dstack((corrected, alpha)), 'RGBA')


def head_root(image):
    alpha = np.asarray(image.getchannel('A'))
    x0, y0, x1, y1 = image.getbbox()
    rows = alpha[y0:y0 + round((y1 - y0) * .56)] > 128
    yy, xx = np.where(rows)
    return float((xx.min() + xx.max()) / 2), float(y0), float(y1)


def selection(clip):
    if clip == 'jump':
        return list(range(42, 104))
    if clip == 'groom':
        return list(range(12, 138))
    if clip == 'knead':
        return list(range(22, 142))
    if clip == 'reach':
        return list(range(18, 142, 3))
    return list(range(22, 142, 2))


def durations(clip, indices):
    if clip == 'jump':
        return [20] * len(indices)
    total = {'groom': 5250, 'knead': 5200, 'reach': 1550, 'punch': 2900}[clip]
    if clip == 'punch':
        points = [(22, 0), (40, 300), (68, 460), (102, 600), (126, 790), (140, 1050), (142, total)]
        clock = np.interp(indices + [142], *np.asarray(points).T)
    else:
        clock = np.linspace(0, total, len(indices) + 1)
    return [int(round(clock[i + 1]) - round(clock[i])) for i in range(len(indices))]


def bake(clip, character, indices):
    path = DIRECTORY / 'sources' / f'{character}-{SOURCE_ACTION[clip]}.mp4'
    video = cv2.VideoCapture(str(path))
    if not video.isOpened():
        raise ValueError('Cannot decode ' + str(path))
    frames = []
    while True:
        valid, bgr = video.read()
        if not valid:
            break
        frames.append(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
    fps = video.get(cv2.CAP_PROP_FPS)
    video.release()
    if max(indices) >= len(frames) or abs(fps - 24) > .01:
        raise ValueError('Unexpected original video cadence or duration')
    neutral = foreground(frames[0])
    center, top, bottom = head_root(neutral)
    factor = REFERENCE_HEIGHT / (bottom - top)
    columns = 8
    rows = (len(indices) + columns - 1) // columns
    atlas = Image.new('RGBA', (columns * CELL, rows * CELL))
    records = []
    paws = []
    for ordinal, source_index in enumerate(indices):
        image = foreground(frames[source_index])
        anchor_x, anchor_y = center, float(image.getbbox()[3])
        if clip == 'jump' and 60 <= source_index <= 80:
            moving_x, _, _ = head_root(image)
            anchor_x += moving_x - center
        image = image.resize((round(image.width * factor), round(image.height * factor)), Image.Resampling.LANCZOS)
        shift = (round(ROOT_POINT[0] - anchor_x * factor), round(ROOT_POINT[1] - anchor_y * factor))
        bounds = image.getbbox()
        if bounds[0] + shift[0] < 0 or bounds[1] + shift[1] < 0 or bounds[2] + shift[0] > CELL or bounds[3] + shift[1] > CELL:
            raise ValueError(f'Clipped {character} {clip} frame {source_index}: {bounds}, {shift}')
        cell = Image.new('RGBA', (CELL, CELL))
        cell.paste(image, shift)
        x, y = ordinal % columns * CELL, ordinal // columns * CELL
        atlas.paste(cell, (x, y))
        records.append({'rect': [x, y, CELL, CELL], 'anchor': list(ROOT_POINT)})
        if clip == 'reach':
            keys = np.asarray(REACH_KEYS[character])
            coordinates = [np.interp(source_index, keys[:, 0], keys[:, component]) for component in range(1, 5)]
            paws.append([[(coordinates[i] - anchor_x) * factor * 110 / REFERENCE_HEIGHT,
                (anchor_y - coordinates[i + 1]) * factor * 110 / REFERENCE_HEIGHT] for i in (0, 2)])
    output = DIRECTORY / f'{character}-{clip}.png'
    atlas.save(output)
    return output, records, columns, rows, {'source': path.relative_to(DIRECTORY).as_posix(),
        'sha256': sha(path), 'source_fps': fps, 'source_frame_count': len(frames),
        'selected_frame_indices': indices, 'scale': factor, 'neutral_root': [center, bottom],
        'reference_height': REFERENCE_HEIGHT, 'paw_points_logical': paws}


def main():
    entries = []
    evidence = {}
    styles_path = ROOT / 'assets/styled-sprites/manifest.json'
    styles = json.loads(styles_path.read_text(encoding='utf-8'))
    replaced = set(RESOURCE.values())
    styles['atlases'] = [entry for entry in styles['atlases'] if entry['base_resource_id'] not in replaced]
    odd_policy_path = ROOT / 'assets/styled-sprites/odd-eye-policy.json'
    odd_policy = json.loads(odd_policy_path.read_text(encoding='utf-8'))
    odd_policy['atlases'] = [entry for entry in odd_policy['atlases'] if entry['base_resource_id'] not in replaced]
    iris_path = ROOT / 'assets/styled-sprites/odd-eye-iris-regions.json'
    iris = json.loads(iris_path.read_text(encoding='utf-8'))
    iris['atlases'] = [entry for entry in iris['atlases'] if entry['base_resource_id'] not in replaced]
    for clip, resource in RESOURCE.items():
        indices = selection(clip)
        exposure = durations(clip, indices)
        steps = [{'frame': index, 'ms': milliseconds} for index, milliseconds in enumerate(exposure)]
        events = []
        if clip == 'jump':
            intro = indices.index(60)
            landing = indices.index(81)
            sequence = {'intro': steps[:intro], 'loop': steps[intro:landing], 'outro': steps[landing:],
                'finish': 'complete', 'duration_ms': 0, 'events': []}
        else:
            if clip == 'punch':
                events = [{'step': indices.index(index), 'offset_ms': 0, 'type': 'contact'} for index in (40, 102)]
            sequence = {'intro': steps, 'loop': [], 'outro': [], 'finish': 'complete', 'duration_ms': 0, 'events': events}
        cream, records, columns, rows, registration = bake(clip, 'cream', indices)
        evidence['cream-' + clip] = registration
        entries.append({'clip': clip, 'character': 0, 'source': cream.name, 'columns': columns, 'rows': rows,
            'reference_height': REFERENCE_HEIGHT, 'frames': records, 'frame_count': len(records),
            'sha256': sha(cream), 'sequence': sequence})
        tuxedo, _, _, _, registration = bake(clip, 'tuxedo', indices)
        evidence['tuxedo-' + clip] = registration
        # Share one rectangle per pose while retaining both complete coats.
        # Tight source rectangles save decoded memory without fitting the
        # characters to independent bounding boxes or changing their scale.
        with Image.open(cream) as base, Image.open(tuxedo) as styled:
            for ordinal, record in enumerate(records):
                x, y, _, _ = record['rect']
                boxes = [image.crop((x, y, x + CELL, y + CELL)).getbbox() for image in (base, styled)]
                left = max(0, min(box[0] for box in boxes) - 2)
                top = max(0, min(box[1] for box in boxes) - 2)
                right = min(CELL, max(max(box[2] for box in boxes) + 2, ROOT_POINT[0] + 1))
                bottom = min(CELL, max(max(box[3] for box in boxes) + 2, ROOT_POINT[1] + 1))
                left, top = min(left, ROOT_POINT[0]), min(top, ROOT_POINT[1])
                record['rect'] = [x + left, y + top, right - left, bottom - top]
                record['anchor'] = [ROOT_POINT[0] - left, ROOT_POINT[1] - top]
        style_output = ROOT / 'assets/styled-sprites' / f'c8-r{resource}.png'
        style_output.write_bytes(tuxedo.read_bytes())
        styles['atlases'].append({'character': 8, 'base_resource_id': resource, 'resource_id': 700 + resource,
            'source': style_output.name, 'anchors': [record['anchor'] for record in records],
            'reference_height': REFERENCE_HEIGHT})
        with Image.open(cream) as source:
            snow = Image.new('RGBA', source.size)
            mask = Image.new('L', source.size)
            annotations = []
            for index, record in enumerate(records):
                x, y, width, height = record['rect']
                eyes, local_mask = eye_material(source, record['rect'], 0, index)
                local = source.crop((x, y, x + width, y + height))
                painted, checks = white_coat(local, None, local_mask)
                if not checks['alpha_exact'] or not checks['skin_rgb_exact']:
                    raise ValueError('OddEye material bake changed geometry or skin')
                snow.paste(painted, (x, y))
                mask.paste(Image.fromarray(local_mask), (x, y))
                annotations.append({'frame': index, 'eyes': eyes})
            snow_path = ROOT / 'assets/styled-sprites' / f'c10-r{resource}.png'
            mask_path = snow_path.parent / 'odd-eye-masks' / snow_path.name
            snow.save(snow_path)
            mask.save(mask_path)
            styles['atlases'].append({'character': 10, 'base_resource_id': resource,
                'resource_id': 900 + resource, 'source': snow_path.name})
            odd_policy['atlases'].append({'base_resource_id': resource,
                'base_source': cream.relative_to(ROOT).as_posix(), 'source': snow_path.name,
                'input_sha256': sha(cream), 'output_sha256': sha(snow_path),
                'source_alpha_sha256': hashlib.sha256(source.getchannel('A').tobytes()).hexdigest(),
                'iris_mask': 'odd-eye-masks/' + snow_path.name, 'iris_mask_sha256': sha(mask_path),
                'alpha_exact': True, 'skin_rgb_exact': True})
            iris['atlases'].append({'base_resource_id': resource, 'source': snow_path.name, 'frames': annotations})
    manifest = DIRECTORY / 'manifest.json'
    write_json(manifest, {'schema': 2, 'clips': entries})
    write_json(styles_path, styles)
    odd_policy['additional_authoring_tool'] = 'tools/pack_video_actions.py'
    odd_policy['additional_authoring_tool_sha256'] = sha(Path(__file__))
    write_json(odd_policy_path, odd_policy)
    write_json(iris_path, iris)
    write_json(DIRECTORY / 'registration.json', evidence)
    header = ['// Generated by tools/pack_video_actions.py from annotated source-video paws.',
        '#pragma once', '#include "cat_deformation.hpp"', '#include <array>',
        'namespace cat::video_actions {', 'using Paws = std::array<cat::deform::Point,2>;']
    for character in ('cream', 'tuxedo'):
        points = evidence[character + '-reach']['paw_points_logical']
        header.append(f'inline constexpr std::array<Paws,{len(points)}> k{character.title()}Reach{{{{')
        for pair in points:
            header.append('    Paws{{' + ','.join('cat::deform::Point{' + ','.join(f'{value:.5f}' for value in point) + '}' for point in pair) + '}},')
        header.append('}};')
    header.extend(['inline Paws ReachPaws(unsigned character,unsigned frame) {',
        '    const auto& points=character==8?kTuxedoReach:kCreamReach;',
        '    return points[std::min<unsigned>(frame,points.size()-1)];', '}', '}'])
    (ROOT / 'src/video_action_contacts.generated.hpp').write_text('\n'.join(header) + '\n', encoding='utf-8', newline='\n')
    files = [p for p in sorted(DIRECTORY.rglob('*')) if p.is_file() and p.name not in ('manifest.json', 'provenance.json')]
    write_json(DIRECTORY / 'provenance.json', {'schema': 1, 'method': 'dropshot_video_extraction',
        'manifest_sha256': sha(manifest), 'precomputed_only': True,
        'files': [{'path': p.relative_to(DIRECTORY).as_posix(), 'sha256': sha(p)} for p in files]})
    print('Baked five action families for Cream and Tuxedo')


if __name__ == '__main__':
    main()
