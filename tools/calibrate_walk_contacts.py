#!/usr/bin/env python3
"""Calibrate a future WALK sheet from explicitly annotated, named paw contacts.

Input JSON: {schema:1, entry:<importer walk entry with explicit frames>,
annotations:[{frame:0, feet:{near_front:{x,y,planted}, far_front:{x,y,planted},
near_rear:{x,y,planted}, far_rear:{x,y,planted}}, landmarks:{head:[x,y],body:[x,y]}}],
limits:{...optional tolerances...}}. Coordinates are LOCAL source pixels in each
explicit frame rectangle. All four paw identities and both landmarks must be
drawn/annotated for every frame. No foot identity is inferred from a bbox.

For every adjacent pair, INCLUDING last->first, at least one same named paw
must stay planted and move backward relative to the common root. Its movement
determines the required forward root travel. Simultaneously planted paws must
agree. Gaps, stationary/forward planted paws, bad floor contacts or head/body
jitter reject the sheet. There is no fallback timing or guessed annotation.

--output writes an importer-compatible schema-2 manifest fragment ONLY after
all checks pass. Loop 'ms' values are high-resolution DISTANCE WEIGHTS for the
existing distance-driven Walk sampler; they are not a requested FPS or action
duration. The tool reads source alpha when a source PNG exists, never edits it.
Missing PNGs permit geometry-only calibration and are explicitly reported;
the normal importer still requires/validates the actual original PNG.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

FEET = ('near_front', 'far_front', 'near_rear', 'far_rear')
DEFAULT_LIMITS = {
    'floor_tolerance_logical_px': 0.5,
    'contact_agreement_logical_px': 0.25,
    'landmark_jitter_logical_px': 0.75,
    'max_interval_root_logical_px': 1.0,
    'min_interval_root_logical_px': 0.0001,
    'alpha_support_radius_source_px': 2,
    'alpha_support_threshold': 200,
}
WEIGHT_TOTAL = 60000


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def point(value):
    return isinstance(value, list) and len(value) == 2 and all(number(v) for v in value)


def reject(errors, report=None):
    return {'accepted': False, 'errors': errors, 'report': report or {}}


def distance_weights(distances):
    """Largest remainder quantization with positive steps and a fixed sum."""
    total = sum(distances)
    quotas = [distance / total * WEIGHT_TOTAL for distance in distances]
    weights = [max(1, math.floor(quota)) for quota in quotas]
    difference = WEIGHT_TOTAL - sum(weights)
    if difference > 0:
        order = sorted(range(len(weights)), key=lambda i: quotas[i]-weights[i], reverse=True)
        for index in order[:difference]:
            weights[index] += 1
    elif difference < 0:
        order = sorted(range(len(weights)), key=lambda i: weights[i]-quotas[i], reverse=True)
        for index in order:
            take = min(weights[index]-1, -difference)
            weights[index] -= take; difference += take
            if difference == 0:
                break
    if sum(weights) != WEIGHT_TOTAL or min(weights) <= 0:
        raise ValueError('Contact intervals cannot be represented as positive distance weights')
    return weights


def calibrate(document, base_dir=Path('.')):
    errors = []
    if not isinstance(document, dict) or type(document.get('schema')) is not int or document['schema'] != 1:
        return reject(['Expected an annotated-contact schema-1 JSON document'])
    entry = document.get('entry')
    annotations = document.get('annotations')
    if not isinstance(entry, dict) or entry.get('clip') != 'walk':
        return reject(['entry must be an explicit importer walk registration'])
    frames = entry.get('frames')
    if not isinstance(frames, list) or not 2 <= len(frames) <= 256:
        return reject(['entry.frames must contain 2..256 explicit rect/anchor registrations'])
    count = len(frames)
    if entry.get('frame_count', count) != count:
        return reject(['entry.frame_count differs from explicit frames'])
    if not number(entry.get('reference_height')) or not 0 < entry['reference_height'] <= 16384:
        return reject(['One positive common entry.reference_height is required'])
    if type(entry.get('character', 0)) is not int or not 0 <= entry.get('character', 0) < 10:
        return reject(['entry.character must be a serialized integer ID in 0..9'])
    if type(entry.get('standing_frame', 0)) is not int or not 0 <= entry.get('standing_frame', 0) < count:
        return reject(['entry.standing_frame must address an explicit registered frame'])
    for key in ('columns', 'rows'):
        if type(entry.get(key)) is not int or entry[key] <= 0:
            errors.append(f'entry.{key} must be a positive integer')
    if errors:
        return reject(errors)
    if entry['columns'] * entry['rows'] < count:
        return reject(['Grid has fewer cells than explicit frames'])
    if not isinstance(entry.get('source'), str) or not entry['source']:
        return reject(['entry.source must identify the original, unchanged PNG'])
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            errors.append(f'Frame {index}: rect and local anchor required'); continue
        rect, anchor = frame.get('rect'), frame.get('anchor')
        if (not isinstance(rect, list) or len(rect) != 4 or any(type(v) is not int for v in rect)
                or min(rect[:2]) < 0 or min(rect[2:]) <= 0):
            errors.append(f'Frame {index}: invalid explicit rect'); continue
        if not point(anchor) or not 0 <= anchor[0] <= rect[2] or not 0 <= anchor[1] <= rect[3]:
            errors.append(f'Frame {index}: invalid local anchor')
    if errors:
        return reject(errors)
    limits = DEFAULT_LIMITS | document.get('limits', {}) if isinstance(document.get('limits', {}), dict) else None
    if limits is None or set(limits) != set(DEFAULT_LIMITS):
        return reject(['limits contains unrecognized fields or is not an object'])
    if any(not number(value) or value < 0 for value in limits.values()):
        return reject(['All contact tolerances must be finite nonnegative numbers'])
    if (limits['max_interval_root_logical_px'] <= 0 or limits['min_interval_root_logical_px'] <= 0
            or limits['min_interval_root_logical_px'] >= limits['max_interval_root_logical_px']):
        return reject(['Root interval limits must satisfy 0 < minimum < maximum'])
    if (type(limits['alpha_support_radius_source_px']) is not int or
            not 0 <= limits['alpha_support_radius_source_px'] <= 8 or
            type(limits['alpha_support_threshold']) is not int or
            not 0 <= limits['alpha_support_threshold'] <= 255):
        return reject(['Alpha radius/threshold must be bounded integers'])
    if not isinstance(annotations, list) or len(annotations) != count:
        return reject(['Every frame needs explicit four-foot annotations; generated sheets without tracks are rejected'])
    indexed = {}
    for annotation in annotations:
        if (not isinstance(annotation, dict) or type(annotation.get('frame')) is not int or
                not 0 <= annotation['frame'] < count or annotation['frame'] in indexed):
            errors.append('Annotations need each unique frame index exactly once'); continue
        index = annotation['frame']; indexed[index] = annotation
        feet, landmarks = annotation.get('feet'), annotation.get('landmarks')
        if not isinstance(feet, dict) or set(feet) != set(FEET):
            errors.append(f'Frame {index}: all four named foot tracks are required'); continue
        width, height = frames[index]['rect'][2:]
        for name, foot in feet.items():
            if (not isinstance(foot, dict) or set(foot) != {'x', 'y', 'planted'} or
                    not number(foot.get('x')) or not number(foot.get('y')) or
                    type(foot.get('planted')) is not bool or
                    not 0 <= foot['x'] <= width or not 0 <= foot['y'] <= height):
                errors.append(f'Frame {index} {name}: explicit bounded x/y and boolean planted required')
        if not isinstance(landmarks, dict) or set(landmarks) != {'head', 'body'}:
            errors.append(f'Frame {index}: explicit head and body landmarks required')
        else:
            for name, value in landmarks.items():
                if not point(value) or not 0 <= value[0] <= width or not 0 <= value[1] <= height:
                    errors.append(f'Frame {index}: invalid {name} landmark')
    if errors or len(indexed) != count:
        return reject(errors or ['Incomplete annotation frame indices'])
    scale = 110.0 / entry['reference_height']
    source = Path(entry['source'])
    if not source.is_absolute():
        source = Path(base_dir) / source
    image = None
    image_status = 'missing: geometry-only calibration; PNG importer verification still required'
    source_sha = None
    if source.is_file():
        try:
            from PIL import Image
            with Image.open(source) as original:
                if original.format != 'PNG' or original.mode != 'RGBA':
                    return reject(['Source must be a PNG with a real RGBA alpha channel'])
                image = original.copy()
            source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
            image_status = 'RGBA source read; explicit planted contacts checked against material alpha'
        except (ImportError, OSError) as error:
            return reject([f'Cannot verify existing source PNG alpha: {error}'])
    floor_residuals, landmark_report = [], {}
    for index in range(count):
        annotation = indexed[index]; frame = frames[index]; anchor = frame['anchor']
        if image is not None:
            x, y, w, h = frame['rect']
            if x+w > image.width or y+h > image.height:
                errors.append(f'Frame {index}: explicit rect exceeds source PNG'); continue
        for name, foot in annotation['feet'].items():
            if not foot['planted']:
                continue
            residual = abs(foot['y'] - anchor[1]) * scale
            floor_residuals.append(residual)
            if residual > limits['floor_tolerance_logical_px']:
                errors.append(f'Frame {index} {name}: planted foot misses common floor by {residual:.6f} logical px')
            if image is not None:
                radius = limits['alpha_support_radius_source_px']
                x, y, w, h = frame['rect']
                px, py = round(foot['x']), round(foot['y'])
                samples = [image.getpixel((x+cx, y+cy))[3]
                           for cy in range(max(0, py-radius), min(h-1, py+radius)+1)
                           for cx in range(max(0, px-radius), min(w-1, px+radius)+1)]
                if not samples or max(samples) <= limits['alpha_support_threshold']:
                    errors.append(f'Frame {index} {name}: annotated planted contact has no source alpha support')
    for name in ('head', 'body'):
        coordinates = [[(indexed[i]['landmarks'][name][axis]-frames[i]['anchor'][axis])*scale
                        for axis in (0, 1)] for i in range(count)]
        center = [statistics.median(p[axis] for p in coordinates) for axis in (0, 1)]
        residuals = [math.dist(p, center) for p in coordinates]
        landmark_report[name] = {'root_relative_median_logical_px': center,
                                 'max_deviation_logical_px': max(residuals)}
        if max(residuals) > limits['landmark_jitter_logical_px']:
            errors.append(f'{name} landmark jitter {max(residuals):.6f} logical px exceeds common registration tolerance')
    intervals, distances, uncovered = [], [], []
    for index in range(count):
        following = (index+1) % count
        estimates = {}
        for name in FEET:
            first, second = indexed[index]['feet'][name], indexed[following]['feet'][name]
            if first['planted'] and second['planted']:
                distance = ((first['x']-frames[index]['anchor'][0]) -
                            (second['x']-frames[following]['anchor'][0])) * scale
                estimates[name] = distance
                if distance <= limits['min_interval_root_logical_px']:
                    errors.append(f'Interval {index}->{following} {name}: zero/forward planted motion ({distance:.6f} logical px)')
        if not estimates:
            uncovered.append([index, following]); distances.append(None)
            errors.append(f'Interval {index}->{following}: no same named planted foot; no timing may be inferred')
            intervals.append({'from': index, 'to': following, 'estimates': {}, 'root_travel_logical_px': None})
            continue
        values = list(estimates.values())
        disagreement = max(values)-min(values)
        if disagreement > limits['contact_agreement_logical_px']:
            errors.append(f'Interval {index}->{following}: planted feet disagree by {disagreement:.6f} logical px')
        distance = statistics.fmean(values)
        distances.append(distance)
        intervals.append({'from': index, 'to': following, 'estimates': estimates,
                          'root_travel_logical_px': distance,
                          'max_contact_residual_logical_px': max(abs(value-distance) for value in values),
                          'contact_disagreement_logical_px': disagreement})
        if distance > limits['max_interval_root_logical_px']:
            errors.append(f'Interval {index}->{following}: root gap {distance:.6f} logical px requires more inbetweens')
    complete = all(value is not None and value > 0 for value in distances)
    report = {'source_image_status': image_status, 'source_sha256': source_sha,
              'acceptance_scope': 'Annotated contact/registration geometry only; not proof of artwork identity or correctness of human foot labels',
              'frame_count': count, 'logical_scale': scale, 'limits': limits,
              'max_floor_residual_logical_px': max(floor_residuals, default=0),
              'landmarks': landmark_report, 'intervals': intervals, 'uncovered_intervals': uncovered,
              'max_root_gap_logical_px': max(distances) if complete else None,
              'measured_stride_logical_px': sum(distances) if complete else None,
              'existing_sequence_or_stride_ignored': 'sequence' in entry or 'stride_pixels' in entry,
              'new_art_created': False, 'foot_identities_inferred': False}
    if errors:
        return reject(errors, report)
    weights = distance_weights(distances)
    stride = sum(distances)
    quantized = [stride*weight/WEIGHT_TOTAL for weight in weights]
    report.update({'distance_weight_total': WEIGHT_TOTAL,
                   'max_weight_quantization_residual_logical_px': max(abs(a-b) for a, b in zip(distances, quantized)),
                   'max_pose_hold_ms_at_22_logical_px_per_second': max(distances)/22*1000,
                   'cycle_seconds_at_22_logical_px_per_second': stride/22})
    exported = {key: copy.deepcopy(entry[key]) for key in ('clip', 'source', 'columns', 'rows', 'reference_height', 'frames')}
    exported.update({'character': entry.get('character', 0), 'frame_count': count,
                     'stride_pixels': stride, 'standing_frame': entry.get('standing_frame', 0),
                     'sequence': {'intro': [], 'loop': [{'frame': i, 'ms': weight} for i, weight in enumerate(weights)],
                                  'outro': [], 'finish': 'complete', 'duration_ms': 0, 'events': []}})
    if source_sha is not None:
        exported['sha256'] = source_sha
    return {'accepted': True, 'errors': [], 'report': report, 'entry': exported}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('annotations', type=Path)
    parser.add_argument('--output', type=Path, help='Importer schema-2 manifest fragment; written only if valid')
    parser.add_argument('--report', type=Path, help='Write full residuals/rejection reasons')
    parser.add_argument('--max-root-step', type=float, help='Override the maximum logical px of root travel between unique drawings')
    args = parser.parse_args()
    try:
        document = json.loads(args.annotations.read_text())
        if args.max_root_step is not None:
            document.setdefault('limits', {})['max_interval_root_logical_px'] = args.max_root_step
        result = calibrate(document, args.annotations.parent)
    except (ValueError, OSError, TypeError) as error:
        result = reject([str(error)])
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    if result['accepted'] and args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({'schema': 2, 'clips': [result['entry']]}, indent=2) + '\n')
    print(json.dumps({'accepted': result['accepted'], 'errors': result['errors'], 'report': result['report']}, indent=2))
    return 0 if result['accepted'] else 1


if __name__ == '__main__':
    sys.exit(main())
