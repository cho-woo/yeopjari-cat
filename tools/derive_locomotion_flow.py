#!/usr/bin/env python3
"""Precompute motion samples from the eight intact original Walk/Run drawings.

These are derived geometric resamples, NOT 64 newly drawn poses. No source PNG
is changed, no limb is cut out, and no completed sprites are alpha-dissolved.
Bidirectional DIS flow moves the complete source material into intermediate
coordinates. A single interpolated signed-distance contour supplies coverage;
only already motion-aligned material RGB is fused inside that contour.

Baking needs numpy, scipy, Pillow and opencv-python-headless. For example:
  python3 -m pip install --target build/flow-deps --no-deps opencv-python-headless
  python3 tools/derive_locomotion_flow.py --dependency-path build/flow-deps

The checked-in PNG is the runtime resource. --check needs only Python's standard
library and the existing PNG importer; it never invokes flow or image editing.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/derived-motion'
SOURCE = DIRECTORY / 'source-registration.json'
OUTPUT = DIRECTORY / 'c0-locomotion-flow.png'
MANIFEST = DIRECTORY / 'manifest.json'
PROVENANCE = DIRECTORY / 'provenance.json'
APPEARANCE = DIRECTORY / 'appearance-manifest.json'
CELL = 314
SUBFRAMES = 8
COUNT = 64


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + '\n')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_source(path):
    document = json.loads(path.read_text())
    require(document.get('schema') == 2, 'Expected schema-2 source registration')
    clips = document.get('clips', [])
    require(len(clips) == 2 and {c['clip'] for c in clips} == {'walk', 'run'},
            'Source registration must contain one Walk and one Run')
    for clip in clips:
        require(clip['character'] == 0 and clip['frame_count'] == 8 and
                len(clip['frames']) == 8 and clip['reference_height'] == 221,
                'Expected eight original Cream drawings at reference height221')
        original = (path.parent / clip['source']).resolve()
        require(digest(original.read_bytes()) == clip['sha256'], 'Original source PNG hash changed')
    return clips


def source_snapshot():
    """Capture registration once; later app manifests may select derived art."""
    if SOURCE.exists():
        return
    original_path = ROOT / 'assets/care-sprites/manifest.json'
    original = json.loads(original_path.read_text())
    clips = []
    for name in ('walk', 'run'):
        clip = copy.deepcopy(next(c for c in original['clips'] if c['clip'] == name))
        clip['source'] = '../care-sprites/' + clip['source']
        clips.append(clip)
    write_json(SOURCE, {'schema': 2, 'clips': clips})


def bake(source_path, appearance_source=None, appearance_resource=508):
    import cv2
    import numpy as np
    import PIL
    from PIL import Image
    import scipy
    from scipy.ndimage import distance_transform_edt

    cv2.setNumThreads(1)
    clips = read_source(source_path)
    output = DIRECTORY / 'c8-locomotion-flow.png' if appearance_source else OUTPUT
    atlas = Image.new('RGBA', (8 * CELL, 16 * CELL))
    entries, reports = [], []
    for clip_index, name in enumerate(('walk', 'run')):
        clip = next(c for c in clips if c['clip'] == name)
        image = Image.open(appearance_source or source_path.parent / clip['source']).convert('RGBA')
        require(image.size == (1254, 1254), 'Source artwork must retain the original1254x1254 canvas')
        prepared = []
        for metadata in clip['frames']:
            x, y, width, height = metadata['rect']
            rgba = np.zeros((CELL, CELL, 4), np.uint8)
            rgba[:height, :width] = np.array(image.crop((x, y, x + width, y + height)))
            rgba[rgba[:, :, 3] <= 16] = 0
            alpha = rgba[:, :, 3] / 255.
            mask = alpha > .15
            _, nearest = distance_transform_edt(~mask, return_indices=True)
            # Extend the edge material only in invisible padding. Intermediate
            # contours can expose it without pulling black transparent RGB in.
            color = rgba[:, :, :3].copy()
            color[~mask] = color[nearest[0][~mask], nearest[1][~mask]]
            sdf = distance_transform_edt(mask) - distance_transform_edt(~mask)
            gray = cv2.cvtColor(rgba[:, :, :3], cv2.COLOR_RGB2GRAY) * alpha + 96 * (1 - alpha)
            feature = (gray * .6 + alpha * 255 * .4).astype(np.uint8)
            prepared.append((rgba, color.astype(np.float32), sdf.astype(np.float32), feature))

        yy, xx = np.mgrid[:CELL, :CELL].astype(np.float32)

        def flow(first, second):
            estimator = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
            estimator.setFinestScale(0)
            # RUN has larger occlusions. Its wider velocity regularization
            # prevents inverse-map folds at the haunch and chest contours.
            sigma = 4.0 if name == 'run' else 1.2
            return cv2.GaussianBlur(estimator.calc(first, second, None), (0, 0), sigma)

        def remap(array, x, y):
            return cv2.remap(array, x, y, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)

        def interpolate(first, second, forward, backward, amount):
            if amount == 0:
                return first[0].copy()
            ax, ay, bx, by = xx.copy(), yy.copy(), xx.copy(), yy.copy()
            # Damped inverse mapping avoids folds/oscillation at paw contours.
            for _ in range(16):
                f, b = remap(forward, ax, ay), remap(backward, bx, by)
                ax = (ax + xx - amount * f[:, :, 0]) * .5
                ay = (ay + yy - amount * f[:, :, 1]) * .5
                bx = (bx + xx - (1 - amount) * b[:, :, 0]) * .5
                by = (by + yy - (1 - amount) * b[:, :, 1]) * .5
            first_color, second_color = remap(first[1], ax, ay), remap(second[1], bx, by)
            first_sdf, second_sdf = remap(first[2], ax, ay), remap(second[2], bx, by)
            sdf = first_sdf * (1 - amount) + second_sdf * amount
            coverage = np.clip(sdf + .5, 0, 1)
            result = np.zeros_like(first[0])
            # This combines aligned material, not two complete alpha sprites.
            result[:, :, :3] = np.clip(first_color * (1 - amount) + second_color * amount,
                                       0, 255).astype(np.uint8)
            result[:, :, 3] = np.rint(coverage * 255).astype(np.uint8)
            result[coverage == 0] = 0
            return result

        frames, records = [], []
        for original_index in range(8):
            print(f'{name}: original interval {original_index}-> {(original_index + 1) % 8}', flush=True)
            first, second = prepared[original_index], prepared[(original_index + 1) % 8]
            forward, backward = flow(first[3], second[3]), flow(second[3], first[3])
            for subframe in range(SUBFRAMES):
                frame = original_index * SUBFRAMES + subframe
                rgba = interpolate(first, second, forward, backward, subframe / SUBFRAMES)
                frames.append(rgba)
                cell_x, cell_y = frame % 8 * CELL, (frame // 8 + clip_index * 8) * CELL
                atlas.paste(Image.fromarray(rgba), (cell_x, cell_y))
                records.append({'rect': [cell_x, cell_y, CELL, CELL],
                                'anchor': clip['frames'][original_index]['anchor']})
        require(all(np.array_equal(frames[index * SUBFRAMES], prepared[index][0]) for index in range(8)),
                'A source key drawing changed')
        unique = len({digest(rgba.tobytes()) for rgba in frames})
        require(unique == COUNT, 'Derived samples must contain actual distinct motion')
        # Measure equal-phase frame changes at actual display size. This is a
        # temporal sampling diagnostic, not an anatomical contact claim.
        display = np.stack([np.array(Image.fromarray(f).resize((156, 156), Image.Resampling.LANCZOS))
                            for f in frames]).astype(float)
        a = display[:, :, :, 3:4] / 255
        rgb = display[:, :, :, :3] * a + 235 * (1 - a)
        changes = np.mean(abs(rgb - np.roll(rgb, 1, axis=0)), axis=(1, 2, 3))
        held = rgb[(np.arange(COUNT) // SUBFRAMES) * SUBFRAMES]
        held_changes = np.mean(abs(held - np.roll(held, 1, axis=0)), axis=(1, 2, 3))
        entries.append({'clip': name, 'character': 0, 'source': output.name,
                        'columns': 8, 'rows': 16, 'frame_count': COUNT,
                        'reference_height': 221, 'frames': records,
                        'sequence': {'intro': [], 'loop': [{'frame': i, 'ms': 125} for i in range(COUNT)],
                                     'outro': [], 'finish': 'complete', 'duration_ms': 0, 'events': []},
                        'stride_pixels': clip['stride_pixels'],
                        'standing_frame': clip['standing_frame'] * SUBFRAMES})
        reports.append({'clip': name, 'frame_count': COUNT, 'derived_frame_count': COUNT,
                        'source_frame_count': 8, 'original_drawings': 8,
                        'samples_per_original_interval': SUBFRAMES, 'unique_derived_frames': unique,
                        'source_key_frames_exact_after_runtime_alpha_cleanup': True,
                        'source_key_indices': [i * SUBFRAMES for i in range(8)],
                        'source_key_rgba_sha256': [digest(p[0].tobytes()) for p in prepared],
                        'anchor': clip['frames'][0]['anchor'], 'reference_height': 221,
                        'stride_pixels': clip['stride_pixels'],
                        'maximum_display_rgb_change_64_phase_steps': float(changes.max()),
                        'maximum_held_source_rgb_change_64_phase_steps': float(held_changes.max())})
    DIRECTORY.mkdir(parents=True, exist_ok=True)
    atlas.save(output, optimize=True)
    for entry in entries:
        entry['sha256'] = digest(output.read_bytes())
    if appearance_source:
        # Preserve the actual authored eight-drawing source before the style
        # importer installs the derived atlas under its runtime filename.
        retained = DIRECTORY / 'source-c8-original.png'
        retained.write_bytes(appearance_source.read_bytes())
        write_json(APPEARANCE, {'schema': 1, 'design': 'authored-tuxedo-motion-flow-v1', 'atlases': [{
            'character': 8, 'base_resource_id': 400, 'resource_id': appearance_resource,
            'source': output.name, 'sha256': digest(output.read_bytes()),
            'image_width': 8 * CELL, 'image_height': 16 * CELL,
        }]})
        report = json.loads(PROVENANCE.read_text())
        report['appearances'] = [{
            'character': 8, 'source_frame_count': 8, 'original_drawings': 8,
            'derived_frames_per_clip': COUNT, 'original_source': str(retained.relative_to(ROOT)),
            'original_source_sha256': digest(retained.read_bytes()),
            'atlas': {'source': output.name, 'sha256': digest(output.read_bytes()),
                      'width': 8 * CELL, 'height': 16 * CELL},
            'clips': reports,
        }]
        report['appearance_manifest_sha256'] = digest(APPEARANCE.read_bytes())
        write_json(PROVENANCE, report)
        return check()
    write_json(MANIFEST, {'schema': 2, 'clips': entries})
    original = (source_path.parent / clips[0]['source']).resolve()
    write_json(PROVENANCE, {
        'schema': 1, 'method': 'bidirectional_dense_flow_geometric_resampling',
        'generator': 'tools/derive_locomotion_flow.py', 'generator_sha256': digest(Path(__file__).read_bytes()),
        'source_registration': str(source_path.relative_to(ROOT)),
        'source_registration_sha256': digest(source_path.read_bytes()),
        'original_source': str(original.relative_to(ROOT)), 'original_source_sha256': digest(original.read_bytes()),
        'atlas': {'source': OUTPUT.name, 'sha256': digest(OUTPUT.read_bytes()),
                  'width': 8 * CELL, 'height': 16 * CELL},
        'derived_manifest_sha256': digest(MANIFEST.read_bytes()),
        'new_hand_drawn_art': False, 'precomputed_only': True, 'whole_sprite_alpha_blend': False,
        'one_intermediate_signed_distance_alpha_contour': True, 'motion_aligned_material_rgb_fusion': True,
        'runtime_flow_or_image_processing': False, 'per_frame_bbox_scaling': False,
        'dependencies': {'opencv': cv2.__version__, 'numpy': np.__version__,
                         'scipy': scipy.__version__, 'Pillow': PIL.__version__},
        'parameters': {'flow': 'DIS_MEDIUM', 'finest_scale': 0,
                       'flow_gaussian_sigma': {'walk': 1.2, 'run': 4.0},
                       'inverse_iterations': 16, 'inverse_damping': .5, 'cell_pixels': CELL,
                       'flow_feature': '0.6*luma_on_gray96 + 0.4*alpha255'},
        'clips': reports,
    })
    return check()


def check():
    from import_care_sprites import load_entries, png_alpha

    report = json.loads(PROVENANCE.read_text())
    require(report['generator_sha256'] == digest(Path(__file__).read_bytes()), 'Generator drift')
    for file_key, hash_key in [('source_registration', 'source_registration_sha256'),
                               ('original_source', 'original_source_sha256')]:
        require(digest((ROOT / report[file_key]).read_bytes()) == report[hash_key], f'{file_key} drift')
    require(digest(MANIFEST.read_bytes()) == report['derived_manifest_sha256'], 'Derived manifest drift')
    data = OUTPUT.read_bytes()
    require(digest(data) == report['atlas']['sha256'], 'Derived atlas drift')
    require(struct.unpack_from('>II', data, 16) == (8 * CELL, 16 * CELL), 'Derived atlas dimensions')
    entries = load_entries(MANIFEST)
    sources = read_source(ROOT / report['source_registration'])
    require(len(entries) == 2 and {e['clip'] for e in entries} == {'walk', 'run'}, 'Derived clips')
    for entry in entries:
        original = next(c for c in sources if c['clip'] == entry['clip'])
        require(len(entry['frames']) == COUNT and entry['reference_height'] == 221 and
                entry['stride_pixels'] == original['stride_pixels'], 'Derived count/scale/stride')
        require(all(f['rect'][2:] == [CELL, CELL] and f['anchor'] == original['frames'][0]['anchor']
                    for f in entry['frames']), 'Derived fixed canvas/anchor')
        require(entry['standing_frame'] == original['standing_frame'] * SUBFRAMES, 'Standing registration')
        require([s['frame'] for s in entry['program']['canonical']['loop']] == list(range(COUNT)),
                'Derived cycle order')
    if report.get('appearances'):
        require(digest(APPEARANCE.read_bytes()) == report['appearance_manifest_sha256'],
                'Derived appearance manifest drift')
        styled = json.loads(APPEARANCE.read_text())['atlases']
        for actor in report['appearances']:
            original = ROOT / actor['original_source']
            require(digest(original.read_bytes()) == actor['original_source_sha256'], 'Authored original drift')
            appearance = next(a for a in styled if a['character'] == actor['character'] and a['base_resource_id'] == 400)
            require(actor['source_frame_count'] == actor['original_drawings'] == 8 and
                    actor['derived_frames_per_clip'] == COUNT, 'Appearance source/derived counts')
            require(actor['atlas']['source'] == appearance['source'] and
                    actor['atlas']['sha256'] == appearance['sha256'], 'Appearance atlas binding')
            pixels = (DIRECTORY / actor['atlas']['source']).read_bytes()
            require(digest(pixels) == actor['atlas']['sha256'], 'Derived appearance atlas drift')
            width, height, _ = png_alpha(pixels)
            require((width, height) == (8 * CELL, 16 * CELL), 'Derived appearance dimensions')
    return {'status': 'verified', 'clips': 2, 'original_drawings_per_clip': 8,
            'derived_frames_per_clip': COUNT, 'png_sha256': report['atlas']['sha256'],
            'appearance_characters': [a['character'] for a in report.get('appearances', [])],
            'precomputed_only': True, 'original_source_unchanged': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--dependency-path', type=Path, help='Optional project-local Python package directory')
    parser.add_argument('--source-registration', type=Path, default=SOURCE)
    parser.add_argument('--appearance-source', type=Path, help='Actual authored Tuxedo original1254x1254 atlas')
    parser.add_argument('--appearance-resource-id', type=int, default=508)
    args = parser.parse_args()
    if args.dependency_path:
        sys.path.insert(0, str(args.dependency_path.resolve()))
    try:
        if not args.check:
            source_snapshot()
        if args.check:
            result = check()
        else:
            result = bake(args.source_registration.resolve())
            if args.appearance_source:
                result = bake(args.source_registration.resolve(), args.appearance_source.resolve(),
                              args.appearance_resource_id)
    except (OSError, ValueError, ImportError) as error:
        print(f'Locomotion derivation failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
