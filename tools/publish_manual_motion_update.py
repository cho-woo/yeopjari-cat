#!/usr/bin/env python3
"""Publish the exact Windows-validated v2.0.22 source-video gait update as stable.

CI-only. Uses a draft/tag prepared at the validated source commit. It never
rebuilds, replaces a public release, or promotes the opt-in mesh walking study.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from urllib.request import Request, urlopen
import zipfile

import release
from verify_silent_exe import verify_silent_exe

REPO = 'cho-woo/yeopjari-cat'
VERSION = '2.0.22'
TAG = 'v' + VERSION
PREVIOUS_STABLE = 'v2.0.21'
ROOT = Path(__file__).resolve().parents[1]
FEED_URL = f'https://github.com/{REPO}/releases/latest/download/latest.txt'
BASE_URL = f'https://github.com/{REPO}/releases/download/{TAG}/'
SAVE_STEP = 'Execute Windows save, cursor and moving-window regression tests'
RENDER_STEP = 'Render real Windows GDI+ gait, contact and posture regressions'
CAPTURE_STEP = 'Capture source-video walking through Windows GDI+ and Box2D at 60 FPS'
MOTION_CAPTURE_STEP = 'Capture source-video meal, walk and run through Windows GDI+ and Box2D at 60 FPS'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def api(path):
    return json.loads(gh('api', f'repos/{REPO}/' + path))


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(4 * 1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def exactly_one(paths, description):
    matches = list(paths)
    require(len(matches) == 1, f'Expected exactly one {description}; found {len(matches)}')
    return matches[0]


def validated_source(run_id, head):
    require(run_id.isdigit(), 'VALIDATION_RUN must identify the successful validation')
    run = api('actions/runs/' + run_id)
    require(run.get('path') == '.github/workflows/validate.yml' and
            run.get('status') == 'completed' and run.get('conclusion') == 'success',
            'The selected Validate workflow must have completed successfully')
    require(run.get('head_repository', {}).get('full_name') == REPO and
            run.get('head_branch') == 'main' and run.get('head_sha') == head,
            'Checkout must be exactly the validated main repository commit')
    # A single bounded page contains both jobs and their individual native steps.
    jobs = api('actions/runs/' + run_id + '/jobs?per_page=100')['jobs']
    require(len(jobs) < 100, 'Validation jobs exceed bounded inspection')
    for name in ('validate', 'windows-smoke'):
        job = exactly_one((j for j in jobs if j['name'] == name), name + ' job')
        require(job.get('status') == 'completed' and job.get('conclusion') == 'success',
                f'Missing successful {name} job')
        if name == 'windows-smoke':
            for step_name in (SAVE_STEP, RENDER_STEP, CAPTURE_STEP, MOTION_CAPTURE_STEP):
                step = exactly_one((s for s in job['steps'] if s['name'] == step_name), step_name)
                require(step.get('status') == 'completed' and step.get('conclusion') == 'success',
                        f'Native Windows step did not pass: {step_name}')
    return run


def repository_file(base, relative, description):
    require(isinstance(relative, str) and relative and not Path(relative).is_absolute() and
            '..' not in Path(relative).parts, f'Unsafe {description} path')
    path = base / relative
    require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT.resolve()),
            f'Missing or external {description}')
    return path


def normal_walk_contract():
    from import_care_sprites import load_entries, prepare_import, runtime_locomotion

    original_path = ROOT / 'assets/care-sprites/manifest.json'
    manifest_path = ROOT / 'assets/natural-gait/manifest.json'
    require(manifest_path.is_file(), 'The new release requires its active source-video action manifest')
    active, replaced = runtime_locomotion(ROOT, load_entries(original_path))
    require(replaced == 3, 'Walking, running and eating must use their actual source-video frames')
    imported = prepare_import(ROOT, check=True)
    require(imported.get('natural_locomotion_clips') == 3,
            'The compiled registration does not select all three source-video actions')
    provenance_path = manifest_path.parent / 'provenance.json'
    require(provenance_path.is_file(), 'The new actions need their original-video provenance')
    provenance = json.loads(provenance_path.read_text(encoding='utf-8'))
    require(provenance.get('schema') == 1 and provenance.get('method') == 'authored_video_extraction' and
            provenance.get('precomputed_only') is True and
            provenance.get('manifest_sha256') == sha(manifest_path),
            'The release must identify the exact original-video extraction')

    actions = {}
    for name, resource in (('walk', 400), ('run', 401), ('eat', 118)):
        entry = exactly_one((e for e in active if e['clip'] == name and e['character'] == 0),
                            'registered Cream ' + name)
        path = repository_file(ROOT, entry.get('asset_path'), 'registered action atlas')
        require(path.resolve().is_relative_to(manifest_path.parent.resolve()) and sha(path) == entry['sha256'],
                'The active action atlas differs from its source-video registration: ' + name)
        require(entry['resource'] == resource, 'Source-video action uses the wrong resource: ' + name)
        program = entry['program']
        require(program is not None, 'Source-video actions require explicit exposure programs')
        first, count = program['ranges'][1]
        cycle_indices = [step['frame'] for step in program['steps'][first:first + count]]
        cycle_frames = len(set(cycle_indices))
        total = len(entry['frames'])
        require(1 <= total <= 256 and entry['reference_height'] > 0, 'Invalid registered source-video images')
        if name in ('walk', 'run'):
            require(2 <= cycle_frames <= total and entry['stride_pixels'] > 0,
                    'Locomotion requires a real cycle and measured positive stride: ' + name)
        else:
            require(entry['stride_pixels'] == 0, 'Eating must not introduce physical locomotion')
        actions[name] = {
            'playback_frames': total, 'registered_frames': total, 'cycle_frames': cycle_frames,
            'cycle_step_count': count, 'cycle_registered_frame_indices': cycle_indices,
            'program_registered_frame_indices': sorted({step['frame'] for step in program['steps']}),
            'program_duration_ms': program.get('duration_ms', 0) or sum(step['ms'] for step in program['steps']),
            'standing_frame': entry['standing_frame'], 'stride_pixels': entry['stride_pixels'],
            'nominal_fps': entry.get('nominal_fps', 0),
            'movement_delay_ms': entry.get('movement_delay_ms', 0),
            'reference_height': entry['reference_height'], 'resource_id': resource,
            'image_width': entry['width'], 'image_height': entry['height'],
            'source': path.relative_to(ROOT).as_posix(), 'sha256': entry['sha256'],
            'animation_method': provenance['method'], 'new_extracted_images': total,
            'program_sections': {section: {'first_step': first_step, 'step_count': step_count,
                                           'duration_ms': sum(step['ms'] for step in
                                               program['steps'][first_step:first_step + step_count])}
                                 for section, (first_step, step_count) in
                                 zip(('intro', 'loop', 'outro'), program['ranges'])},
        }
    videos = source_videos_contract(provenance, actions)
    appearances = video_appearance_contract(manifest_path.parent, provenance, actions)
    source = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
    require(re.search(r'\bbool\s+meshWalkReview\s*=\s*false\s*;', source),
            'The archived mesh review must remain disabled by default')
    return {
        'normal_walk_frames': actions['walk']['registered_frames'],
        'normal_walk_cycle_frames': actions['walk']['cycle_frames'],
        'normal_walk_standing_frame': actions['walk']['standing_frame'],
        'normal_walk_sha256': actions['walk']['sha256'], 'mesh_default': False,
        'locomotion_manifest': manifest_path.relative_to(ROOT).as_posix(),
        'locomotion_manifest_sha256': sha(manifest_path),
        'video_action_artwork': actions,
        'locomotion_artwork': {name: actions[name] for name in ('walk', 'run')},
        'care_artwork': {'eat': actions['eat']},
        'natural_gait_provenance': {'path': provenance_path.relative_to(ROOT).as_posix(),
                                    'sha256': sha(provenance_path), 'report': provenance},
        'video_tuxedo_appearances': appearances,
        'natural_tuxedo_appearance': appearances['walk'],
        'source_videos': videos,
        # The native 60FPS walk-capture contract keeps its single-video alias.
        'source_video': {**videos['walk'], 'clips': {'walk': videos['walk']['selection']}},
    }


def source_videos_contract(provenance, actions):
    videos = provenance.get('videos')
    require(isinstance(videos, dict), 'The extraction must record each original action video')
    result = {}
    for name, action in actions.items():
        video = videos.get(name)
        require(isinstance(video, dict), 'Missing original video metadata: ' + name)
        source = exactly_one((s for s in provenance.get('sources', [])
                              if s.get('role') == video.get('source_role')), 'original ' + name + ' video')
        path = repository_file(ROOT, source.get('path'), 'original ' + name + ' input video')
        require(sha(path) == source.get('sha256'), 'Original video differs from its recorded hash: ' + name)
        fps = video.get('source_fps')
        total = video.get('source_frame_count')
        indices = video.get('selected_frame_indices')
        require(type(fps) in (int, float) and math.isfinite(fps) and 0 < fps <= 240 and
                type(total) is int and total > 0 and isinstance(indices, list) and
                len(indices) == action['registered_frames'] and
                all(type(index) is int and 0 <= index < total for index in indices),
                'Registered images must identify exact original video frames: ' + name)
        require(action['nominal_fps'] == fps,
                'Runtime exposure cadence differs from its original video registration: ' + name)
        require(len(set(indices)) == len(indices),
                'Repeated source images must use exposure steps rather than inflated artwork counts: ' + name)
        cycle_registered = action['cycle_registered_frame_indices']
        selection = {'indices': indices, 'selected_images': len(indices),
                     'distinct_source_frames': len(set(indices)),
                     'cycle_registered_frame_indices': cycle_registered,
                     'cycle_indices': [indices[index] for index in cycle_registered],
                     'cycle_step_count': action['cycle_step_count']}
        if name in ('walk', 'run'):
            selection['native_cycle_seconds'] = action['cycle_step_count'] / fps
            selection['native_speed_logical_px_s'] = action['stride_pixels'] * fps / action['cycle_step_count']
        result[name] = {'source': path.relative_to(ROOT).as_posix(), 'sha256': sha(path),
                        'native_fps': fps, 'native_frame_count': total, 'selection': selection,
                        'new_motion_drawings': 0, 'new_hand_drawn_frames': 0,
                        'appearance_retexturing_separate': True,
                        'timing_source': 'native timing recorded by extraction; bound to the original MP4 hash'}
    require(len({video['source'] for video in result.values()}) == len(result),
            'Walking, running and eating must retain their distinct original videos')
    return result


def video_appearance_contract(directory, provenance, actions):
    atlases = provenance.get('atlases', [])
    appearances = {}
    for name, action in actions.items():
        result = {}
        for character in (0, 8):
            atlas = exactly_one((a for a in atlases if a.get('character') == character and
                                  a.get('clip') == name and a.get('base_resource_id') == action['resource_id']),
                                 'source-video ' + name + ' texture for character ' + str(character))
            path = repository_file(directory, atlas.get('source'), 'source-video ' + name + ' atlas')
            require(path.resolve().is_relative_to(directory.resolve()) and sha(path) == atlas.get('sha256'),
                    'Character texture differs from its exact video appearance provenance: ' + name)
            require(atlas.get('image_width') == action['image_width'] and
                    atlas.get('image_height') == action['image_height'],
                    'Character texture does not share the registered source-video canvas: ' + name)
            result[character] = {'source': path.relative_to(ROOT).as_posix(), 'sha256': sha(path),
                                  'base_resource_id': action['resource_id'],
                                  'image_width': atlas['image_width'], 'image_height': atlas['image_height'],
                                  'appearance_method': atlas.get('appearance_method',
                                      provenance.get('appearance_method', 'supplied_character_texture'))}
        require(result[0]['sha256'] == action['sha256'] and result[0]['source'] == action['source'],
                'The active Cream texture differs from the recorded video extraction: ' + name)
        require(result[0]['sha256'] != result[8]['sha256'],
                'Cream and Tuxedo must select different character textures: ' + name)
        appearances[name] = result[8]
    return appearances


def authored_appearance_contract():
    from import_styled_sprites import prepare_import

    # This checks actual authored PNG hashes, base registration, complete
    # Tuxedo coverage, and exact generated header/RC/report bytes together.
    report = prepare_import(ROOT, check=True, require_character=8)
    manifest_path = ROOT / 'assets/styled-sprites/manifest.json'
    report_path = ROOT / 'assets/styled-sprites/import-report.json'
    return {'manifest': str(manifest_path.relative_to(ROOT)), 'manifest_sha256': sha(manifest_path),
            'import_report_sha256': sha(report_path), 'design': report['design'],
            'complete_characters': report['complete_characters'],
            'atlases': [atlas for atlas in report['sources'] if atlas['character'] == 8]}


def manual_motion_contract(path, expected_walk_frames=None, expected_walk_speed=None, expected_run_speed=None):
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    require(report.get('schema') == 1 and report.get('test') == 'manual-motion-native' and
            report.get('status') == 'passed' and report.get('exit_code', 0) == 0 and
            report.get('version') == VERSION, 'Native manual-motion test did not pass for this version')
    walk_frames = report.get('approved_walk_frames')
    require(report.get('mesh_walk_default') is False and type(walk_frames) is int and walk_frames >= 8,
            'Native report does not identify valid default walking playback')
    if expected_walk_frames is not None:
        require(walk_frames == expected_walk_frames,
                'Native default walking frame count differs from the published artwork manifest')
    require(report.get('menu_commands') == [2800, 2801, 2802] and
            report.get('menu_labels_checked') is True and
            isinstance(report.get('unsafe_states_checked'), int) and report['unsafe_states_checked'] > 0,
            'Native manual-motion menu or unsafe-state checks are missing')
    for key in ('pause_preference_preserved', 'suspension_clock_preserved'):
        require(report.get(key) is True, f'Native manual-motion check failed: {key}')
    characters = report.get('characters', [])
    require(len(characters) == 2 and {c.get('character') for c in characters} == {0, 8},
            'Native manual-motion report must cover both characters')
    for character in characters:
        for key in ('walk_distance', 'run_distance', 'walk_peak_speed', 'run_peak_speed', 'jump_max_lift'):
            value = character.get(key)
            require(isinstance(value, (int, float)) and math.isfinite(value) and value > 0,
                    f'Native manual-motion metric is missing or nonpositive: {key}')
        if expected_walk_speed is not None:
            require(abs(character['walk_peak_speed'] - expected_walk_speed) <=
                    max(.5, expected_walk_speed * .005),
                    'Native manual walking speed does not reproduce the source-video cadence')
        if expected_run_speed is not None:
            require(abs(character['run_peak_speed'] - expected_run_speed) <= max(.5, expected_run_speed * .005),
                    'Native running speed does not reproduce the source-video cadence')
        for key in ('jump_landed', 'moving_jump_braked'):
            require(character.get(key) is True, f'Native manual-motion check failed: {key}')
    return report


def native_walk_capture_contract(native_root, walk_contract):
    capture_path = exactly_one((p for p in native_root.rglob('capture.json')
                                if p.parent.name == 'walk-video'), 'native source-video walk capture')
    report_path = exactly_one(native_root.rglob('walk-video-report.txt'), 'native video capture report')
    require(report_path.read_text(encoding='utf-8-sig').startswith('exit=0 '),
            'The native source-video capture did not report success')
    metadata = json.loads(capture_path.read_text(encoding='utf-8-sig'))
    source_video = walk_contract['source_video']
    walking = source_video['clips']['walk']
    require(metadata.get('schema') == 1 and metadata.get('render_source') == 'Windows GDI+ / Box2D' and
            metadata.get('desktop_recording') is False and metadata.get('fps') == 60 and
            metadata.get('frame_count') == 600 and metadata.get('reference_height_px') == 110,
            'The release needs the real ten-second, 60 FPS native capture at normal character size')
    require(metadata.get('source_video_frames') == walk_contract['normal_walk_cycle_frames'] and
            metadata.get('cycle_frames') == walk_contract['normal_walk_cycle_frames'] and
            metadata.get('source_video_fps') == source_video['native_fps'] and
            metadata.get('source_video_frame_indices') == walking['cycle_indices'] and
            metadata.get('registered_walk_frames') == walk_contract['normal_walk_frames'] and
            metadata.get('registered_walk_source_indices') == walking['indices'],
            'The native capture does not use the exact original-video walking frames')
    require(metadata.get('trace') == 'trace.json', 'Native capture trace must stay beside its images')
    trace_path = capture_path.parent / 'trace.json'
    trace = json.loads(trace_path.read_text(encoding='utf-8-sig'))
    require(trace.get('schema') == 1 and trace.get('fps') == metadata['fps'] and
            isinstance(trace.get('frames'), list) and len(trace['frames']) == metadata['frame_count'],
            'Native walking trace differs from its declared sampling rate or frame count')
    characters = metadata.get('characters', [])
    require(len(characters) == 2 and {c.get('character') for c in characters} == {0, 8},
            'The native walking capture must show both current characters')
    source_count = walk_contract['normal_walk_frames']
    selected = walking['indices']
    cycle_indices = set(walking['cycle_registered_frame_indices'])
    expected_speed = walking['native_speed_logical_px_s']
    stats = {character: {'seen': set(), 'changes': 0, 'previous': None,
                         'peak': 0, 'stopped': False, 'reversed': False} for character in (0, 8)}
    for index, frame in enumerate(trace['frames']):
        require(frame.get('frame') == index and frame.get('elapsed_ms') == index * 1000 // metadata['fps'],
                'Native walking trace frames are not contiguous at the recorded clock')
        rows = frame.get('characters', [])
        require(len(rows) == 2 and {row.get('character') for row in rows} == {0, 8},
                'A native capture frame is missing a current character')
        for row in rows:
            character = row['character']
            stat = stats[character]
            for field in ('root_x', 'velocity_x', 'phase'):
                require(type(row.get(field)) in (int, float) and math.isfinite(row[field]),
                        'The native walk trace contains invalid physical coordinates')
            velocity = row['velocity_x']
            stat['peak'] = max(stat['peak'], abs(velocity))
            stat['stopped'] |= 390 <= index < 450 and abs(velocity) < .5
            stat['reversed'] |= index > 470 and velocity < -5
            # Motion::Walk=1 and Clip::Walk=9 are stable product enum values.
            require(not (row.get('requested_motion') == 1 and abs(velocity) > 2) or row.get('sprite_clip') == 9,
                    'Requested walking was replaced by another locomotion clip')
            if row.get('sprite_clip') == 9:
                source = row.get('sprite_frame')
                require(type(source) is int and 0 <= source < source_count and
                        row.get('input_video_frame') == selected[source],
                        'The native walking trace does not select the recorded source-video frame')
                if source in cycle_indices:
                    stat['seen'].add(source)
                    if stat['previous'] is not None and source != stat['previous']:
                        stat['changes'] += 1
                    stat['previous'] = source
                else:
                    stat['previous'] = None
            else:
                require(row.get('input_video_frame') is None,
                        'An unrelated native pose is incorrectly labelled a source-video frame')
                stat['previous'] = None
    for character in characters:
        stat = stats[character['character']]
        require(character.get('all_twenty_frames_used') is True and character.get('stopped') is True and
                character.get('reversed') is True and stat['seen'] == cycle_indices and
                stat['stopped'] and stat['reversed'],
                'Native capture did not complete source-frame coverage, stopping and reversal')
        require(type(character.get('peak_walk_speed')) in (int, float) and
                abs(character['peak_walk_speed'] - expected_speed) <= .5 and
                abs(stat['peak'] - expected_speed) <= .5 and
                character.get('source_frame_changes') == stat['changes'] and stat['changes'] >= len(cycle_indices),
                'Native capture movement or source-frame cadence differs from its trace')
        maximum_changes = math.ceil(metadata['frame_count'] * source_video['native_fps'] / metadata['fps']) + 10
        require(stat['changes'] <= maximum_changes, 'Native capture advances source poses faster than the original video')
    return capture_path.parent, {
        'capture': metadata, 'capture_sha256': sha(capture_path), 'trace_sha256': sha(trace_path),
        'native_report_sha256': sha(report_path), 'native_capture_step': CAPTURE_STEP,
    }


def native_motion_capture_contract(native_root, artwork_contract):
    capture_path = exactly_one((p for p in native_root.rglob('capture.json')
                                if p.parent.name == 'motion-video'), 'native meal, walk and run capture')
    report_path = exactly_one(native_root.rglob('motion-video-report.txt'), 'native motion capture report')
    require(report_path.read_text(encoding='utf-8-sig').startswith('exit=0 '),
            'The native meal, walk and run capture did not report success')
    metadata = json.loads(capture_path.read_text(encoding='utf-8-sig'))
    actions = artwork_contract['video_action_artwork']
    videos = artwork_contract['source_videos']
    meal_time = 500
    walk_time = (meal_time + actions['walk']['movement_delay_ms'] +
                 actions['eat']['program_duration_ms'] + actions['walk']['program_sections']['outro']['duration_ms'] + 600)
    run_time = walk_time + 6200
    duration = run_time + 5000
    require(metadata.get('schema') == 1 and metadata.get('render_source') == 'Windows GDI+ / Box2D' and
            metadata.get('desktop_recording') is False and metadata.get('fps') == 60 and
            metadata.get('canvas') == [960, 500] and metadata.get('reference_height_px') == 110 and
            metadata.get('trace') == 'trace.json' and metadata.get('profile') == 'isolated capture fixture only' and
            metadata.get('schedule_ms') == {'meal': meal_time, 'walk': walk_time, 'run': run_time, 'duration': duration} and
            0 < metadata.get('frame_count', 0) == math.ceil(duration * 60 / 1000) <= 1800,
            'The release requires the complete native meal, walk, run and settling scenario at 60 FPS')
    clip_ids = {'walk': 9, 'run': 10, 'eat': 5}
    clips = metadata.get('clips', [])
    require(len(clips) == len(clip_ids), 'Native motion capture has unexpected action registrations')
    for name, clip_id in clip_ids.items():
        action = actions[name]
        clip = exactly_one((c for c in clips if c.get('clip') == clip_id), 'native ' + name + ' registration')
        require(clip.get('resource') == action['resource_id'] and clip.get('source_path') == action['source'] and
                clip.get('art_frames') == action['registered_frames'] and
                clip.get('loop_steps') == action['cycle_step_count'] and
                clip.get('source_fps') == videos[name]['native_fps'] and
                clip.get('reference_height') == action['reference_height'] and
                clip.get('stride_pixels') == action['stride_pixels'],
                'Native motion capture differs from the exact source-video registration: ' + name)
    trace_path = capture_path.parent / 'trace.json'
    trace = json.loads(trace_path.read_text(encoding='utf-8-sig'))
    require(trace.get('schema') == 1 and trace.get('fps') == 60 and
            isinstance(trace.get('frames'), list) and len(trace['frames']) == metadata['frame_count'],
            'Native motion trace differs from its declared clock or frame count')
    stats = {character: {'eat': set(), 'run': set(), 'peak_walk': 0, 'peak_run': 0,
                         'walk_stopped': False, 'final_stopped': False} for character in (0, 8)}
    names_by_clip = {clip: name for name, clip in clip_ids.items()}
    for index, frame in enumerate(trace['frames']):
        elapsed = index * 1000 // 60
        require(frame.get('frame') == index and frame.get('elapsed_ms') == elapsed,
                'Native action trace is not contiguous at 60 FPS')
        rows = frame.get('characters', [])
        require(len(rows) == 2 and {row.get('character') for row in rows} == {0, 8},
                'A native action frame is missing a current character')
        for row in rows:
            character = row['character']
            stat = stats[character]
            require(all(type(row.get(key)) in (int, float) and math.isfinite(row[key])
                        for key in ('root_x', 'canvas_root_x', 'velocity_x', 'phase')) and
                    row.get('floor_y') == (245 if character == 0 else 460) and
                    row.get('reference_height_px') == 110,
                    'Native action trace contains invalid geometry or character scale')
            name = names_by_clip.get(row.get('sprite_clip'))
            if name is not None:
                action = actions[name]
                source = row.get('sprite_frame')
                require(type(source) is int and 0 <= source < action['registered_frames'] and
                        row.get('sprite_resource') == action['resource_id'] and
                        row.get('source_reference_height') == action['reference_height'],
                        'Native action pixels do not select the exact source-video atlas: ' + name)
                if name in ('eat', 'run'):
                    stat[name].add(source)
                if name == 'eat':
                    require(meal_time <= elapsed < walk_time,
                            'The native meal did not finish before locomotion')
            velocity = abs(row['velocity_x'])
            if row.get('requested_motion') == 1:
                stat['peak_walk'] = max(stat['peak_walk'], velocity)
            if row.get('requested_motion') == 9:
                stat['peak_run'] = max(stat['peak_run'], velocity)
            stat['walk_stopped'] |= run_time - 400 <= elapsed < run_time and velocity < .5
            stat['final_stopped'] |= elapsed > run_time + 4200 and velocity < .5
    characters = metadata.get('characters', [])
    require(len(characters) == 2 and {c.get('character') for c in characters} == {0, 8},
            'The native meal, walk and run capture must show both characters')
    for character in characters:
        stat = stats[character['character']]
        require(set(actions['eat']['program_registered_frame_indices']) <= stat['eat'] and
                set(actions['run']['cycle_registered_frame_indices']) <= stat['run'] and
                character.get('meal_art_frames_used') == len(stat['eat']) and
                character.get('run_art_frames_used') == len(stat['run']) and
                character.get('meal_finished') is True and character.get('stopped') is True and
                stat['walk_stopped'] and stat['final_stopped'],
                'Native capture omitted source artwork, completed meal or settled transitions')
        for name in ('walk', 'run'):
            speed = videos[name]['selection']['native_speed_logical_px_s']
            recorded = character.get('peak_' + name + '_speed')
            require(type(recorded) in (int, float) and math.isfinite(recorded) and
                    abs(recorded - speed) <= .5 and abs(stat['peak_' + name] - speed) <= .5,
                    'Native action speed differs from its source-video cadence: ' + name)
    return capture_path.parent, {
        'capture': metadata, 'capture_sha256': sha(capture_path), 'trace_sha256': sha(trace_path),
        'native_report_sha256': sha(report_path), 'native_capture_step': MOTION_CAPTURE_STEP,
    }


def archive_files(output, paths):
    seen = set()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, name in sorted(paths, key=lambda item: item[1]):
            require(path.is_file() and not path.is_symlink(), f'Invalid evidence file: {path}')
            require(name not in seen and not name.startswith('/') and '..' not in Path(name).parts,
                    f'Unsafe or duplicate evidence archive path: {name}')
            seen.add(name)
            info = zipfile.ZipInfo(name, (2026, 11, 10, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            with path.open('rb') as source, archive.open(info, 'w', force_zip64=True) as destination:
                shutil.copyfileobj(source, destination, 4 * 1024 * 1024)


def public_bytes(url, limit):
    # No token is sent: clients must be able to fetch these public URLs too.
    request = Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'YeopjariCat-release-verifier'})
    with urlopen(request, timeout=30) as response:
        data = response.read(limit + 1)
    require(len(data) <= limit, f'Public response exceeds size limit: {url}')
    return data


def verify_public_update(package, info, expected, sizes):
    last_error = ''
    for attempt in range(12):
        try:
            latest = api('releases/latest')
            require(latest['tag_name'] == TAG and not latest['draft'] and not latest['prerelease'],
                    'GitHub latest release is not the new stable update')
            raw = public_bytes(FEED_URL, release.MAX_MANIFEST)
            require(raw == (package / 'latest.txt').read_bytes(), 'Official latest feed has not reached the exact tested manifest')
            manifest = release.parse_manifest(raw)
            require(manifest['version'] == VERSION and manifest['download_url'] == info['download_url'],
                    'Official latest feed identifies the wrong version or executable')
            # Verify all public package assets and provenance, including the exact
            # EXE bytes referenced by the official feed, after public promotion.
            for name, digest in expected.items():
                data = public_bytes(BASE_URL + name, sizes[name])
                require(hashlib.sha256(data).hexdigest() == digest, f'Public download hash mismatch: {name}')
                if name == f'YeopjariCat-{VERSION}.exe':
                    require(len(data) == int(manifest['size_bytes']) and digest == manifest['sha256'],
                            'Public executable does not match the official latest feed')
            return
        except (OSError, ValueError, subprocess.CalledProcessError) as exc:
            last_error = f'{type(exc).__name__}: {exc}'
            print(f'Public verification attempt {attempt + 1}/12: {last_error}', flush=True)
            if attempt < 11:
                time.sleep(5)
    raise ValueError('Stable release was published, but public delivery verification failed: ' + last_error)


def publication_result(published, head, run_id, walk_contract, expected):
    result = {'release': published['url'], 'source_commit': head, 'validation_run': int(run_id),
              'stable_latest': TAG, 'official_feed': FEED_URL, 'public_delivery_verified': True,
              **walk_contract, 'asset_sha256': expected}
    print(json.dumps(result, indent=2), flush=True)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as output:
            output.write(f'Published [{TAG}]({published["url"]}) from `{head}`.\n\n'
                         f'Official [latest feed]({FEED_URL}) and public asset hashes verified.\n\n'
                         f'Registered walking images: {walk_contract["normal_walk_frames"]}. '
                         f'Cycle poses: {walk_contract["normal_walk_cycle_frames"]}; parked pose: {walk_contract["normal_walk_standing_frame"]}. '
                         'Original video, selected frames, contact data and registration are recorded in windows-validation.json.\n')


def main():
    require(os.environ.get('GITHUB_ACTIONS') == 'true', 'This publisher runs only in GitHub Actions')
    require(os.environ.get('GITHUB_REPOSITORY') == REPO, 'Wrong repository')
    require(os.environ.get('GITHUB_REF') == 'refs/heads/main', 'Dispatch from main only')
    os.chdir(ROOT)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    require(re.fullmatch(r'[0-9a-f]{40}', head), 'Invalid source commit')
    run_id = os.environ.get('VALIDATION_RUN', '')
    validated_source(run_id, head)
    require(release.version_from_header(ROOT / 'src/app_version.h') == VERSION, 'Wrong source version')
    walk_contract = normal_walk_contract()
    walk_contract['authored_appearance'] = authored_appearance_contract()
    for name, expected in walk_contract['video_tuxedo_appearances'].items():
        selected = exactly_one((a for a in walk_contract['authored_appearance']['atlases']
                                if a['base_resource_id'] == expected['base_resource_id']), 'runtime Tuxedo ' + name)
        require(selected['sha256'] == expected['sha256'],
                'Runtime Tuxedo texture differs from the exact source-video appearance: ' + name)
    stable_before = api('releases/latest')['tag_name']
    require(stable_before in (PREVIOUS_STABLE, TAG),
            f'Stable latest changed from {PREVIOUS_STABLE} to {stable_before}; reassess this publication')

    metadata = exactly_one(Path('verified-package').rglob('release-info.json'), 'package metadata')
    package = metadata.parent
    info = release.verify_release(package)
    require(info['version'] == VERSION and info['github_repository'] == REPO and
            info['feed_url'] == FEED_URL and info['download_url'] == BASE_URL + f'YeopjariCat-{VERSION}.exe',
            'Wrong verified package, feed or download URL')
    exe = package / f'YeopjariCat-{VERSION}.exe'
    verify_silent_exe(exe.read_bytes())
    native_root = Path('native-review')
    report = exactly_one(native_root.rglob('render-self-test.txt'), 'native render report')
    require(report.read_text(encoding='utf-8-sig').startswith('exit=0 '), 'Native render artifact does not report exit=0')
    manual_report = exactly_one(native_root.rglob('manual-motion-self-test.json'), 'native manual-motion report')
    manual_contract = manual_motion_contract(manual_report, walk_contract['normal_walk_frames'],
        walk_contract['source_video']['clips']['walk']['native_speed_logical_px_s'],
        walk_contract['source_videos']['run']['selection']['native_speed_logical_px_s'])
    capture_directory, capture_contract = native_walk_capture_contract(native_root, walk_contract)
    motion_directory, motion_capture_contract = native_motion_capture_contract(native_root, walk_contract)

    history = json.loads(gh('release', 'list', '--repo', REPO, '--limit', '1000',
                           '--json', 'tagName,isDraft,isPrerelease'))
    require(len(history) < 1000, 'Release history exceeds bounded inspection')
    existing = exactly_one((r for r in history if r['tagName'] == TAG), 'pre-created release draft')
    require(not existing['isPrerelease'], 'The command update must be a stable release')
    already_public = not existing['isDraft']
    require(stable_before == (TAG if already_public else PREVIOUS_STABLE),
            'A published update may only be reverified while it is the latest stable release')
    for item in history:
        if not item['isDraft'] and not item['isPrerelease']:
            if already_public and item['tagName'] == TAG:
                continue
            require(item['tagName'].startswith('v') and
                    release.version_tuple(item['tagName'][1:]) < release.version_tuple(VERSION),
                    'Stable release history contains an equal or newer version')
    draft = json.loads(gh('release', 'view', TAG, '--repo', REPO, '--json', 'isDraft,isPrerelease,targetCommitish,assets,url'))
    require(draft['isDraft'] == (not already_public) and not draft['isPrerelease'] and draft['targetCommitish'] == head,
            'The stable release must target exactly the validated commit')
    tag_ref = exactly_one((r for r in api('git/matching-refs/tags/' + TAG)
                           if r['ref'] == 'refs/tags/' + TAG), 'pre-created release tag')
    require(tag_ref and api('commits/' + TAG)['sha'] == head, 'Release tag differs from the validated commit')

    with tempfile.TemporaryDirectory(prefix='motion-release-') as directory:
        stage = Path(directory)
        files = [package / name for name in info['files']] + [metadata]
        native_report = stage / 'render-self-test.txt'
        shutil.copyfile(report, native_report)
        files.append(native_report)
        native_manual_report = stage / 'manual-motion-self-test.json'
        shutil.copyfile(manual_report, native_manual_report)
        files.append(native_manual_report)
        # Encode the exact captured native pixels at their real sampling rate.
        # There is no pose interpolation, playback retiming or desktop-recording claim.
        from encode_locomotion_capture import encode
        for directory, basename, contract in (
                (capture_directory, 'walking-native', capture_contract),
                (motion_directory, 'motions-native', motion_capture_contract)):
            native_video = stage / (basename + '.mp4')
            contract['encoding'] = encode(directory, native_video)
            contract['video_sha256'] = sha(native_video)
            files += [native_video, native_video.with_suffix('.json')]
            for source_name, suffix in (('capture.json', '-capture.json'), ('trace.json', '-trace.json')):
                destination = stage / (basename + suffix)
                shutil.copyfile(directory / source_name, destination)
                files.append(destination)
        # Include actual command evidence when the native test emitted it; no
        # encoder or unrelated mesh-review media is needed for this release.
        command_roots = [p for p in native_root.rglob('*')
                         if p.is_dir() and p.name in ('manual-command', 'manual-commands')]
        require(len(command_roots) <= 1, 'Ambiguous native manual-command evidence')
        command_hashes = {}
        if command_roots:
            command_root = command_roots[0]
            evidence_files = [(p, str(p.relative_to(command_root))) for p in command_root.rglob('*') if p.is_file()]
            require(evidence_files, 'Native manual-command evidence directory is empty')
            command_hashes = {name: sha(path) for path, name in evidence_files}
            evidence = stage / 'manual-command-evidence.zip'
            archive_files(evidence, evidence_files)
            files.append(evidence)
        proof = stage / 'windows-validation.json'
        proof.write_text(json.dumps({
            'version': VERSION, 'source_commit': head, 'validation_run': int(run_id),
            'validation_url': f'https://github.com/{REPO}/actions/runs/{run_id}',
            'native_self_test': 'passed', 'native_self_test_step': SAVE_STEP,
            'native_gdi_render_test': 'passed', 'native_gdi_render_step': RENDER_STEP,
            'native_render_report_sha256': sha(report), 'exe_sha256': sha(exe),
            'native_manual_motion_test': 'passed', 'native_manual_motion_report_sha256': sha(manual_report),
            'native_manual_motion': manual_contract,
            'native_walk_capture': capture_contract,
            'native_motion_capture': motion_capture_contract,
            **walk_contract,
            'archived_mesh_review_enabled': False, 'desktop_recording': False,
            'manual_command_evidence_sha256': command_hashes,
        }, indent=2) + '\n', encoding='utf-8')
        files.append(proof)
        sums = stage / 'motion-update-SHA256SUMS.txt'
        sums.write_text(''.join(sha(p) + '  ' + p.name + '\n' for p in sorted(files, key=lambda p: p.name)), encoding='ascii')
        files.append(sums)
        require(len({p.name for p in files}) == len(files), 'Duplicate release asset names')
        expected = {p.name: sha(p) for p in files}
        sizes = {p.name: p.stat().st_size for p in files}
        notes = stage / 'notes.md'
        notes.write_text((package / 'RELEASE_NOTES_KO.txt').read_text(encoding='utf-8') + '\n\n' +
                         '고양이 우클릭 → 행동시키기 → 걷기·달리기·점프하기를 선택할 수 있습니다.\n'
                         '고양이 우클릭 → 프로그램 업데이트 → 업데이트 확인…에서 설치할 수 있습니다.\n\n'
                         f'- [Windows ZIP]({BASE_URL}YeopjariCat-Windows-{VERSION}.zip)\n'
                         f'- [Windows EXE]({BASE_URL}YeopjariCat-{VERSION}.exe)\n'
                         f'- [실제 앱 렌더링으로 보는 걷기 영상]({BASE_URL}walking-native.mp4)\n'
                         f'- [밥 먹기·걷기·달리기 영상]({BASE_URL}motions-native.mp4)\n'
                         f'- [검증 출처와 SHA-256]({BASE_URL}windows-validation.json)\n\n'
                         '보내준 영상의 걷기·달리기·밥 먹기를 적용했습니다. 다른 돌봄 기능과 성장 기록은 유지합니다.\n'
                         '미리보기는 실제 앱 그리기로 출력한 60FPS 영상이며, 바탕화면 녹화는 아닙니다.\n'
                         f'검증 소스: `{head}` · [Windows 실행 검사](https://github.com/{REPO}/actions/runs/{run_id}).\n',
                         encoding='utf-8')
        if already_public:
            # A CDN failure after promotion can be retried without touching any
            # public release metadata or bytes. Every expected asset must match.
            require({a['name'] for a in draft['assets']} == set(expected), 'Published release asset set differs')
            with tempfile.TemporaryDirectory(prefix='motion-public-verified-') as downloaded:
                gh('release', 'download', TAG, '--repo', REPO, '--dir', downloaded)
                require({p.name: sha(p) for p in Path(downloaded).iterdir()} == expected,
                        'Published release differs from the exact validated artifacts; nothing was changed')
            verify_public_update(package, info, expected, sizes)
            publication_result(draft, head, run_id, walk_contract, expected)
            return
        require({a['name'] for a in draft['assets']} <= set(expected), 'Unexpected assets in existing draft')
        with tempfile.TemporaryDirectory(prefix='motion-existing-') as previous:
            if draft['assets']:
                gh('release', 'download', TAG, '--repo', REPO, '--dir', previous)
            already = {p.name: sha(p) for p in Path(previous).iterdir()}
            for name, digest in already.items():
                require(expected[name] == digest, f'Draft asset differs and was not overwritten: {name}')
            for path in files:
                if path.name not in already:
                    gh('release', 'upload', TAG, '--repo', REPO, str(path))
        with tempfile.TemporaryDirectory(prefix='motion-verified-') as downloaded:
            gh('release', 'download', TAG, '--repo', REPO, '--dir', downloaded)
            require({p.name: sha(p) for p in Path(downloaded).iterdir()} == expected,
                    'Uploaded release files failed download hash verification')
        current = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                                '--json', 'isDraft,isPrerelease,targetCommitish'))
        require(current['isDraft'] and not current['isPrerelease'] and current['targetCommitish'] == head,
                'Draft metadata changed before publication')
        require(api('commits/' + TAG)['sha'] == head, 'Tag changed before publication')
        require(api('releases/latest')['tag_name'] == stable_before, 'Stable latest changed before publication')
        gh('release', 'edit', TAG, '--repo', REPO, '--notes-file', str(notes),
           '--title', f'옆자리 고양이 {VERSION} — 발을 딛고 걷는 새 보행',
           '--draft=false', '--prerelease=false', '--latest=true')
        published = json.loads(gh('release', 'view', TAG, '--repo', REPO,
                                 '--json', 'isDraft,isPrerelease,targetCommitish,url'))
        require(not published['isDraft'] and not published['isPrerelease'] and
                published['targetCommitish'] == head, 'Published stable metadata differs')
        verify_public_update(package, info, expected, sizes)
        publication_result(published, head, run_id, walk_contract, expected)


if __name__ == '__main__':
    main()
