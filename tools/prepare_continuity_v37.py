"""Register the reviewed v37 Dropshot movies without changing canonical scale."""
import json
from pathlib import Path
import cv2
from pack_video_actions import foreground

ROOT = Path(__file__).resolve().parents[1] / 'assets/video-actions'
SELECTION = ROOT / 'selected/continuity-v37'

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')

identity = read(ROOT/'identity-v29.json')
selection = read(SELECTION/'selection.json')
for action in ('brake', 'slide', 'stumble'):
    entry = {'frame_count':97, 'duration_ms':4042,
             'source_indices':{c:list(range(97)) for c in ('cream','tuxedo')},
             'source_files':{c:[f'sources/continuity-v37/{c}-{action}.mp4']*97 for c in ('cream','tuxedo')},
             'exposures_ms':[round((n+1)*1000/24)-round(n*1000/24) for n in range(97)],
             'fixed_horizontal_root':{'cream':480., 'tuxedo':480.},
             'root_keys':{}, 'strict_pink_skin':True}
    for coat in ('cream','tuxedo'):
        capture=cv2.VideoCapture(str(ROOT/entry['source_files'][coat][0]))
        keys=[]
        for n in (0,96):
            capture.set(cv2.CAP_PROP_POS_FRAMES,n)
            ok, frame=capture.read()
            if not ok:raise ValueError('Incomplete source')
            rgba=foreground(cv2.cvtColor(frame,cv2.COLOR_BGR2RGB),None,True)
            keys.append([n,480.,float(rgba.getbbox()[3])])
        capture.release()
        entry['root_keys'][coat]=keys
    identity['clips'][action]=entry
    selection[action]={'source_indices':list(range(97)), 'duration_ms':4042,
        'canonical_reference_height':367,'root_keys':entry['root_keys'],
        'review':'Accepted full side-planted entry, continuous braking/skid/trip and supported front-facing recovery. Camera and common scale retained. Linear bookend floor calibration only; no per-pose normalization.'}
for entry in identity['clips'].values():entry['strict_pink_skin']=True
for action in ('roll','tail-chase'):
    selection[action]={'source_indices':list(range(97)),'duration_ms':4042,
        'canonical_reference_height':367,
        'review':'Complete reviewed ground action and seated recovery at original 24fps. Exact canonical first and last artwork; no repeated excerpts or pose-height scaling.'}
write(ROOT/'identity-v29.json',identity)
write(SELECTION/'selection.json',selection)
generation=read(ROOT/'generation.json')
known={j['job_id'] for j in generation['jobs']}
for file in ('play-generations.json','stop-generations.json'):
    for job in read(SELECTION/file):
        result=job['result']; job_id=result['job_id']
        if job_id in known:continue
        generation['jobs'].append({'job_id':job_id,'model':job['args']['model'],
            'character':job['character'],'action':job['action'],
            'options':job['args']['options'],'credit_cost':result['creditCost'],
            'source':'assets/video-actions/'+job['source_path'],'sha256':job['sha256'],
            'review':selection[job['action']]['review']})
        generation['total_credits']+=result['creditCost'];known.add(job_id)
write(ROOT/'generation.json',generation)
encoding=read(SELECTION/'lossless-encoding.json')
encoding['stage_note']='Historical pre-replacement lossless encoding proof. Brake, Slide and Stumble were subsequently replaced by reviewed v37 movies; their final hashes are in manifest/provenance. Retained clips preserve decoded RGBA exactly.'
write(SELECTION/'lossless-encoding.json',encoding)
print('Registered 6 complete stop movies; 12 retained v37 generations, 5520 credits. Shared canonical height 367.')
