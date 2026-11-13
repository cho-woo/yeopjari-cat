"""Register new Dropshot gait boundaries and the retained gallop seam distance clock."""
from pathlib import Path
import hashlib
import json
import sys
import cv2
import numpy as np
from PIL import Image
import pack_video_actions as pack

ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'assets/video-actions'
SELECTION=D/'selected/run-v36'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def values(image):
    a=np.asarray(image).astype(np.float32)/255
    a[:,:,:3]*=a[:,:,3:4]
    return a
def main(jobs_path):
    history=D/'history/v36-before-run';history.mkdir(parents=True,exist_ok=True)
    if (history/'identity-v29.json').exists():raise ValueError('Registration already prepared; do not transform clocks twice.')
    for name in ('identity-v29.json','manifest.json','registration.json'):
        (history/name).write_bytes((D/name).read_bytes())
    identity=json.loads((D/'identity-v29.json').read_text());manifest=json.loads((D/'manifest.json').read_text())
    entries={e['clip']:e for e in manifest['clips']}
    config=identity['clips']['run'];old=json.loads(json.dumps(config));count=old['loop_frames']
    assert count==106
    SELECTION.mkdir(parents=True,exist_ok=True)
    jobs=json.loads(Path(jobs_path).read_text())
    write(SELECTION/'generations.json',jobs)
    generation=json.loads((D/'generation.json').read_text())
    assert isinstance(generation['jobs'],list)
    def cells(coat,clip):
        atlas=Image.open(D/f'{coat}-{clip}.png');result=[]
        for f in entries[clip]['frames']:
            x,y,w,h=f['rect'];ax,ay=f['anchor'];cell=Image.new('RGBA',(320,320))
            cell.paste(atlas.crop((x,y,x+w,y+h)),(160-ax,240-ay));result.append(cell)
        return result
    weight=np.ones((320,320,1),np.float32);weight[:195]*=.3;weight[207:247]*=4
    evidence={};config['distance_knots']={};config['source_registration']={}
    for coat in ('cream','tuxedo'):
        job=next(j for j in jobs if j['character']==coat)
        assert job['poll']['status']=='succeeded'
        original=D/f'sources/run-v36/{coat}-responsive-run.mp4'
        derived=D/f'sources/run-v36/{coat}-responsive-run-768.avi'
        cap=cv2.VideoCapture(str(original));assert cap.get(cv2.CAP_PROP_FPS)==24
        frames=[];writer=cv2.VideoWriter(str(derived),cv2.VideoWriter_fourcc(*'FFV1'),24,(768,768))
        assert writer.isOpened()
        raw_hash=hashlib.sha256()
        while True:
            ok,bgr=cap.read()
            if not ok:break
            resized=cv2.resize(bgr,(768,768),interpolation=cv2.INTER_LANCZOS4)
            raw_hash.update(resized.tobytes());writer.write(resized);frames.append(cv2.cvtColor(resized,cv2.COLOR_BGR2RGB))
        writer.release();cap.release();assert len(frames)==97
        check=cv2.VideoCapture(str(derived));decoded_hash=hashlib.sha256()
        while True:
            ok,bgr=check.read()
            if not ok:break
            decoded_hash.update(bgr.tobytes())
        check.release();assert decoded_hash.digest()==raw_hash.digest(),'Derivative must be lossless after uniform canvas resize.'
        floor=float(pack.foreground(frames[96]).getbbox()[3])
        source=derived.relative_to(D).as_posix()
        config['source_registration'][source]={'fixed_horizontal_root':384,'fixed_ground_root':floor}
        loop_run=cells(coat,'run')[:count];loop_walk=cells(coat,'walk')[:len(entries['walk']['sequence']['loop'])]
        changes=np.array([float(np.abs(values(loop_run[(i+1)%count])-values(loop_run[i])).mean()) for i in range(count)])
        threshold=float(np.median(changes)*.4)
        weights=np.ones(count)
        for i in list(range(8))+list(range(count-8,count)):
            weights[i]=max(.1,min(1.,changes[i]/threshold))
        knots=np.concatenate(([0.],np.cumsum(weights)/weights.sum()));knots[-1]=1
        config['distance_knots'][coat]=knots.tolist()
        factor=96/(367*.75)
        def registered(index):
            image=pack.foreground(frames[index]);root=max(floor,image.getbbox()[3])
            image=image.resize((round(768*factor),round(768*factor)),Image.Resampling.LANCZOS)
            cell=Image.new('RGBA',(320,320));cell.paste(image,(round(160-384*factor),round(240-root*factor)))
            return cell
        intro=list(range(21 if coat=='cream' else 25,33 if coat=='cream' else 37))
        outro=list(range(84,96));assert len(intro)==len(outro)==12
        arrival={};scores={}
        for mode,target,options in [('run',registered(intro[-1]),loop_run),('walk',registered(outro[-1]),loop_walk)]:
            a=values(target);errors=[float(np.mean((a-values(b))**2*weight)) for b in options]
            index=int(np.argmin(errors));phase=index/len(options)
            arrival[mode]=float(np.interp(phase,np.linspace(0,1,count+1),knots)) if mode=='run' else phase
            scores[mode]={'source_frame':index,'registered_rgba_mse':min(errors)}
        config['transition_arrival'][coat]=arrival
        previous_turn=identity['clips']['turn']['arrival_phase'][coat]['run']
        identity['clips']['turn']['arrival_phase'][coat]['run']=float(np.interp(previous_turn,np.linspace(0,1,count+1),knots))
        config['source_indices'][coat]=old['source_indices'][coat][:count]+intro+outro
        config['source_files'][coat]=old['source_files'][coat][:count]+[source]*24
        for label,indices in [('acceleration',intro),('deceleration',outro)]:
            for index in (indices[0],indices[-1]):registered(index).save(SELECTION/f'{coat}-{label}-{index}.png')
        evidence[coat]={'original':original.relative_to(D).as_posix(),'original_sha256':sha(original),
            'derivative':source,'derivative_sha256':sha(derived),'uniform_canvas_scale':.8,'derived_pixel_sha256':raw_hash.hexdigest(),
            'source_fps':24,'frame_count':97,'acceleration_source_frames':intro,'deceleration_source_frames':outro,
            'floor':floor,'unchanged_loop_rgba_sha256':[hashlib.sha256(np.asarray(im).tobytes()).hexdigest() for im in loop_run],
            'loop_source_pose_change':changes.tolist(),'seam_weights':weights.tolist(),'distance_knots':knots.tolist(),
            'arrival':scores,'loop_stride_pixels_unchanged':old['stride_pixels']}
        generation['jobs'].append({'character':coat,'action':'responsive-gait-v36','model':job['args']['model'],
            'job_id':job['result']['job_id'],'credit_cost':job['result']['creditCost'],'status':'succeeded','accepted':True,
            'source_file':original.relative_to(D).as_posix(),'source_sha256':sha(original),
            'video_url':job['poll']['outputList'][0]['videoUrl'],'options':job['args']['options'],
            'selection':'Complete12-frame acceleration/deceleration boundaries; unchanged106-frame calibrated gallop retained.'})
    config['frame_count']=count+24;config['intro_frames']=12
    config['strict_pink_skin']=True
    config['exposures_ms']=old['exposures_ms'][:count]+[round((i+1)*1000/24)-round(i*1000/24) for i in range(12)]*2
    config['duration_ms']=sum(config['exposures_ms'])
    config['transition_audit_v36']='New canonical-conditioned complete500ms transitions. Original106-frame gallop and523px measured stride preserved; positive seam-distance weights remove near-static film bookends without deleting any drawing.'
    write(D/'identity-v29.json',identity);write(D/'generation.json',generation);write(SELECTION/'selection.json',evidence)
    print('Registered complete500ms transitions and all106 original gallop drawings for both coats.')
if __name__=='__main__':main(sys.argv[1])
