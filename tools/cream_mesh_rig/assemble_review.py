#!/usr/bin/env python3
"""Join an existing calibrated loop and separately rendered neutral review pose.

Reads FOLDER/{calibrated-manifest,manifest,annotations}.json and the referenced
PNG files. All geometry and PNG hashes must agree before review-manifest.json
is written. It never invents timing, changes source pixels or grants approval.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path

FEET = {'near_front', 'far_front', 'near_rear', 'far_rear'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def clip(document, label):
    require(document.get('schema') == 2 and isinstance(document.get('clips'), list) and
            len(document['clips']) == 1, f'{label}: exactly one schema2 clip is required')
    entry = document['clips'][0]
    require(entry.get('clip') == 'walk' and entry.get('character') == 0,
            f'{label}: expected the Cream Walk registration')
    return entry


def source_hash(folder, entry, label):
    require(isinstance(entry.get('source'), str) and entry['source'], f'{label}: missing source PNG')
    path = folder/entry['source']
    data = path.read_bytes()
    require(data.startswith(b'\x89PNG\r\n\x1a\n'), f'{label}: source is not a PNG')
    digest = hashlib.sha256(data).hexdigest()
    if 'sha256' in entry:
        require(entry['sha256'] == digest, f'{label}: source PNG hash mismatch')
    return digest


def assemble(folder):
    folder = Path(folder)
    packed = json.loads((folder/'manifest.json').read_text())
    calibrated = json.loads((folder/'calibrated-manifest.json').read_text())
    annotations = json.loads((folder/'annotations.json').read_text())
    full = clip(packed, 'packed manifest')
    loop = clip(calibrated, 'calibrated manifest')
    count = loop.get('frame_count')
    require(type(count) is int and count > 0 and len(loop.get('frames', [])) == count,
            'Calibrated loop frame count is inconsistent')
    require(len(annotations.get('annotations', [])) == count and
            [sample.get('frame') for sample in annotations['annotations']] == list(range(count)),
            'Loop annotations must identify every calibrated frame in order')
    auxiliary = annotations.get('auxiliary_annotations', [])
    require(isinstance(auxiliary, list) and len(auxiliary) == 1 and auxiliary[0].get('frame') == count,
            'Exactly one separate neutral annotation at frameN is required')
    feet = auxiliary[0].get('feet', {})
    require(set(feet) == FEET and all(point.get('planted') is True and
            all(type(point.get(axis)) in (int, float) and math.isfinite(point[axis]) for axis in ('x', 'y'))
            for point in feet.values()), 'Neutral annotation must plant all four named paws')
    require(full.get('frame_count') == count+1 and len(full.get('frames', [])) == count+1 and
            full.get('standing_frame') == count,
            'Packed manifest must append exactly one neutral frame outside the loop')
    require([step.get('frame') for step in full.get('sequence', {}).get('loop', [])] == list(range(count)),
            'Packed loop must exclude the separate neutral pose')
    require(full['frames'][:count] == loop['frames'], 'Packed/calibrated source rectangles or anchors differ')
    raw = annotations.get('entry', {})
    require(raw.get('frame_count') == count and raw.get('frames') == loop['frames'],
            'Annotation geometry must remain loop-only and match the calibrated frames')
    for key in ('clip', 'character', 'columns', 'rows', 'reference_height'):
        require(full.get(key) == loop.get(key) == raw.get(key), f'Geometry metadata mismatch: {key}')
    if 'root_anchor' in auxiliary[0]:
        require(auxiliary[0]['root_anchor'] == full['frames'][count]['anchor'],
                'Neutral root anchor differs from its registered frame anchor')
    sequence = loop.get('sequence', {})
    steps = sequence.get('loop', [])
    require([step.get('frame') for step in steps] == list(range(count)) and
            all(type(step.get('ms')) is int and step['ms'] > 0 for step in steps),
            'Calibrated loop must provide positive distance weights for all actual poses')
    require(not sequence.get('intro') and not sequence.get('outro'),
            'Calibrated periodic loop must not include auxiliary entrance or exit poses')
    stride = loop.get('stride_pixels')
    require(type(stride) in (int, float) and math.isfinite(stride) and stride > 0,
            'A measured positive calibrated stride is required')
    digest = source_hash(folder, full, 'packed manifest')
    require(loop.get('sha256') == digest and source_hash(folder, loop, 'calibrated manifest') == digest and
            source_hash(folder, raw, 'annotations') == digest,
            'Packed, calibrated and annotated source PNGs must have the same hash')
    combined = copy.deepcopy(packed)
    result = combined['clips'][0]
    for key in ('sequence', 'stride_pixels', 'sha256'):
        result[key] = copy.deepcopy(loop[key])
    # Commit after complete validation. Source/calibration/annotation files are
    # immutable inputs; the extra neutral frame never enters the copied loop.
    output = folder/'review-manifest.json'
    output.write_text(json.dumps(combined, indent=2)+'\n')
    return {'review_manifest': str(output), 'source_png_sha256': digest,
            'loop_frames': count, 'auxiliary_neutral_frame': count, 'registered_frames': count+1,
            'calibrated_loop_preserved': True, 'calibration_rerun': False,
            'visual_approved': False, 'production_approved': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    args = parser.parse_args()
    try:
        result = assemble(args.folder)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Review manifest assembly failed: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
