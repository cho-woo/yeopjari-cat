#!/usr/bin/env python3
"""Render unchanged sprite drawings for a constant-speed art review.

This is a Pillow/FFmpeg technical review, not Windows GDI+ output or a desktop
recording. Source PNG bytes are never rewritten. All frames use one fixed
reference height; no per-frame bbox fit, body warp, dissolve or inbetweens.

The optional candidate JSON uses the same frame/loop fields as the importer.
Without one, the generated 48-pose candidate uses PROVISIONAL uniform timing
for inspection only; this is not a measured foot-contact registration.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CANDIDATE = Path('/workspace/generated_images/exec-01b8710b-38f3-43ef-ae11-a5aed9b01234.png')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def material_frames(config, source):
    """Read the explicit cells and clear only the runtime's alpha<=16 noise."""
    image = Image.open(source).convert('RGBA')
    width, height = image.size
    columns, rows = config['columns'], config['rows']
    count = config.get('frame_count', columns * rows)
    registration = config.get('frames')
    if registration is None:
        registration = []
        for index in range(count):
            col, row = index % columns, index // columns
            x, y = col * width // columns, row * height // rows
            x1, y1 = (col + 1) * width // columns, (row + 1) * height // rows
            registration.append({'rect': [x, y, x1-x, y1-y], 'anchor': config['anchor']})
    if len(registration) != count:
        raise ValueError('Frame count must equal the explicit registration length')
    drawings, metrics = [], []
    for index, frame in enumerate(registration):
        x, y, w, h = frame['rect']
        anchor = frame.get('anchor', config.get('anchor'))
        if not (0 <= x < width and 0 <= y < height and w > 0 and h > 0 and
                x+w <= width and y+h <= height and anchor is not None):
            raise ValueError(f'Invalid source rectangle/anchor at frame {index}')
        pixels = np.array(image.crop((x, y, x+w, y+h)), dtype=np.uint8)
        pixels[pixels[:, :, 3] <= 16] = 0
        solid = pixels[:, :, 3] > 200
        yy, xx = np.where(solid)
        if not len(xx):
            raise ValueError(f'Empty visible frame {index}')
        scale = 110.0 / config['reference_height']
        metrics.append({
            'frame': index, 'source_rect': frame['rect'], 'anchor': anchor,
            'solid_bbox': [int(xx.min()), int(yy.min()), int(xx.max())+1, int(yy.max())+1],
            'solid_floor_offset_logical_px': (float(yy.max())+1-anchor[1])*scale,
            'solid_pixels': int(solid.sum()),
            'material_rgba_sha256': hashlib.sha256(pixels.tobytes()).hexdigest(),
        })
        drawing = Image.fromarray(pixels)
        drawings.append((drawing.resize((round(w*scale), round(h*scale)), Image.Resampling.LANCZOS),
                         [anchor[0]*scale, anchor[1]*scale]))
    loop = config.get('sequence', {}).get('loop')
    if not loop:
        raise ValueError('A nonempty explicit sequence.loop is required')
    total = sum(step['ms'] for step in loop)
    if total <= 0 or any(step['ms'] <= 0 or not 0 <= step['frame'] < count for step in loop):
        raise ValueError('Invalid frame or weight in loop')
    return drawings, metrics, loop


def sample(loop, phase):
    cursor = phase * sum(step['ms'] for step in loop)
    for step in loop:
        if cursor < step['ms']:
            return step['frame']
        cursor -= step['ms']
    return loop[-1]['frame']


def font(size=15):
    for path in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                 '/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf'):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def render(baseline, candidate, baseline_source, candidate_source, output, fps=60, cycles=2, speed=22):
    output.mkdir(parents=True, exist_ok=True)
    data = [material_frames(baseline, baseline_source), material_frames(candidate, candidate_source)]
    stride = float(candidate['stride_pixels'])
    expected_seconds = cycles * stride / speed
    frame_count = math.ceil(expected_seconds * fps)
    trace = []
    selected = {}
    with tempfile.TemporaryDirectory(prefix='walk-art-review-') as temporary:
        for index in range(frame_count):
            seconds = index/fps
            travel = speed * seconds
            canvas = Image.new('RGBA', (760, 470), (235, 239, 243, 255))
            draw = ImageDraw.Draw(canvas)
            draw.text((12, 8), 'Sprite art review / analytical constant travel / NOT a Windows desktop recording',
                      fill=(30, 43, 57), font=font(13))
            draw.text((12, 30), f'{speed:g} logical px/s | {fps} output FPS | t={seconds:.3f}s | adult height=110',
                      fill=(30, 43, 57), font=font(13))
            for lane, (config, prepared) in enumerate(zip((baseline, candidate), data)):
                drawings, _, loop = prepared
                phase = (travel / config['stride_pixels']) % 1.0
                frame = sample(loop, phase)
                drawing, anchor = drawings[frame]
                ground = 235 if lane == 0 else 440
                center = 160 + travel
                draw.line((0, ground, 760, ground), fill=(152, 163, 174), width=1)
                label = config.get('label', 'Baseline 8 poses' if lane == 0 else 'Generated candidate')
                draw.text((12, 65 if lane == 0 else 270),
                          f'{label} | frame {frame:02d} | phase {phase:.4f} | stride {config["stride_pixels"]:g}',
                          fill=(30, 43, 57), font=font())
                if lane == 1:
                    draw.text((12, 292), config.get('timing_status', 'Explicit candidate timing'),
                              fill=(125, 65, 25), font=font(12))
                canvas.alpha_composite(drawing, (round(center-anchor[0]), round(ground-anchor[1])))
                trace.append({'time_seconds': seconds, 'lane': lane, 'travel_logical_px': travel,
                              'root_x': center, 'ground_y': ground, 'phase': phase, 'frame': frame})
            canvas.convert('RGB').save(Path(temporary) / f'frame-{index:05d}.png')
            if index in (0, round(frame_count*.25), round(frame_count*.5), frame_count-1):
                selected[index] = canvas.convert('RGB')
        movie = output / 'walk-art-constant-22.mp4'
        subprocess.run(['ffmpeg', '-y', '-v', 'error', '-framerate', str(fps), '-i',
                        str(Path(temporary) / 'frame-%05d.png'), '-c:v', 'libx264', '-pix_fmt',
                        'yuv420p', '-movflags', '+faststart', str(movie)], check=True)
    for index, image in selected.items():
        image.save(output / f'constant-frame-{index:05d}.png')
    with (output / 'constant-trace.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(trace[0]))
        writer.writeheader(); writer.writerows(trace)
    proof = {
        'render_type': 'Pillow sprite compositing; analytical constant travel; not GDI+, Box2D or desktop recording',
        'source_pngs_rewritten': False, 'display_height_logical_px': 110,
        'speed_logical_px_per_second': speed, 'output_fps': fps, 'output_frames': frame_count,
        'output_duration_seconds': frame_count/fps, 'requested_cycles': cycles,
        'application_active_target': '16 ms display interval, approximately 60 Hz; separate from this video',
        'no_new_drawings_created_by_renderer': True,
        'sources': [{'path': str(source), 'sha256': sha(source), 'registered_frame_count': len(d[0]),
                     'stride_pixels': config['stride_pixels'], 'reference_height': config['reference_height'],
                     'timing_status': config.get('timing_status', 'registered source timing'),
                     'loop': d[2], 'frames': d[1]}
                    for source, config, d in zip((baseline_source, candidate_source), (baseline, candidate), data)],
    }
    (output / 'art-review.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--baseline-manifest', type=Path, default=ROOT/'assets/care-sprites/manifest.json')
    parser.add_argument('--baseline-source', type=Path)
    parser.add_argument('--candidate', type=Path, default=DEFAULT_CANDIDATE)
    parser.add_argument('--candidate-metadata', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'build/walk-art-review/constant-22')
    parser.add_argument('--fps', type=int, default=60, choices=(20, 30, 60))
    parser.add_argument('--cycles', type=int, default=2)
    args = parser.parse_args()
    manifest = json.loads(args.baseline_manifest.read_text())
    baseline = next(clip for clip in manifest['clips'] if clip['clip'] == 'walk' and clip['character'] == 0)
    baseline_source = args.baseline_source or args.baseline_manifest.parent / baseline['source']
    if args.candidate_metadata:
        candidate = json.loads(args.candidate_metadata.read_text())
        if 'clips' in candidate:
            candidate = next(clip for clip in candidate['clips'] if clip['clip'] == 'walk' and clip['character'] == 0)
    else:
        candidate = {'label': 'Generated 48-pose candidate - NOT APPROVED', 'columns': 8, 'rows': 6,
                     'frame_count': 48, 'reference_height': 136, 'anchor': [93, 158], 'stride_pixels': 64,
                     'sequence': {'loop': [{'frame': i, 'ms': 1} for i in range(48)]},
                     'timing_status': 'PROVISIONAL uniform weights for inspection only; not contact-calibrated'}
    if args.cycles < 1:
        parser.error('--cycles must be positive')
    proof = render(baseline, candidate, baseline_source, args.candidate, args.output, args.fps, args.cycles)
    print(json.dumps({key: proof[key] for key in ('render_type', 'output_frames', 'output_fps', 'output_duration_seconds')}))


if __name__ == '__main__':
    main()
