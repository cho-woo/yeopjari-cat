"""Measure SurfaceFlinger display intervals during each authored phase of a live-wallpaper trial."""
import argparse
import json
import math
from pathlib import Path
from analyze_android_cold_jump import query

def analyze(tool,path):
    trial=json.loads(path.read_text())
    rows=query(tool,path.with_suffix('.perfetto-trace'),"""SELECT ts,dur,name FROM slice
      WHERE name GLOB 'CatWallpaperPose/*' AND dur>0 ORDER BY ts""")
    spans=[]
    for row in rows:
        _,action,frame,x,lift=row['name'].split('/')
        start=int(row['ts'])
        if not spans or spans[-1]['action']!=action:
            spans.append(dict(action=action,begin=start,end=start,airborne=False))
        spans[-1]['end']=start+int(row['dur'])
        spans[-1]['airborne'] |= int(lift)>0
    index=next(i for i,s in enumerate(spans) if i and s['action']=='cursor-pounce'
               and s['airborne'] and spans[i-1]['action']=='run')
    assert [s['action'] for s in spans[index-1:index+3]]==['run','cursor-pounce','punch','walk'],path
    # Column 1 of --latency is the buffer's requested timestamp, column 2
    # is the actual presentation timestamp. Scope the buffers by the recorded
    # draw-span boundaries; the next presentation also covers the transition.
    shown=sorted(trial['presentations'],key=lambda t:t[1])
    phases={}
    for s in spans[index-1:index+3]:
        gaps=[(b[1]-a[1])/1e6 for a,b in zip(shown,shown[1:]) if s['begin']<=a[0]<=s['end']]
        assert len(gaps)>=10,(path,s,len(gaps))
        gaps.sort()
        phases[s['action']]={'presented_intervals':len(gaps),'max_ms':round(max(gaps),2),
                             'p95_ms':round(gaps[math.ceil(.95*len(gaps))-1],2),'over_100':sum(v>100 for v in gaps)}
    return {'trial':path.stem,'coat':trial['coat'],'pid':trial['pid'],'system_wallpaper_layer':trial['layer'],
            'force_stopped':trial['force_stopped'],'states':trial['states'],'phases':phases}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--processor',type=Path,required=True);p.add_argument('--dir',type=Path,required=True)
    a=p.parse_args()
    data=[analyze(a.processor,f) for f in sorted(a.dir.glob('*.json')) if f.with_suffix('.perfetto-trace').is_file()]
    assert data and len(set(x['pid'] for x in data))==len(data)
    summary=[]
    for coat in sorted(set(x['coat'] for x in data)):
        rows=[x for x in data if x['coat']==coat]
        summary.append(dict(coat=coat,trials=len(rows),worst_phase_ms={phase:max(x['phases'][phase]['max_ms'] for x in rows) for phase in rows[0]['phases']}))
    (a.dir/'display-report.json').write_text(json.dumps(dict(method='Android SurfaceFlinger --latency actual presentations on the selected wallpaper layer, within app-traced action windows, one failed-to-stop or missing pursuit invalidates the test.',summary=summary,trials=data),indent=2))
    print(json.dumps(summary))
if __name__=='__main__':main()
