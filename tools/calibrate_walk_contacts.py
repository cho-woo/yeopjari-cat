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

An optional motion_contract schema1 declares low-order Fourier head/body motion
in root-relative LOGICAL pixels, sample_phases, stride, speed and exact rig
start/end poses/velocities. This separates an authored periodic weight shift
from unexplained jitter. The model must be authored with the rig, not fitted to
per-frame noise. Four named foot tracks and all existing planted-contact checks
remain mandatory. A numerical pass never sets visual or production approval.

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
    'landmark_velocity_residual_logical_px_per_cycle': 8.0,
    'loop_position_tolerance_logical_px': 0.05,
    'loop_velocity_tolerance_logical_px_per_cycle': 0.25,
    'phase_distance_tolerance_logical_px': 0.02,
    'stride_tolerance_logical_px': 0.05,
    'max_paw_step_logical_px': 3.0,
}
WEIGHT_TOTAL = 60000


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def point(value):
    return isinstance(value, list) and len(value) == 2 and all(number(v) for v in value)


def reject(errors, report=None):
    return {'accepted': False, 'numeric_passed': False, 'visual_quality_status': 'not_assessed',
            'production_approved': False, 'errors': errors, 'report': report or {}}


def periodic_value(model, phase, derivative=False):
    """An explicitly authored, at most fourth-order periodic landmark curve."""
    result = [0.0, 0.0] if derivative else list(model['base'])
    for harmonic in model['harmonics']:
        omega = 2 * math.pi * harmonic['order']
        cosine, sine = math.cos(omega * phase), math.sin(omega * phase)
        for axis in (0, 1):
            if derivative:
                result[axis] += omega * (-harmonic['cos'][axis] * sine + harmonic['sin'][axis] * cosine)
            else:
                result[axis] += harmonic['cos'][axis] * cosine + harmonic['sin'][axis] * sine
    return result


def parse_motion_contract(document, count):
    contract = document.get('motion_contract')
    if contract is None:
        return None, []
    errors = []
    if not isinstance(contract, dict):
        return None, ['motion_contract must be a schema-1 object']
    required = {'schema', 'coordinate_space', 'foot_source', 'sample_phases', 'stride_logical_px',
                'speed_logical_px_per_second', 'landmarks', 'loop_boundary'}
    if set(contract) - required - {'source_pose_fps'} or required - set(contract):
        return None, ['motion_contract has missing or unrecognized fields']
    if (type(contract['schema']) is not int or contract['schema'] != 1 or
            contract['coordinate_space'] != 'root_relative_logical_px' or contract['foot_source'] != 'rig_controls'):
        errors.append('motion_contract needs schema1, root_relative_logical_px and rig_controls foot provenance')
    phases = contract['sample_phases']
    if (not isinstance(phases, list) or len(phases) != count or
            any(not number(p) or not 0 <= p < 1 for p in phases) or
            (phases and abs(phases[0]) > 1e-9) or
            any(b <= a for a, b in zip(phases, phases[1:]))):
        errors.append('motion_contract.sample_phases must start at0 and increase through one cycle without duplicating phase1')
    for key, maximum in [('stride_logical_px', 4096), ('speed_logical_px_per_second', 4096)]:
        if not number(contract[key]) or not 0 < contract[key] <= maximum:
            errors.append(f'motion_contract.{key} must be positive and bounded')
    if 'source_pose_fps' in contract and (not number(contract['source_pose_fps']) or not 0 < contract['source_pose_fps'] <= 240):
        errors.append('motion_contract.source_pose_fps must be in (0,240] and is distinct from render FPS')
    models = contract['landmarks']
    if not isinstance(models, dict) or set(models) != {'head', 'body'}:
        errors.append('motion_contract needs authored head and body landmark curves')
    else:
        for name, model in models.items():
            if (not isinstance(model, dict) or set(model) != {'base', 'harmonics'} or not point(model['base']) or
                    not isinstance(model['harmonics'], list) or len(model['harmonics']) > 4):
                errors.append(f'{name}: use a base point and at most four authored harmonics'); continue
            orders = []
            for harmonic in model['harmonics']:
                if (not isinstance(harmonic, dict) or set(harmonic) != {'order', 'cos', 'sin'} or
                        type(harmonic.get('order')) is not int or not 1 <= harmonic['order'] <= 4 or
                        not point(harmonic.get('cos')) or not point(harmonic.get('sin'))):
                    errors.append(f'{name}: invalid authored harmonic'); continue
                orders.append(harmonic['order'])
            if len(set(orders)) != len(orders):
                errors.append(f'{name}: duplicate harmonic order')
    boundary = contract['loop_boundary']
    if not isinstance(boundary, dict) or set(boundary) != {'start', 'end'}:
        errors.append('motion_contract.loop_boundary needs exact start and end rig samples')
    else:
        for label, sample in boundary.items():
            if not isinstance(sample, dict) or set(sample) != {'landmarks', 'feet'}:
                errors.append(f'Loop {label}: landmarks and feet are required'); continue
            for group, names in [('landmarks', ('head', 'body')), ('feet', FEET)]:
                values = sample[group]
                if not isinstance(values, dict) or set(values) != set(names):
                    errors.append(f'Loop {label}: all named {group} are required'); continue
                for name, value in values.items():
                    keys = {'position', 'velocity'} | ({'planted'} if group == 'feet' else set())
                    if (not isinstance(value, dict) or set(value) != keys or not point(value.get('position')) or
                            not point(value.get('velocity')) or
                            (group == 'feet' and type(value.get('planted')) is not bool)):
                        errors.append(f'Loop {label} {name}: explicit position and per-cycle-phase velocity required')
    return (None, errors) if errors else (contract, [])


def inspect_motion_contract(contract, indexed, frames, scale, distances, limits):
    """Check independent rig boundary samples and contact-derived sampling."""
    errors, report = [], {}
    phases = contract['sample_phases']
    stride = contract['stride_logical_px']
    position_limit = limits['loop_position_tolerance_logical_px']
    velocity_limit = limits['loop_velocity_tolerance_logical_px_per_cycle']
    start, end = (contract['loop_boundary'][key] for key in ('start', 'end'))
    closures = {}
    for group, names in [('landmarks', ('head', 'body')), ('feet', FEET)]:
        for name in names:
            first, last = start[group][name], end[group][name]
            position_error = math.dist(first['position'], last['position'])
            velocity_error = math.dist(first['velocity'], last['velocity'])
            closures[name] = {'position_error_logical_px': position_error,
                              'velocity_error_logical_px_per_cycle': velocity_error}
            if position_error > position_limit:
                errors.append(f'Loop {name}: position seam {position_error:.6f} logical px')
            if velocity_error > velocity_limit:
                errors.append(f'Loop {name}: velocity seam {velocity_error:.6f} logical px per cycle')
            if group == 'landmarks':
                actual = indexed[0]['landmarks'][name]
                for endpoint, phase in [(first, 0), (last, 1)]:
                    model = contract['landmarks'][name]
                    if math.dist(endpoint['position'], periodic_value(model, phase)) > limits['landmark_jitter_logical_px']:
                        errors.append(f'Loop {name}: boundary position differs from authored periodic curve')
                    if math.dist(endpoint['velocity'], periodic_value(model, phase, True)) > velocity_limit:
                        errors.append(f'Loop {name}: boundary velocity differs from authored periodic curve')
            else:
                actual = [indexed[0]['feet'][name][axis] for axis in ('x', 'y')]
                if first['planted'] != last['planted'] or first['planted'] != indexed[0]['feet'][name]['planted']:
                    errors.append(f'Loop {name}: contact state changes across the seam')
                for endpoint in (first, last):
                    if endpoint['planted']:
                        if abs(endpoint['position'][1]) > limits['floor_tolerance_logical_px']:
                            errors.append(f'Loop {name}: planted endpoint misses common floor')
                        if math.dist(endpoint['velocity'], [-stride, 0]) > velocity_limit:
                            errors.append(f'Loop {name}: planted endpoint has nonzero world velocity')
            local_root = [(actual[axis]-frames[0]['anchor'][axis])*scale for axis in (0, 1)]
            if math.dist(local_root, first['position']) > position_limit:
                errors.append(f'Loop {name}: start rig sample disagrees with first baked frame annotation')
    phase_intervals = [(phases[(i+1) % len(phases)]-phases[i]) % 1 for i in range(len(phases))]
    phase_errors = [None if distance is None else abs(distance-stride*phase_intervals[i])
                    for i, distance in enumerate(distances)]
    for i, error in enumerate(phase_errors):
        if error is not None and error > limits['phase_distance_tolerance_logical_px']:
            errors.append(f'Interval {i}->{(i+1)%len(phases)}: contact travel disagrees with rig stride/phase by {error:.6f} logical px')
    measured = sum(distances) if all(d is not None for d in distances) else None
    if measured is not None and abs(measured-stride) > limits['stride_tolerance_logical_px']:
        errors.append(f'Contact-derived stride {measured:.6f} differs from designed rig stride {stride:.6f}')
    minimum_count = math.ceil(stride/limits['max_interval_root_logical_px']-1e-9)
    if len(frames) < minimum_count:
        errors.append(f'Frame count {len(frames)} cannot cover stride {stride:g} within maximum root gap; need at least {minimum_count} intervals')
    minimum_pose_samples = None
    if 'source_pose_fps' in contract:
        minimum_pose_samples = math.ceil(stride/contract['speed_logical_px_per_second']*contract['source_pose_fps']-1e-9)
        if len(frames) < minimum_pose_samples:
            errors.append(f'Frame count {len(frames)} is below authored pose sampling target {minimum_pose_samples}')
    paw_steps = {}
    for name in FEET:
        coordinates = [[(indexed[i]['feet'][name][axis]-frames[i]['anchor'][j])*scale
                        for j, axis in enumerate(('x', 'y'))] for i in range(len(frames))]
        maximum = max(math.dist(point, coordinates[(i+1) % len(coordinates)]) for i, point in enumerate(coordinates))
        paw_steps[name] = maximum
        if maximum > limits['max_paw_step_logical_px']:
            errors.append(f'{name}: paw step {maximum:.6f} logical px needs more actual pose samples')
    report.update({'foot_source': 'rig_controls (producer declaration; not a pixel-based foot-ID inference)',
                   'declared_stride_logical_px': stride, 'contact_derived_stride_logical_px': measured,
                   'minimum_intervals_for_root_gap': minimum_count,
                   'minimum_samples_for_source_pose_fps': minimum_pose_samples,
                   'sample_phases': phases, 'phase_distance_residuals_logical_px': phase_errors,
                   'loop_closure': closures, 'max_paw_steps_logical_px': paw_steps})
    return errors, report


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
    contract, contract_errors = parse_motion_contract(document, count)
    if contract_errors:
        return reject(contract_errors)
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
        if contract is None:
            center = [statistics.median(p[axis] for p in coordinates) for axis in (0, 1)]
            residuals = [math.dist(p, center) for p in coordinates]
            landmark_report[name] = {'mode': 'static', 'root_relative_median_logical_px': center,
                                     'max_deviation_logical_px': max(residuals)}
        else:
            expected = [periodic_value(contract['landmarks'][name], phase) for phase in contract['sample_phases']]
            offsets = [[actual[axis]-designed[axis] for axis in (0, 1)]
                       for actual, designed in zip(coordinates, expected)]
            residuals = [math.hypot(*offset) for offset in offsets]
            velocities = []
            for index, offset in enumerate(offsets):
                following = (index+1) % count
                phase_delta = (contract['sample_phases'][following]-contract['sample_phases'][index]) % 1
                velocities.append(math.dist(offset, offsets[following])/phase_delta)
            landmark_report[name] = {'mode': 'authored_periodic', 'model': contract['landmarks'][name],
                                     'max_deviation_logical_px': max(residuals),
                                     'max_residual_velocity_logical_px_per_cycle': max(velocities),
                                     'expected_root_relative_logical_px': expected}
            if max(velocities) > limits['landmark_velocity_residual_logical_px_per_cycle']:
                errors.append(f'{name} unplanned landmark velocity residual {max(velocities):.6f} logical px per cycle')
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
    if contract is not None:
        motion_errors, motion_report = inspect_motion_contract(contract, indexed, frames, scale, distances, limits)
        errors.extend(motion_errors)
        report['motion_contract'] = motion_report
    report['visual_quality_status'] = 'not_assessed'
    report['production_approved'] = False
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
    return {'accepted': True, 'numeric_passed': True, 'visual_quality_status': 'not_assessed',
            'production_approved': False, 'errors': [], 'report': report, 'entry': exported}


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
