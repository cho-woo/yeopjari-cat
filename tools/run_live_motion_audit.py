"""Record isolated real-time app runs from one pinned Windows executable.

The framebuffer mode copies painted shipping HWND surfaces. It is neither a
desktop recording nor an accelerated renderer simulation. Ordinary user data,
global hotkeys, OS cursor/keyboard input and updater initialization are unused.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

GROUPS = {
    'social': (45, 45, 3, 240),
    'wall': (44, 44, 3, 300),
    'controllers': (40, 43, 1, 480),
    'young_scaled': (40, 43, 1, 480),
    'specials': (32, 39, 3, 1200),
    'states': (0, 31, 3, 2400),
    'autonomy': (46, 46, 1, 900),
}
PAIRS = ((0, 8), (8, 10), (10, 0))


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    temp.replace(path)


def inspect_trace(path, primary, companion):
    with path.open(newline='', encoding='utf-8-sig') as stream:
        rows = list(csv.DictReader(stream))
    last = {}
    warps = []
    covers = collections.defaultdict(lambda: collections.defaultdict(set))
    gaps = []
    for row in rows:
        role = int(row['role'])
        if int(row['coat']) != (companion if role else primary):
            raise ValueError('Actor identity metadata changed')
        point = tuple(float(row[key]) for key in ('x', 'lift', 'vx', 'vy'))
        if not all(math.isfinite(n) for n in point):
            raise ValueError('Non-finite actor physics')
        now = float(row.get('wall_precise_ms') or row['wall_ms'])
        if role in last:
            prior, then = last[role]
            dt = (now - then) / 1000
            if dt < 0:
                raise ValueError('Wall clock moved backward')
            if (abs(point[0] - prior[0]) > max(abs(point[2]), abs(prior[2])) * dt + 2 or
                    abs(point[1] - prior[1]) > max(abs(point[3]), abs(prior[3])) * dt + 2):
                warps.append({'role': role, 'wall_ms': now, 'previous': prior, 'current': point})
        last[role] = point, now
        clip = f"scene-{row['scene']}" if int(row['scene']) else 'pounce' if int(row['pounce']) else f"clip-{row['clip']}"
        covers[(int(row['action']), int(row['repeat']), role)][clip].add(int(row['source']))
        if role == 0:
            gaps.append(int(row['timer_gap_ms']))
    coverage = [{'action': key[0], 'repeat': key[1], 'role': key[2],
                 'clips': {name: {'first': min(frames), 'last': max(frames), 'distinct': len(frames)}
                           for name, frames in clips.items()}}
                for key, clips in sorted(covers.items())]
    expected = {1: 9, 2: 1, 3: 5, 4: 0, 5: 22, 7: 9, 8: 6, 9: 10, 10: 23, 11: 4,
                12: 6, 13: 7, 16: 24, 18: 11, 20: 0, 22: 4, 32: 15, 33: 16, 34: 17,
                35: 19, 36: 3, 41: 14, 44: 18, 45: 4}
    missing = []
    for key, clips in covers.items():
        action, repeat, role = key
        if action in expected and f'clip-{expected[action]}' not in clips:
            missing.append({'action': action, 'repeat': repeat, 'role': role, 'wanted': expected[action]})
        if 37 <= action <= 39 and f'scene-{action - 36}' not in clips:
            missing.append({'action': action, 'repeat': repeat, 'role': role, 'wanted': 'furniture movie'})
        if action == 40 and not ({'clip-13', 'pounce'} & clips.keys()):
            missing.append({'action': action, 'repeat': repeat, 'role': role, 'wanted': 'ball interaction'})
        if action in (42, 43) and not ({'clip-9', 'clip-10'} & clips.keys()):
            missing.append({'action': action, 'repeat': repeat, 'role': role, 'wanted': 'locomotion response'})
        if action == 43 and not {'clip-6', 'clip-20'} <= clips.keys():
            missing.append({'action': action, 'repeat': repeat, 'role': role, 'wanted': 'keyboard jump and planted turn'})
    return {'rows': len(rows), 'physical_discontinuities': len(warps), 'warp_events': warps[:30],
            'timer_mean_ms': statistics.mean(gaps) if gaps else None,
            'timer_histogram': dict(collections.Counter(gaps)), 'missing_action_coverage': missing, 'coverage': coverage}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--encoder', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--ci-run', required=True)
    parser.add_argument('--version', default='2.0.41')
    parser.add_argument('--groups', nargs='+', choices=GROUPS, default=list(GROUPS))
    parser.add_argument('--workers', type=int, choices=(1, 2), default=2)
    args = parser.parse_args()
    exe, encoder, output = args.exe.resolve(), args.encoder.resolve(), args.output.resolve()
    actual_sha = digest(exe)
    if actual_sha.lower() != args.expected_sha256.lower():
        raise SystemExit('Executable does not match the pinned package hash')
    output.mkdir(parents=True, exist_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    results = []
    cases = [(primary, companion, group) for group in args.groups for primary, companion in PAIRS]

    def run(case):
        primary, companion, group = case
        base = output / f'pair-c{primary}-c{companion}' / group
        base.mkdir(parents=True, exist_ok=True)
        # Preserve previous failed/interrupted observations rather than overwriting evidence.
        prior = sorted(base.glob('attempt-*/execution.json'))
        for receipt in prior:
            old = json.loads(receipt.read_text(encoding='utf-8'))
            if old.get('passed') and old.get('exe_sha256') == actual_sha:
                return old
        attempt = base / f'attempt-{len(list(base.glob("attempt-*"))) + 1}'
        attempt.mkdir(exist_ok=False)
        first, last, repeat, timeout = GROUPS[group]
        age_days, dpi = (7, 1.5) if group == 'young_scaled' else (366, 1)
        result = {'group': group, 'primary': primary, 'companion': companion,
                  'exe_sha256': actual_sha, 'source_commit': args.source_commit, 'ci_run': args.ci_run,
                  'directory': str(attempt), 'passed': False, 'status': 'running'}
        write(attempt / 'execution.json', result)
        print(json.dumps({'event': 'starting', 'primary': primary, 'companion': companion, 'group': group}), flush=True)
        start = time.monotonic()
        try:
            completed = subprocess.run([str(exe), '--live-motion-audit', str(primary), str(first), str(last),
                                        str(repeat), str(encoder), '--framebuffer', '--companion-coat', str(companion),
                                        '--age-days', str(age_days), '--dpi-scale', str(dpi)],
                                       cwd=attempt, startupinfo=startup, timeout=timeout, capture_output=True)
            result['exit_code'] = completed.returncode
            report = json.loads((attempt / 'report.json').read_text())
            result['native'] = report
            result['trace'] = inspect_trace(attempt / 'trace.csv', primary, companion)
            result['passed'] = (completed.returncode == 0 and report['version'] == args.version and
                                report['completed'] == report['requested'] == (last - first + 1) * repeat and
                                report['native_app_framebuffer_capture'] and not report['desktop_compositor_capture'] and
                                report['hidden_layers_excluded'] and
                                report['capture_actors_complete'] and
                                not report['encoder_failed'] and report['fixture_save_passed'] and
                                report['render_epoch_mismatches'] == 0 and
                                result['trace']['physical_discontinuities'] == 0 and not result['trace']['missing_action_coverage'])
            result['status'] = 'passed' if result['passed'] else 'failed'
        except Exception as error:
            result['status'], result['error'] = 'failed', str(error)
        result['elapsed_seconds'] = round(time.monotonic() - start, 3)
        write(attempt / 'execution.json', result)
        print(json.dumps({'event': result['status'], 'primary': primary, 'companion': companion,
                          'group': group, 'elapsed_seconds': result['elapsed_seconds'],
                          'error': result.get('error')}), flush=True)
        return result

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for result in pool.map(run, cases):
            results.append(result)
            write(output / 'verification.json', {'version': args.version, 'exe_sha256': actual_sha,
                  'source_commit': args.source_commit, 'ci_run': args.ci_run,
                  'capture': 'real-time app framebuffer; scripted input; not desktop recording; not synthetic time',
                  'completed_cases': len(results), 'requested_cases': len(cases),
                  'passed': len(results) == len(cases) and all(item['passed'] for item in results), 'cases': results})
    return 0 if all(result['passed'] for result in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
