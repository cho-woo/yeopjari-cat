#!/usr/bin/env python3
"""Compare v1.9 painted walking, v2.0.5 procedural walking, and working restoration.

Read-only artwork and production C++ evaluators; no invented intermediate poses.
Pillow implements final placement, so these are portable previews, not a Windows
GDI+ or Box2D integration test. All three columns travel at 22 logical pixels/second.
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tarfile

from PIL import Image, ImageDraw
from render_motion_preview import atlas
from render_walk_review import save_gif

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT/'build-inputs/v1.9.0/yeopjari-src-1.9.0.tar.gz'
RELEASE = '87ec4ec62d70b621ed51d89e4c6cc4ab93087a66'


def compile_bridges(output):
    archived = output/'v190'; archived.mkdir(exist_ok=True)
    current = output/'v205'; current.mkdir(exist_ok=True)
    with tarfile.open(BASELINE) as bundle:
        for member in bundle.getmembers():
            path = Path(member.name)
            if path.parent == Path('src') and path.suffix == '.hpp':
                (archived/path.name).write_bytes(bundle.extractfile(member).read())
    # The v1.9 source archive references externally supplied art. Its recovery
    # commit tracks those original PNGs; reject any later art replacement.
    for name in ('cat.png', 'cat-silver.png'):
        if subprocess.check_output(['git','show',f'bfd204d:assets/{name}'],cwd=ROOT)!=(ROOT/'assets'/name).read_bytes():
            raise RuntimeError('Recovered and current artwork differ: '+name)
    paths = subprocess.check_output(['git','ls-tree','-r','--name-only',RELEASE,'src'],cwd=ROOT,text=True).splitlines()
    for path in paths:
        if Path(path).suffix == '.hpp':
            (current/Path(path).name).write_bytes(subprocess.check_output(['git','show',f'{RELEASE}:{path}'],cwd=ROOT))
    old_source = output/'archived-bridge.cpp'
    old_source.write_text(r'''
#include "cat_animation.hpp"
struct Review { cat::animation::Locomotion gait; double elapsed=0; };
extern "C" void* original_create() { return new Review; }
extern "C" void original_destroy(void* p) { delete static_cast<Review*>(p); }
extern "C" int original_step(void* p,double distance,double dt,double* output) {
    auto& review=*static_cast<Review*>(p);
    if(!review.gait.Advance(cat::animation::Motion::Walk,distance,dt))return 0;
    review.elapsed+=dt;
    const auto ms=static_cast<std::uint64_t>(review.elapsed*1000);
    const auto pose=cat::animation::Evaluate(cat::animation::Motion::Walk,ms,500000+ms,1,review.gait);
    output[0]=pose.frame;output[1]=pose.blendFrame;output[2]=pose.blend;
    output[3]=pose.x;output[4]=pose.lift;output[5]=pose.scaleX;
    output[6]=pose.scaleY;output[7]=pose.angle;
#ifdef ORIGINAL_WALK_CANDIDATE
    output[8]=review.gait.OriginalWalkPhase();
#else
    output[8]=review.gait.Phase();
#endif
    output[9]=review.gait.Speed();return 1;
}
''')
    new_source = output/'released-bridge.cpp'
    new_source.write_bytes(subprocess.check_output(['git','show',f'{RELEASE}:tools/gait_review.cpp'],cwd=ROOT))
    libraries = []
    candidate=output/'candidate'; candidate.mkdir(exist_ok=True)
    for header in (ROOT/'src').glob('*.hpp'): (candidate/header.name).write_bytes(header.read_bytes())
    for name,source,include,extra in [('original',old_source,archived,[]),('released',new_source,current,['-DGAIT_REVIEW_TRACKED_FEET']),
                                     ('candidate',old_source,candidate,['-DORIGINAL_WALK_CANDIDATE'])]:
        path = output/(name+'.so')
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        '-I'+str(include),*extra,str(source),'-o',str(path)],check=True)
        libraries.append(ctypes.CDLL(str(path)))
    old,new,working = libraries
    p,d,i,ds = ctypes.c_void_p,ctypes.c_double,ctypes.c_int,ctypes.POINTER(ctypes.c_double)
    for lib in (old,working):
        lib.original_create.restype = p
        lib.original_destroy.argtypes = [p]
        lib.original_step.argtypes = [p,d,d,ds]
        lib.original_step.restype = i
    for name,args,result in [('create',[p,i,i,i],p),('destroy',[p],None),('step',[p,i,d,d,d,d,ds],i),
                            ('pixels',[p],p),('width',[p],i),('height',[p],i),('inset',[p],d),('offset',[p],d)]:
        function = getattr(new,'gait_review_'+name); function.argtypes=args; function.restype=result
    return old,new,working


def archived_layer(frames, scale, pose, center, ground, size, zoom):
    def draw(frame):
        source = frames[int(frame)].convert('RGBa')
        width,height = source.width*scale*pose[5]*zoom,source.height*scale*pose[6]*zoom
        angle = math.radians(pose[7]); cosine,sine = math.cos(angle),math.sin(angle)
        x = (center+pose[3])*zoom
        y = (ground-pose[4])*zoom-abs(sine)*width/2
        ax,bx = cosine*source.width/width,sine*source.width/width
        ay,by = -sine*source.height/height,cosine*source.height/height
        return source.transform(size,Image.Transform.AFFINE,
                                (ax,bx,source.width/2-ax*x-bx*y,ay,by,source.height-ay*x-by*y),
                                Image.Resampling.BICUBIC)
    first = draw(pose[0])
    if pose[1]>=0 and pose[2]>0:
        weight = math.floor(min(1,max(0,pose[2]))*255+.5)/255
        first = Image.blend(first,draw(pose[1]),weight)
    return first.convert('RGBA')


def earliest_pose(time):
    """v1.3 released binary's recovered Walk evaluator; source was not tracked.

    floor(elapsed/145)%4, SmoothStep on the entire frame, plus bounded lift.
    This is an explicitly reconstructed formula, not recovered C++ source.
    """
    elapsed=(2+time)*1000
    frame=int(elapsed//145)%4
    fraction=(elapsed%145)/145
    blend=fraction*fraction*(3-2*fraction)
    lift=.38*(math.sin((500000+elapsed)/700)+1)+.8*abs(math.sin(elapsed/145*math.pi))
    return [frame,(frame+1)%4,blend,0,lift,1,1,0,(elapsed/580)%1,22]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/original-walk-review')
    args=parser.parse_args(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    old,new,working=compile_bridges(output)
    sheets=[atlas(name)[0] for name in ('cat.png','cat-silver.png')]
    scales=[110/max(frame.height for frame in frames[:6]) for frames in sheets]
    originals=[]; candidates=[]; rigs=[]; samples=[]; speed=22.; dt=.04; count=80
    for character,frames in enumerate(sheets):
        originals.append(old.original_create())
        candidates.append(working.original_create())
        source=frames[1]; pixels=source.convert('RGBa').tobytes('raw','BGRa')
        memory=ctypes.create_string_buffer(pixels,len(pixels))
        rig=new.gait_review_create(memory,source.width,source.height,character)
        if not rig: raise RuntimeError('Could not prepare released rig')
        rigs.append(rig)

    def capture(character,delta):
        source=sheets[character][1]; scale=scales[character]
        pose=(ctypes.c_double*10)(); candidate=(ctypes.c_double*10)(); parameters=(ctypes.c_double*5)()
        if not old.original_step(originals[character],speed*delta,delta,pose): raise RuntimeError('Archived evaluator failed')
        if not working.original_step(candidates[character],speed*delta,delta,candidate): raise RuntimeError('Candidate evaluator failed')
        if not new.gait_review_step(rigs[character],0,speed*delta,delta,source.width*scale,source.height*scale,parameters):
            raise RuntimeError('Released evaluator failed')
        width,height=new.gait_review_width(rigs[character]),new.gait_review_height(rigs[character])
        raw=ctypes.string_at(new.gait_review_pixels(rigs[character]),width*height*4)
        image=Image.frombytes('RGBa',(width,height),raw,'raw','BGRa')
        inset=new.gait_review_inset(rigs[character])*scale+new.gait_review_offset(rigs[character])
        return list(pose),image,inset,list(parameters),list(candidate)

    def panel(character,state,version,time,zoom=1,moving=True):
        pose,image,inset,parameters,candidate=state
        size=(280*zoom,190*zoom); ground=155; center=105+(speed*time if moving else 0)
        background=Image.new('RGBA',size,'#edf0f4'); draw=ImageDraw.Draw(background)
        for x in range(-100,400,10):
            gx=(x-(0 if moving else speed*time))*zoom
            draw.line((gx,ground*zoom,gx,(ground+4)*zoom),fill='#9aa8b8')
        draw.line((0,ground*zoom,size[0],ground*zoom),fill='#6a7888')
        if version in (0,2,3):
            displayed=pose if version==0 else candidate if version==2 else earliest_pose(time)
            layer=archived_layer(sheets[character],scales[character],displayed,center,ground,size,zoom)
        else:
            scale=scales[character]*zoom
            # Match DrawWalkingPet: raster width, original source height.
            width=image.width*scale; height=sheets[character][1].height*scale
            x=center*zoom-width/2; y=(ground+inset)*zoom-height
            ax=image.width/width; by=image.height/height
            layer=image.transform(size,Image.Transform.AFFINE,(ax,0,-x*ax,0,by,-y*by),Image.Resampling.BICUBIC).convert('RGBA')
        background.alpha_composite(layer)
        label=('v1.9 ORIGINAL 4 PAINTED POSES','v2.0.5 SINGLE-POSE PAW WARP','CANDIDATE ORIGINAL, GROUNDED','v1.3 RECOVERED BINARY TIMING')[version]
        draw.text((5*zoom,5*zoom),('Cream','Silver')[character]+' | '+label,fill='#24394d')
        detail=(f'frame {int(displayed[0])} > {int(displayed[1])}, blend {displayed[2]:.2f}' if version in (0,2,3) else f'phase {parameters[3]:.2f}')
        draw.text((5*zoom,174*zoom),f'{time:.2f}s | 22px/s | '+detail,fill='#24394d')
        return background.convert('RGB')

    try:
        # Start with settled speed, retaining each released cycle's own phase.
        for _ in range(50):
            for character in range(2): capture(character,dt)
        moving=[]; tracking=[]; stills=[]; earliest=[]
        for index in range(count):
            time=index*dt; board=Image.new('RGB',(840,380)); tracked=Image.new('RGB',(1680,760)); first_versions=Image.new('RGB',(840,380))
            states=[]
            for character in range(2):
                state=capture(character,dt if index else 0); states.append(state)
                samples.append({'time':time,'character':character,'archived_pose':state[0],'released_gait':state[3],'candidate_pose':state[4]})
                for version in range(3):
                    board.paste(panel(character,state,version,time),(version*280,character*190))
                    tracked.paste(panel(character,state,version,time,zoom=2,moving=False),(version*560,character*380))
                for column,version in enumerate((3,0,2)):
                    first_versions.paste(panel(character,state,version,time),(column*280,character*190))
            moving.append(board); tracking.append(tracked); earliest.append(first_versions)
            if index in range(0,32,2): stills.append((time,states))
        save_gif(moving,output/'original-v190-v205-real-time.gif',40)
        save_gif(tracking,output/'original-v190-v205-closeup.gif',40)
        save_gif(tracking,output/'original-v190-v205-slow-4x.gif',160)
        save_gif(earliest,output/'earliest-v13-v19-candidate-real-time.gif',40)
        for character in range(2):
            contact=Image.new('RGB',(1120,720),'#edf0f4')
            for index in range(4):
                pose=[index,-1,0,0,0,1,1,0,0,22]
                frame=Image.new('RGBA',(560,360),'#edf0f4')
                frame.alpha_composite(archived_layer(sheets[character],scales[character],pose,140,155,frame.size,2))
                ImageDraw.Draw(frame).text((12,12),f'Original painted frame {index}',fill='#24394d')
                contact.paste(frame.convert('RGB'),((index%2)*560,(index//2)*360))
            contact.save(output/(('cream','silver')[character]+'-original-four-poses.png'))
            feet=Image.new('RGB',(4*300,12*140),'#edf0f4')
            for version in range(3):
                for index,(time,states) in enumerate(stills):
                    crop=panel(character,states[character],version,time,zoom=3,moving=False).crop((165,345,465,485))
                    ImageDraw.Draw(crop).text((4,4),('v1.9','v2.0.5','candidate')[version]+f' {time:.2f}s',fill='#24394d')
                    feet.paste(crop,((index%4)*300,(version*4+index//4)*140))
            feet.save(output/(('cream','silver')[character]+'-feet-comparison.png'))
            full=Image.new('RGB',(4*240,6*250),'#edf0f4')
            for row,version in enumerate((3,0,2)):
                for index,(time,states) in enumerate(stills[:8]):
                    crop=panel(character,states[character],version,time,zoom=2,moving=False).crop((90,65,330,315))
                    ImageDraw.Draw(crop).text((4,4),('v1.3' if version==3 else 'v1.9' if version==0 else 'candidate')+f' {time:.2f}s',fill='#24394d')
                    full.paste(crop,((index%4)*240,(row*2+index//4)*250))
            full.save(output/(('cream','silver')[character]+'-earliest-full-sequence.png'))
        proof={'archive_sha256':hashlib.sha256(BASELINE.read_bytes()).hexdigest(),
               'released_source_commit':RELEASE,'speed_px_s':speed,'tick_seconds':dt,
               'candidate_sha256':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in (output/'candidate').glob('*.hpp')},
               'original_cycle_px':14,'original_cycle_seconds':14/speed,
               'original_frame_hold_fraction':.6,'original_blend':'SmoothStep final 40% of each frame',
               'v13_reconstruction':'Released binary recovered evaluator: 145ms/frame, full-frame SmoothStep, bounded lift; not archived C++ source',
               'normalization':'110/max(first six cropped source heights), shared scale; alpha>16 +2px crop',
               'limits':'Portable C++ evaluators + Pillow placement; not Windows/GDI+ or live Box2D/window execution',
               'art_sha256':{name:hashlib.sha256((ROOT/'assets'/name).read_bytes()).hexdigest() for name in ('cat.png','cat-silver.png')},
               'samples':samples}
        (output/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(output)
    finally:
        for instance in originals: old.original_destroy(instance)
        for instance in candidates: working.original_destroy(instance)
        for instance in rigs: new.gait_review_destroy(instance)


if __name__=='__main__': main()
