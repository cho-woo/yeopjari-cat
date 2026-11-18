"""Join draw markers to the cat's submitted surface frame and the display's actual presentation."""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import subprocess

def query(tool, trace, sql):
    result=subprocess.run([str(tool),'-Q',sql,str(trace)],capture_output=True,text=True,check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))

def analyze(tool,path):
    data=json.loads(path.read_text())
    trace=path.with_suffix('.perfetto-trace')
    rows=data['draws']
    sql=f"""
    WITH markers AS (
      SELECT CAST(substr(s.name,14) AS INTEGER) n, CAST(substr(a.name,23) AS INTEGER) token
      FROM slice s, ancestor_slice(s.id) a
      WHERE s.name GLOB 'ColdJumpDraw [0-9]*' AND a.name GLOB 'Choreographer#doFrame [0-9]*'
    ), surfaces AS (
      SELECT m.n, f.layer_name, f.display_frame_token, f.jank_type
      FROM markers m JOIN actual_frame_timeline_slice f ON f.surface_frame_token=m.token
      JOIN process p USING(upid) WHERE p.pid={int(data['pid'])} AND f.dur>0
        AND f.layer_name IS NOT NULL AND f.present_type!='Dropped Frame'
    ), chosen AS (SELECT layer_name FROM surfaces GROUP BY 1 ORDER BY count(DISTINCT n) DESC LIMIT 1)
    SELECT MAX(n) n, (d.ts+d.dur) presented_ns, s.jank_type app_jank, d.jank_type display_jank
    FROM surfaces s JOIN chosen USING(layer_name) JOIN actual_frame_timeline_slice d
      ON d.display_frame_token=s.display_frame_token AND d.layer_name IS NULL AND d.dur>0
    GROUP BY d.display_frame_token ORDER BY presented_ns
    """
    displayed=query(tool,trace,sql)
    assert len(displayed)>len(rows)*.65,(path,len(displayed),len(rows))
    costs=query(tool,trace,f"""SELECT s.ts,s.dur,s.name FROM slice s JOIN thread_track tr ON tr.id=s.track_id
      JOIN thread t USING(utid) WHERE t.tid={int(data['pid'])} AND s.name LIKE '%relayoutWindow%' AND s.dur>0 ORDER BY s.ts""")
    # Include the boundary on each side, even if one intermediate draw was dropped.
    gaps=[]
    for a,b in zip(displayed,displayed[1:]):
        left,right=rows[int(a['n'])],rows[int(b['n'])]
        if 'cursor-pounce' not in (left[0],right[0]):continue
        start,end=int(a['presented_ns']),int(b['presented_ns'])
        layouts=[r for r in costs if int(r['ts'])<end and int(r['ts'])+int(r['dur'])>start]
        gaps.append(dict(ms=round((end-start)/1e6,2),from_action=left[0],to_action=right[0],
                    from_frame=left[4],to_frame=right[4],from_size=left[6:8],to_size=right[6:8],
                    app_jank=b['app_jank'],display_jank=b['display_jank'],
                    layout_ms=round(sum(int(r['dur']) for r in layouts)/1e6,2)))
    assert len(gaps)>=20 and any(rows[int(r['n'])][5]>0 and rows[int(r['n'])][0]=='cursor-pounce' for r in displayed),path
    ordered=sorted(gaps,key=lambda g:g['ms'])
    boundary_resizes=sum(j[6:8]!=i[6:8] for i,j in zip(rows,rows[1:]) if 'cursor-pounce' in (i[0],j[0]))
    return {'trial':path.stem,'version':data['version'],'coat':data['coat'],'pid':data['pid'],
            'cold_start':True,'ordered_landed_and_recovered':data['ordered_landed_and_recovered'],
            'runner_to_touch_ms':round((data['down_boottime_ns']-data['runner_boottime_ns'])/1e6),
            'observed_draws':len(rows),'matched_presented':len(displayed),'jump_intervals':len(gaps),
            'p95_ms':ordered[math.ceil(.95*len(ordered))-1]['ms'],'worst':ordered[-1],
            'over_100':sum(i['ms']>100 for i in gaps),'over_200':sum(i['ms']>200 for i in gaps),
            'render_view_size_changes_at_or_during_pounce':boundary_resizes,'top_gaps':ordered[-3:][::-1]}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--processor',required=True,type=Path)
    p.add_argument('--dir',required=True,type=Path)
    a=p.parse_args()
    out=[analyze(a.processor,f) for f in sorted(a.dir.glob('c*-r*.json'))]
    assert out
    (a.dir/'presentation.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
    for r in out:print(r['trial'],r['p95_ms'],r['worst']['ms'],r['over_200'],r['render_view_size_changes_at_or_during_pounce'])
if __name__=='__main__':
    main()
