#!/usr/bin/env python3
"""Read-only local raster coverage near rig-exported planted sole controls.

This does not identify/track painted feet optically. A narrow alpha sample can
include another overlapping paw; the report states its source-pixel sample
window and flags the bottom row reaching that window. Numerical contact and
local paint coverage do not grant visual approval.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--annotations', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text())
    entry = document.get('entry') or next(c for c in document['clips'] if c['clip'] == 'walk')
    source = args.manifest.parent/entry['source']
    data = source.read_bytes(); image = Image.open(source).convert('RGBA')
    contacts = json.loads(args.annotations.read_text())
    samples = contacts['annotations']+contacts.get('auxiliary_annotations', [])
    scale = 110/entry['reference_height']
    rows = []
    for sample in samples:
        frame = entry['frames'][sample['frame']]
        x0, y0, width, height = frame['rect']
        alpha = image.crop((x0, y0, x0+width, y0+height)).getchannel('A')
        for name, foot in sample['feet'].items():
            if not foot['planted']:
                continue
            cx, cy = round(foot['x']), round(foot['y'])
            left, right = max(0, cx-2), min(width-1, cx+2)
            top, bottom = max(0, cy-8), min(height-1, cy+8)
            solid = [(x, y) for y in range(top, bottom+1) for x in range(left, right+1)
                     if alpha.getpixel((x, y)) >= 200]
            solid_bottom = max((y for _, y in solid), default=None)
            offset = solid_bottom-foot['y'] if solid_bottom is not None else None
            ground_offset = solid_bottom-frame['anchor'][1] if solid_bottom is not None else None
            rows.append({'frame': sample['frame'], 'foot_control_id': name,
                         'controller_source': [foot['x'], foot['y']],
                         'sample_box_inclusive': [left, top, right, bottom],
                         'solid_bottom_y': solid_bottom,
                         'solid_bottom_minus_controller_source_px': offset,
                         'solid_bottom_minus_ground_logical_px': ground_offset*scale if ground_offset is not None else None,
                         'sample_window_bottom_reached': solid_bottom == bottom,
                         'requires_visual_review': offset is None or abs(offset) > 2 or abs(ground_offset) > 2 or solid_bottom == bottom})
    offsets = [row['solid_bottom_minus_controller_source_px'] for row in rows
               if row['solid_bottom_minus_controller_source_px'] is not None]
    result = {'source': str(source), 'source_sha256': hashlib.sha256(data).hexdigest(),
              'measurement': 'local solid alpha bottom near named rig sole controls; not optical foot tracking',
              'alpha_threshold': 200, 'horizontal_sample_radius_source_px': 2,
              'vertical_sample_radius_source_px': 8, 'review_offset_limit_source_px': 2,
              'overlapping_paw_identity_resolved': False, 'visual_approved': False,
              'production_approved': False, 'planted_control_samples': len(rows),
              'samples_requiring_visual_review': sum(row['requires_visual_review'] for row in rows),
              'maximum_below_control_source_px': max([0]+offsets),
              'maximum_above_control_source_px': max([0]+[-v for v in offsets]), 'samples': rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'samples'}, indent=2))


if __name__ == '__main__':
    main()
