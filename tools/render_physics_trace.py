#!/usr/bin/env python3
"""Trace the real Box2D Controller and export physics plots/GIF diagnostics.

Run after tools/setup.sh and tools/test.sh (which builds native Box2D).
Developer dependencies: matplotlib and Pillow. No equations are substituted for
Controller outputs. Plotted circle is the body's contact marker, not cat art.
"""
from __future__ import annotations
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build' / 'previews' / 'physics'
OUTPUT.mkdir(parents=True, exist_ok=True)
os.environ.setdefault('MPLCONFIGDIR', str(OUTPUT / 'matplotlib-cache'))
os.environ.setdefault('XDG_CACHE_HOME', str(OUTPUT / 'cache'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from PIL import Image, ImageDraw

SCENES = ('rise','moving','closed')
RATES = (30,60,120,144)
COLORS = {30:'#cf6631',60:'#146cb8',120:'#21955b',144:'#9751af'}


def collect():
    library = ROOT/'build/native-box2d/libbox2d.a'
    if not library.exists():
        raise SystemExit('Missing native Box2D archive: run bash tools/test.sh first')
    executable = OUTPUT/'physics_trace'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra',str(ROOT/'tools/physics_trace.cpp'),
                    str(ROOT/'src/cat_physics.cpp'),'-I'+str(ROOT/'third_party/box2d/include'),
                    str(library),'-lm','-o',str(executable)],check=True)
    path=OUTPUT/'controller-traces.csv'
    with path.open('w') as output:
        subprocess.run([str(executable)],stdout=output,check=True)
    grouped={(scene,fps):[] for scene in SCENES for fps in RATES}
    with path.open() as source:
        for row in csv.DictReader(source):
            scene=row.pop('scenario');fps=int(row.pop('fps'))
            grouped[(scene,fps)].append({key:float(value) for key,value in row.items()})
    return grouped


def plot(rows):
    fig,axes=plt.subplots(3,3,figsize=(15,11),constrained_layout=True)
    for column,scene in enumerate(SCENES):
        for fps in RATES:
            samples=rows[(scene,fps)]
            times=[x['time'] for x in samples]
            axes[0,column].plot([x['x'] for x in samples],[x['lift'] for x in samples],color=COLORS[fps],label=f'{fps} fps',alpha=.85)
            axes[1,column].plot(times,[x['lift'] for x in samples],color=COLORS[fps],alpha=.85)
            axes[2,column].plot(times,[x['vy'] for x in samples],color=COLORS[fps],alpha=.85)
        samples=rows[(scene,60)]
        for sample in samples:
            if sample['landed']:
                axes[0,column].scatter([sample['x']],[sample['lift']],marker='x',s=70,color='black',zorder=6)
                axes[1,column].axvline(sample['time'],color='#8b9bad',linestyle=':',linewidth=.8)
        axes[0,column].plot([450,750],[220,220],color='#777777',linewidth=4,label='initial ledge')
        axes[0,column].axhline(0,color='#444444',linewidth=1)
        axes[0,column].set(title=f'{scene}: actual contact path',xlabel='x (logical px)',ylabel='feet above taskbar (px)',xlim=(0,1000),ylim=(-10,400))
        axes[1,column].set(xlabel='elapsed seconds',ylabel='lift (px)',title='height and landing times')
        axes[2,column].set(xlabel='elapsed seconds',ylabel='vertical velocity (px/s)',title='vertical velocity from Box2D')
        for row in range(3):axes[row,column].grid(alpha=.2)
    axes[0,0].legend(loc='upper left',fontsize=8)
    fig.suptitle('Production Controller diagnostics — real Box2D step/contact outputs; X marks landings',fontsize=13)
    fig.savefig(OUTPUT/'controller-paths.png',dpi=150)
    fig.savefig(OUTPUT/'controller-paths.svg')
    plt.close(fig)


def summarize(rows):
    result={'engine':'Production cat::physics::Controller with pinned Box2D','scenarios':{},
            'note':'FPS comparisons use common 1/6-second sample times; generated paths are not fitted curves.'}
    for scene in SCENES:
        variants={}
        baseline=rows[(scene,60)]
        for fps in RATES:
            samples=rows[(scene,fps)]
            comparisons=[]
            for k in range(31):
                sample=samples[round(k*fps/6)]
                reference=baseline[k*10]
                comparisons.append(max(abs(sample['x']-reference['x']),abs(sample['lift']-reference['lift'])))
            variants[str(fps)]={'peak_lift':max(x['lift'] for x in samples),
                'final_x':samples[-1]['x'],'final_lift':samples[-1]['lift'],
                'final_supported_on_ledge':bool(samples[-1]['on_platform']),
                'landings':[{'time':x['time'],'x':x['x'],'lift':x['lift'],'on_platform':bool(x['on_platform'])} for x in samples if x['landed']],
                'max_common_sample_deviation_from_60fps':max(comparisons)}
        result['scenarios'][scene]=variants
    files=['src/cat_physics.cpp','src/cat_physics.hpp','tools/physics_trace.cpp',
           'tools/render_physics_trace.py','build/native-box2d/libbox2d.a']
    result['sources_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}
    (OUTPUT/'controller-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def animate(rows):
    panel_width,panel_height=660,350
    scale=.6
    ground=292
    frames=[]
    for frame in range(0,301,3): # 20fps display of each 60Hz trace
        image=Image.new('RGB',(panel_width*3,panel_height),(242,245,248))
        draw=ImageDraw.Draw(image)
        for column,scene in enumerate(SCENES):
            offset=column*panel_width
            sample=rows[(scene,60)][frame]
            def point(x,y):return offset+30+x*scale,ground-y*scale
            draw.text((offset+15,12),f'{scene} | t={sample["time"]:.2f}s | actual Box2D',fill='#1f3344')
            draw.line((offset+15,ground,offset+panel_width-15,ground),fill='#697887',width=2)
            if sample['ledge_visible']:
                x0,y0=point(sample['ledge_left'],sample['ledge_height'])
                x1,_=point(sample['ledge_right'],sample['ledge_height'])
                draw.rectangle((x0,y0,x1,y0+6),fill='#7f8e9b')
            elif scene=='closed':
                draw.text((offset+270,140),'window removed',fill='#a65f43')
            trail=[point(s['x'],s['lift']) for s in rows[(scene,60)][:frame+1]]
            if len(trail)>1:draw.line(trail,fill='#81b2d8',width=2)
            x,y=point(sample['x'],sample['lift'])
            color='#279260' if sample['on_platform'] else '#377bb5' if not sample['grounded'] else '#d18a3d'
            draw.ellipse((x-8,y-16,x+8,y),fill=color,outline='#203f53',width=1)
            draw.text((offset+15,312),f'lift={sample["lift"]:.2f}  vy={sample["vy"]:.2f}  supported={int(sample["on_platform"])}',fill='#263d4f')
            if sample['landed']:draw.text((offset+490,40),'CONTACT',fill='#237949')
        frames.append(image)
    frames[0].save(OUTPUT/'controller-motion.gif',save_all=True,append_images=frames[1:],duration=50,loop=0,disposal=2)


def main():
    rows=collect()
    result=summarize(rows)
    plot(rows)
    animate(rows)
    print(OUTPUT/'controller-paths.png')
    print(OUTPUT/'controller-motion.gif')
    for scene,variants in result['scenarios'].items():
        print(scene,'max FPS deviation:',max(v['max_common_sample_deviation_from_60fps'] for v in variants.values()))


if __name__=='__main__':main()
