#!/usr/bin/env python3
"""Review existing mesh-rig PNGs; never create artwork or modify source pixels.

Short review first:
  python3 tools/cream_mesh_rig/review.py --mode short
Full review after the same source has been baked/calibrated:
  python3 tools/cream_mesh_rig/review.py --mode full --manifest PATH --annotations PATH
Short/full views of exactly the same final loop (no additional pose artwork):
  python3 tools/cream_mesh_rig/review.py --mode all --short-from-full --manifest PATH

These are Pillow/FFmpeg composites of actual supplied poses, NOT Windows GDI+
or desktop recordings. The clean comparison keeps the adult110logicalpx
reference and22logicalpx/s travel. No dissolve, pose interpolation, per-frame
bbox fitting or root warp is used. Sole markers come only from explicit rig
annotations; they are not detected or inferred from image bounds.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
from typing import Callable

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FOOT_NAMES = ('near_front', 'far_front', 'near_rear', 'far_rear')
FOOT_LABELS = {'near_front': 'NF', 'far_front': 'FF', 'near_rear': 'NR', 'far_rear': 'FR'}
BG = (235, 239, 243, 255)
INK = (30, 43, 57)
GROUND = (150, 163, 176)
PLANTED = (25, 145, 72)
SWING = (218, 112, 18)
ROOT_COLOR = (45, 105, 190)


def review_status(draw, width):
    """This bake is a rejected visual candidate, available only for review."""
    left = width-350
    draw.rectangle((left, 61, width-12, 87), fill=(145, 34, 34))
    draw.text((left+7, 67), 'UNAPPROVED / DIAGNOSTIC + HANDOFF',
              fill=(255, 255, 255), font=font(12))


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def font(size=14):
    for name in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                 '/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf'):
        if Path(name).exists():
            return ImageFont.truetype(name, size)
    return ImageFont.load_default()


def read_entry(path: Path):
    data = json.loads(path.read_text())
    if 'entry' in data:
        return data['entry']
    if 'clips' in data:
        return next(e for e in data['clips'] if e['clip'] == 'walk' and e.get('character', 0) == 0)
    return data


def material(image):
    a = np.array(image.convert('RGBA'), dtype=np.uint8)
    a[a[:, :, 3] <= 16] = 0  # Same transparent-noise cutoff as the real loader.
    return Image.fromarray(a)


class Poses:
    def __init__(self, entry, source_root: Path, frames_dir: Path | None = None):
        self.entry = entry
        self.reference = float(entry['reference_height'])
        self.stride = float(entry['stride_pixels'])
        if not self.reference > 0 or not self.stride > 0:
            raise ValueError('A positive common reference height and measured stride are required')
        self.loop = entry['sequence']['loop']
        if not self.loop:
            raise ValueError('An explicit nonempty loop is required')
        self.weights = [int(s['ms']) for s in self.loop]
        if min(self.weights) <= 0:
            raise ValueError('Loop distance weights must be positive')
        self.cumulative = np.cumsum(self.weights).tolist()
        self.total = self.cumulative[-1]
        self.images = []
        self.anchors = []
        self.inputs = []
        atlas_path = source_root / entry['source']
        atlas = None
        if frames_dir is None:
            if not atlas_path.is_file():
                raise ValueError(f'Missing source atlas: {atlas_path}; use --frames-dir for actual PNG sequence')
            atlas = Image.open(atlas_path).convert('RGBA')
            self.inputs.append({'path': str(atlas_path), 'sha256': sha(atlas_path)})
        for index, frame in enumerate(entry['frames']):
            anchor = frame.get('anchor', entry.get('anchor'))
            if not isinstance(anchor, list) or len(anchor) != 2:
                raise ValueError(f'Explicit common-coordinate anchor required at frame {index}')
            if frames_dir is not None:
                path = frames_dir / f'frame-{index:04d}.png'
                if not path.is_file():
                    # A separate auxiliary standing pose need not be in loop.
                    if index not in [s['frame'] for s in self.loop]:
                        self.images.append(None); self.anchors.append(anchor); continue
                    raise ValueError(f'Missing actual rig pose: {path}')
                image = Image.open(path)
                self.inputs.append({'path': str(path), 'sha256': sha(path)})
            else:
                x, y, w, h = frame['rect']
                if min(x, y) < 0 or min(w, h) <= 0 or x+w > atlas.width or y+h > atlas.height:
                    raise ValueError(f'Invalid explicit source rectangle at frame {index}')
                image = atlas.crop((x, y, x+w, y+h))
            self.images.append(material(image)); self.anchors.append(anchor)
        for step in self.loop:
            if not 0 <= step['frame'] < len(self.images) or self.images[step['frame']] is None:
                raise ValueError('Loop references a missing actual image')
        self.scaled = {}

    def sample(self, phase):
        cursor = (phase % 1) * self.total
        index = min(len(self.loop)-1, bisect.bisect_right(self.cumulative, cursor))
        return self.loop[index]['frame']

    def drawing(self, index, zoom=1):
        key = (index, zoom)
        if key not in self.scaled:
            scale = 110 / self.reference * zoom
            image = self.images[index]
            self.scaled[key] = (image.resize((round(image.width*scale), round(image.height*scale)),
                                            Image.Resampling.LANCZOS),
                                [value*scale for value in self.anchors[index]])
        return self.scaled[key]


def annotations_by_frame(document):
    result = {}
    for item in document.get('annotations', []) + document.get('auxiliary_annotations', []):
        if item['frame'] in result:
            raise ValueError('Duplicate rig annotation frame')
        if set(item['feet']) != set(FOOT_NAMES):
            raise ValueError('Rig annotations must identify all four feet explicitly')
        result[item['frame']] = item
    return result


def ground(draw, width, y, origin, zoom=1):
    draw.line((0, y, width, y), fill=GROUND, width=1)
    low = math.floor(-origin/zoom/10)*10
    high = math.ceil((width-origin)/zoom/10)*10
    for world in range(low, high+1, 10):
        x = origin + world*zoom
        draw.line((x, y, x, y+5), fill=GROUND, width=1)
        if world % 50 == 0:
            draw.text((x+2, y+7), str(world), fill=GROUND, font=font(10))


def paste(canvas, poses, index, center, floor, zoom=1):
    image, anchor = poses.drawing(index, zoom)
    # Fixed whole-clip scaling. Round only final display placement, like a
    # raster viewport; do not move a source image to fit its painted sole.
    canvas.alpha_composite(image, (round(center-anchor[0]), round(floor-anchor[1])))


def marker(draw, poses, index, annotation, root_world, origin, floor, zoom, names, trace, seconds, scene):
    scale = 110 / poses.reference
    anchor = poses.anchors[index]
    for number, name in enumerate(names):
        foot = annotation['feet'][name]
        world_x = root_world + (foot['x']-anchor[0])*scale
        logical_y = (foot['y']-anchor[1])*scale
        x, y = origin+world_x*zoom, floor+logical_y*zoom
        color = PLANTED if foot['planted'] else SWING
        radius = 3 if zoom == 1 else 5
        draw.ellipse((x-radius, y-radius, x+radius, y+radius), outline=color, width=1)
        label_y = floor+27+number*16
        draw.line((x, y+radius, x, label_y), fill=color, width=1)
        draw.text((x+4, label_y-5), FOOT_LABELS[name], fill=color, font=font(11 if zoom == 1 else 13))
        trace.append({'scene': scene, 'time_seconds': seconds, 'frame': index, 'foot': name,
                      'planted': bool(foot['planted']), 'root_world_logical_x': root_world,
                      'sole_world_logical_x': world_x, 'sole_logical_height': -logical_y})


def encode(path: Path, frames: int, fps: int, size, make_frame: Callable):
    command = ['ffmpeg', '-y', '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'rgb24',
               '-video_size', f'{size[0]}x{size[1]}', '-framerate', str(fps), '-i', 'pipe:0',
               '-an', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(path)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    selected = {}
    try:
        for i in range(frames):
            image = make_frame(i).convert('RGB')
            if image.size != size:
                raise ValueError('Renderer returned inconsistent canvas dimensions')
            process.stdin.write(image.tobytes())
            if i in {0, frames//4, frames//2, frames-1}:
                selected[i] = image.copy()
        process.stdin.close()
        error = process.stderr.read().decode(errors='replace')
        code = process.wait()
        if code:
            raise RuntimeError(f'FFmpeg failed: {error}')
    except BaseException:
        process.kill(); process.wait(); raise
    for index, image in selected.items():
        image.save(path.parent / f'{path.stem}-{index:05d}.png')
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                      '-show_entries', 'stream=codec_name,pix_fmt,width,height,nb_frames,r_frame_rate',
                      '-show_entries', 'format=duration', '-of', 'json', str(path)], text=True))
    assert int(probe['streams'][0]['nb_frames']) == frames
    return {'file': path.name, 'sha256': sha(path), 'frames': frames, 'output_fps': fps,
            'duration_seconds': float(probe['format']['duration']), 'probe': probe}


def write_trace(output, trace, mode):
    if trace:
        with (output/f'{mode}-sole-review-trace.csv').open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=list(trace[0]))
            writer.writeheader(); writer.writerows(trace)


def stance_spans(trace):
    """Observe displayed control spans; this does not inspect painted sole alpha."""
    streams = {}
    for row in trace:
        streams.setdefault((row['scene'], row['foot']), []).append(row)
    reports = []
    for (scene, foot), rows in streams.items():
        active = []
        def finish():
            if active:
                reports.append({'scene': scene, 'foot': foot, 'samples': len(active),
                                'from_seconds': active[0]['time_seconds'],
                                'to_seconds': active[-1]['time_seconds'],
                                'displayed_control_world_span_logical_px':
                                    max(r['sole_world_logical_x'] for r in active)-
                                    min(r['sole_world_logical_x'] for r in active),
                                'max_control_floor_residual_logical_px':
                                    max(abs(r['sole_logical_height']) for r in active)})
        for row in rows:
            if row['planted']:
                active.append(row)
            else:
                finish(); active = []
        finish()
    return reports


def make_short(args, baseline, output, trace):
    if args.short_from_full:
        return make_short_from_full(args, baseline, output, trace)
    evaluation_path = args.short_evaluation or args.rig_root/'short-evaluation.json'
    evaluation = json.loads(evaluation_path.read_text())
    source_dir = args.short_dir or args.rig_root/'short'
    phase_samples = [float(item['phase']) for item in evaluation['frames']]
    if len(phase_samples) < 3 or any(b <= a for a, b in zip(phase_samples, phase_samples[1:])):
        raise ValueError('Short rig sample phases must be explicitly strictly increasing')
    if abs(phase_samples[-1]-phase_samples[0]-1) > 1e-6:
        raise ValueError('Short source must include one complete phase span and its loop endpoint')
    count = len(phase_samples)
    entry = {'source': '', 'reference_height': args.reference_height,
             'stride_pixels': evaluation['stride_logical_px'],
             'frames': [{'rect': [0, 0, 313, 313], 'anchor': [args.anchor_x, args.anchor_y]}
                        for _ in range(count)],
             'sequence': {'loop': [{'frame': i, 'ms': 1} for i in range(count-1)]}}
    poses = Poses(entry, args.rig_root, source_dir)
    annotations = annotations_by_frame(evaluation)
    if any(i not in annotations for i in range(count)):
        raise ValueError('Every short source pose must have directly exported rig annotations')
    stride = poses.stride; period = stride/args.speed; fps = args.fps
    duration = 1.25*period
    frames = math.ceil(duration*fps)+1
    def image(i):
        seconds = i/fps; travel = seconds*args.speed
        phase = phase_samples[0] + travel/stride
        lookup = phase_samples[0] + (phase-phase_samples[0]) % 1
        index = max(0, min(count-2, bisect.bisect_right(phase_samples, lookup)-1))
        canvas = Image.new('RGBA', (900, 480), BG); draw = ImageDraw.Draw(canvas)
        draw.text((12, 10), 'Editable mesh contact study / Pillow composite / NOT Windows or desktop recording', fill=INK, font=font(13))
        draw.text((12, 34), f'110 logical px | 22 logical px/s | {fps} output FPS | t={seconds:.3f}s', fill=INK, font=font(13))
        review_status(draw, 900)
        draw.text((75, 100), 'Original appearance reference (unchanged pose)', fill=INK, font=font(13))
        draw.text((480, 100), 'Same mesh source: stance -> lift -> recovery -> touchdown', fill=INK, font=font(12))
        ground(draw, 900, 325, 570)
        paste(canvas, baseline, baseline.entry.get('standing_frame', 0), 185, 325)
        paste(canvas, poses, index, 570+travel, 325)
        draw.line((570+travel, 325, 570+travel, 333), fill=ROOT_COLOR, width=2)
        marker(draw, poses, index, annotations[index], travel, 570, 325, 1,
               ('near_front',), trace, seconds, 'short-footlock')
        draw.text((12, 430), 'NF = near front sole. Green = stance, orange = swing. Ground tick marks remain fixed.', fill=INK, font=font(12))
        draw.text((12, 451), 'Numerical curve checks and visual quality are separate; this renderer does not approve either.', fill=INK, font=font(12))
        return canvas
    movie = encode(output/'short-footlock.mp4', frames, fps, (900, 480), image)
    return movie, {'path': str(evaluation_path), 'sha256': sha(evaluation_path),
                   'phase_start': phase_samples[0], 'phase_end': phase_samples[-1],
                   'source_pose_samples': count, 'stride_logical_px': stride,
                   'actual_root_distance_logical_px': (frames-1)/fps*args.speed,
                   'source_pngs': poses.inputs}


def load_full(args):
    manifest = args.manifest or args.rig_root/'manifest.json'
    annotations_path = args.annotations or args.rig_root/'annotations.json'
    entry = read_entry(manifest)
    document = json.loads(annotations_path.read_text())
    annotations = annotations_by_frame(document)
    frames_dir = args.frames_dir
    source_path = manifest.parent / entry['source']
    if frames_dir is None and not source_path.is_file():
        frames_dir = args.rig_root/'frames'
    poses = Poses(entry, manifest.parent, frames_dir)
    if any(step['frame'] not in annotations for step in poses.loop):
        raise ValueError('Every loop frame needs actual rig foot annotations')
    return poses, annotations, manifest, annotations_path


def make_short_from_full(args, baseline, output, trace):
    """Display the real final loop; no artificial45-pose phase list is made."""
    poses, annotations, manifest, annotations_path = load_full(args)
    fps = args.fps; period = poses.stride/args.speed
    count = math.ceil(1.25*period*fps)+1
    def image(i):
        seconds = i/fps; travel = seconds*args.speed
        phase = 0.25+travel/poses.stride
        index = poses.sample(phase)
        canvas = Image.new('RGBA', (900, 480), BG); draw = ImageDraw.Draw(canvas)
        draw.text((12, 10), 'Final mesh contact study / Pillow composite / NOT Windows or desktop recording', fill=INK, font=font(13))
        draw.text((12, 34), f'110 logical px | 22 logical px/s | {fps} output FPS | t={seconds:.3f}s', fill=INK, font=font(13))
        review_status(draw, 900)
        draw.text((75, 100), 'Original appearance reference (unchanged pose)', fill=INK, font=font(13))
        draw.text((480, 100), f'Final {len(poses.loop)}-pose loop sampled for this view; no new art', fill=INK, font=font(12))
        ground(draw, 900, 325, 570)
        paste(canvas, baseline, baseline.entry.get('standing_frame', 0), 185, 325)
        paste(canvas, poses, index, 570+travel, 325)
        draw.line((570+travel, 325, 570+travel, 333), fill=ROOT_COLOR, width=2)
        marker(draw, poses, index, annotations[index], travel, 570, 325, 1,
               ('near_front',), trace, seconds, 'short-footlock')
        draw.text((12, 430), 'NF = near front sole. Green = stance, orange = swing. Ground tick marks remain fixed.', fill=INK, font=font(12))
        draw.text((12, 451), 'Full-loop source poses and actual registered phase weights; no interpolated or added artwork.', fill=INK, font=font(12))
        return canvas
    movie = encode(output/'short-footlock.mp4', count, fps, (900, 480), image)
    return movie, {'source_kind': 'final_full_loop_resampled_for_display_no_new_poses',
                   'manifest': str(manifest), 'manifest_sha256': sha(manifest),
                   'annotations': str(annotations_path), 'annotations_sha256': sha(annotations_path),
                   'phase_start': 0.25, 'phase_end': 0.25+(count-1)/fps/period,
                   'loop_pose_count': len(poses.loop),
                   'stride_logical_px': poses.stride,
                   'actual_root_distance_logical_px': (count-1)/fps*args.speed,
                   'source_pngs': poses.inputs}


def make_full(args, baseline, output, trace):
    poses, annotations, manifest, annotations_path = load_full(args)
    fps = args.fps; speed = args.speed
    required = args.cycles * max(baseline.stride, poses.stride)
    count = math.ceil(required/speed*fps)+1
    def image(i):
        seconds = i/fps; travel = seconds*speed
        canvas = Image.new('RGBA', (900, 520), BG); draw = ImageDraw.Draw(canvas)
        draw.text((12, 10), 'Existing8 vs editable mesh bake / Pillow composite / NOT Windows or desktop recording', fill=INK, font=font(13))
        draw.text((12, 34), f'110 logical px | {speed:g} logical px/s | {fps} output FPS | t={seconds:.3f}s', fill=INK, font=font(13))
        review_status(draw, 900)
        for lane, current in enumerate((baseline, poses)):
            floor = 235 if lane == 0 else 475
            phase = travel/current.stride; index = current.sample(phase)
            ground(draw, 900, floor, 180)
            paste(canvas, current, index, 180+travel, floor)
            name = 'Existing 8 registered drawings' if lane == 0 else 'Editable mesh source / rejected visual candidate'
            draw.text((12, 68 if lane == 0 else 305), name, fill=INK, font=font(13))
            draw.text((12, 91 if lane == 0 else 328), f'stride={current.stride:g} | cycles={phase:.3f} | source frame={index}', fill=INK, font=font(12))
        return canvas
    normal = encode(output/'walk-comparison.mp4', count, fps, (900, 520), image)
    # A separate slow, enlarged view; no change is presented as new artwork.
    zoom, slowdown = 3, 3
    phase_start = 0.25
    duration = 1.25*poses.stride/speed*slowdown
    slow_count = math.ceil(duration*fps)+1
    def slow_image(i):
        output_seconds = i/fps; source_seconds = output_seconds/slowdown
        travel = source_seconds*speed; phase = phase_start+travel/poses.stride
        index = poses.sample(phase)
        canvas = Image.new('RGBA', (1200, 760), BG); draw = ImageDraw.Draw(canvas)
        draw.text((12, 10), 'Rig sole review / 3x enlarged / 3x slower / Pillow composite, NOT Windows output', fill=INK, font=font(15))
        draw.text((12, 37), f'Source travel22logicalpx/s | playback speed1/3 | output{fps}FPS | source t={source_seconds:.3f}s', fill=INK, font=font(14))
        review_status(draw, 1200)
        floor, origin = 625, 440
        ground(draw, 1200, floor, origin, zoom)
        paste(canvas, poses, index, origin+travel*zoom, floor, zoom)
        marker(draw, poses, index, annotations[index], travel, origin, floor, zoom,
               FOOT_NAMES, trace, source_seconds, 'contact-slow')
        draw.line((origin+travel*zoom, floor, origin+travel*zoom, floor+9), fill=ROOT_COLOR, width=2)
        draw.text((12, 725), 'Green = stance; orange = swing. Markers come from evaluated rig controls, not painted-image bounds.', fill=INK, font=font(13))
        return canvas
    slow = encode(output/'contact-slow.mp4', slow_count, fps, (1200, 760), slow_image)
    last_distance = (count-1)/fps*speed
    if last_distance/baseline.stride < args.cycles or last_distance/poses.stride < args.cycles:
        raise ValueError('Review did not display the requested minimum cycles')
    return [normal, slow], {'manifest': str(manifest), 'manifest_sha256': sha(manifest),
                           'annotations': str(annotations_path), 'annotations_sha256': sha(annotations_path),
                           'stride_logical_px': poses.stride, 'loop_pose_count': len(poses.loop),
                           'baseline_cycles_displayed': last_distance/baseline.stride,
                           'mesh_cycles_displayed': last_distance/poses.stride,
                           'actual_root_distance_logical_px': last_distance,
                           'source_pngs': poses.inputs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('short', 'full', 'all'), default='short')
    parser.add_argument('--rig-root', type=Path, default=ROOT/'build/cream-mesh-rig')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--annotations', type=Path)
    parser.add_argument('--frames-dir', type=Path)
    parser.add_argument('--short-dir', type=Path)
    parser.add_argument('--short-evaluation', type=Path)
    parser.add_argument('--short-from-full', action='store_true',
                        help='Sample short view from the same full manifest/annotations; no new pose artwork')
    parser.add_argument('--baseline-manifest', type=Path, default=ROOT/'assets/care-sprites/manifest.json')
    parser.add_argument('--baseline-source', type=Path)
    parser.add_argument('--fps', type=int, choices=(30, 60), default=60)
    parser.add_argument('--speed', type=float, default=22)
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--reference-height', type=float, default=221)
    parser.add_argument('--anchor-x', type=float, default=161)
    parser.add_argument('--anchor-y', type=float, default=275)
    args = parser.parse_args()
    if not math.isfinite(args.speed) or args.speed != 22 or args.cycles < 3:
        parser.error('This acceptance review requires22logicalpx/s and at least3cycles')
    output = args.output or args.rig_root/'review'
    output.mkdir(parents=True, exist_ok=True)
    baseline_entry = read_entry(args.baseline_manifest)
    baseline_source = args.baseline_source or args.baseline_manifest.parent / baseline_entry['source']
    # The source override is explicit, never a guessed path or replacement PNG.
    baseline = Poses(baseline_entry | {'source': str(baseline_source.resolve())}, args.baseline_manifest.parent)
    movies, sources, trace = [], {}, []
    if args.mode in ('short', 'all'):
        movie, source = make_short(args, baseline, output, trace); movies.append(movie); sources['short'] = source
    if args.mode in ('full', 'all'):
        made, source = make_full(args, baseline, output, trace); movies.extend(made); sources['full'] = source
    write_trace(output, trace, args.mode)
    proof = {'render_type': 'Pillow/FFmpeg composition of unchanged baked poses; not GDI+ or desktop recording',
             'source_pngs_modified': False, 'image_crossfades_or_invented_inbetweens': False,
             'per_frame_bbox_scale': False, 'display_reference_height_logical_px': 110,
             'speed_logical_px_per_second': 22, 'normal_output_fps': args.fps,
             'application_active_target_hz': 'approximately60; separate from source pose count and review encoding',
             'numerical_quality_status': 'read separate rig/contact reports',
             'visual_quality_status': 'not_assessed_by_renderer', 'production_approved': False,
             'visible_video_status': 'UNAPPROVED / DIAGNOSTIC + HANDOFF',
             'invocation_mode': args.mode,
             'movies': movies, 'sources': sources,
             'observed_displayed_control_stances': stance_spans(trace),
             'baseline_pngs': baseline.inputs}
    # Keep each stage's evidence when short-first/full-later runs share a folder.
    (output/f'{args.mode}-review-proof.json').write_text(json.dumps(proof, indent=2)+'\n')
    (output/'review-proof.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps({'output': str(output), 'movies': movies, 'visual_quality_status': proof['visual_quality_status']}))


if __name__ == '__main__':
    main()
