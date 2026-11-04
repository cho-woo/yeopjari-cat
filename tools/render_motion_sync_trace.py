#!/usr/bin/env python3
"""Export actual Box2D/locomotion/placement timing evidence (no native cat art).

Requires the native archive from tools/test.sh, g++, and matplotlib. Outputs
CSV, PNG, SVG and a JSON summary with hashes of the production inputs. The
legacy comparison applies the prior current-position gait/rounded-Y rules to
these same real Controller samples; it does not execute a historical binary.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/previews/motion-sync')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    library = ROOT/'build/native-box2d/libbox2d.a'
    if not library.exists():
        raise SystemExit('Run bash tools/test.sh first to build native Box2D')
    executable = output/'motion_sync_trace'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra',str(ROOT/'tools/motion_sync_trace.cpp'),
                    str(ROOT/'src/cat_physics.cpp'),'-I'+str(ROOT/'third_party/box2d/include'),
                    str(library),'-lm','-o',str(executable)],check=True)
    with (output/'motion-sync.csv').open('w') as target:
        subprocess.run([str(executable)],stdout=target,check=True)
    groups = {}
    with (output/'motion-sync.csv').open() as source:
        for row in csv.DictReader(source):
            scenario = row.pop('scenario')
            groups.setdefault(scenario,[]).append({key:float(value) for key,value in row.items()})
    summary = {'scope':'Production Box2D Controller, Locomotion and BuildPlacement; no native Windows/cat-art claim',
               'comparison':'Prior current-position gait and rounded-Y rules on the same Controller samples, not a historical binary',
               'scenarios':{}}
    for name, rows in groups.items():
        summary['scenarios'][name] = {
            'samples':len(rows),
            'max_legacy_gait_vs_render_distance_error_px':max(abs(r['legacy_gait_distance']-abs(r['relative_render_dx'])) for r in rows),
            'max_synchronized_gait_vs_render_distance_error_px':max(abs(r['synchronized_gait_distance']-abs(r['relative_render_dx'])) for r in rows),
            'max_legacy_y_rounding_error_screen_px':max(abs(r['legacy_rounded_y']-r['expected_y']) for r in rows),
            'max_placement_y_error_screen_px':max(abs(r['placed_y']-r['expected_y']) for r in rows),
        }
    paused = [r for r in groups['halfstep_pause_120hz'] if r['paused']]
    origin = groups['halfstep_pause_120hz'][0]['render_x']
    summary['pause'] = {
        'max_visible_displacement_while_paused_px':max(abs(r['render_x']-origin) for r in paused),
        'prior_collapse_to_current_rule_would_jump_px':paused[0]['current_x']-origin,
    }
    sources = ['src/cat_physics.cpp','src/cat_physics.hpp','src/cat_animation.hpp',
               'src/cat_deformation.hpp','src/cat_render_placement.hpp','tools/motion_sync_trace.cpp',
               'tools/render_motion_sync_trace.py','build/native-box2d/libbox2d.a']
    summary['sources_sha256'] = {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
    (output/'motion-sync-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    os.environ.setdefault('MPLCONFIGDIR',str(output/'matplotlib-cache'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(3,2,figsize=(13,10),constrained_layout=True)
    for column,name in enumerate(('walk_120hz','walk_16ms')):
        rows=groups[name][:36]
        x=[r['time']*1000 for r in rows]
        ax=axes[0,column]
        ax.plot(x,[r['legacy_gait_distance'] for r in rows],color='#c45c39',label='prior gait sampling',alpha=.8)
        ax.plot(x,[r['synchronized_gait_distance'] for r in rows],color='#197858',label='synchronized gait',linewidth=2)
        ax.scatter(x,[abs(r['relative_render_dx']) for r in rows],s=11,color='#253346',label='visible body distance')
        ax.set(title=f'{name}: distance per displayed frame',xlabel='elapsed ms',ylabel='body pixels')
    for column,name in enumerate(('moving_support_rest_120hz','moving_support_walk_120hz')):
        rows=groups[name][:60];x=[r['time'] for r in rows];ax=axes[1,column]
        ax.plot(x,[r['render_dx'] for r in rows],label='world movement',color='#526fba')
        ax.plot(x,[r['relative_render_dx'] for r in rows],label='relative to moving ledge',color='#253346')
        ax.plot(x,[r['synchronized_gait_distance'] for r in rows],label='gait distance',color='#197858',linestyle='--')
        ax.set(title=name,xlabel='elapsed seconds',ylabel='pixels per displayed frame')
    rows=groups['halfstep_pause_120hz'];x=[r['time'] for r in rows]
    axes[2,0].plot(x,[r['render_x']-origin for r in rows],label='visible root',color='#197858')
    axes[2,0].axvspan(0,.5,color='#a4b4c9',alpha=.2,label='paused')
    axes[2,0].scatter([0],[summary['pause']['prior_collapse_to_current_rule_would_jump_px']],color='#c45c39',label='prior pause-rule jump')
    axes[2,0].set(title='Half-step pause at 320 px/s',xlabel='elapsed seconds',ylabel='visible displacement (px)',xlim=(-.015,.62),ylim=(-1,40))
    rows=[r for r in groups['jump_120hz'] if .25<=r['time']<=.43];x=[r['time'] for r in rows]
    axes[2,1].step(x,[r['legacy_rounded_y'] for r in rows],where='mid',color='#c45c39',label='prior rounded Y')
    axes[2,1].plot(x,[r['placed_y'] for r in rows],color='#197858',label='fractional Y placement')
    axes[2,1].set(title='Real jump near apex, 125% DPI',xlabel='elapsed seconds',ylabel='screen foot Y (px)')
    axes[2,1].invert_yaxis()
    for ax in axes.flat:
        ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle('Production motion timing diagnostics — actual Box2D, no fitted physics curves')
    fig.savefig(output/'motion-sync.png',dpi=160)
    fig.savefig(output/'motion-sync.svg')
    plt.close(fig)
    print(json.dumps({'output':str(output),'pause':summary['pause'],
                      'walk_120hz':summary['scenarios']['walk_120hz']},indent=2))

if __name__=='__main__':
    main()
