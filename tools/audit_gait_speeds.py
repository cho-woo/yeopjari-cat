#!/usr/bin/env python3
"""Replay actual gait/rig at desktop speeds, nutrition shapes and age scales.

Uses the production C++ bridge from render_walk_run_audit. The reflected world
contact check covers both facings; actual Win32 bitmap reflection is covered by
native_walking_tests. This is not live Windows or Box2D execution.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import subprocess
from PIL import Image
from render_motion_preview import atlas
from render_walk_run_audit import BRIDGE, ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--character',type=int,choices=(0,1),required=True)
    parser.add_argument('--frames',type=int,default=128)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.frames<128:parser.error('--frames must be at least128')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    include=out/'source';include.mkdir(exist_ok=True);hashes={}
    for source in (ROOT/'src').glob('*.hpp'):
        data=source.read_bytes();(include/source.name).write_bytes(data)
        hashes['src/'+source.name]=hashlib.sha256(data).hexdigest()
    (out/'bridge.cpp').write_text(BRIDGE)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-DAUDIT_HAS_JOINT_DYNAMICS','-DAUDIT_HAS_SPINE','-DAUDIT_HAS_ACTUAL_MODE',
        '-I'+str(include),str(out/'bridge.cpp'),'-o',str(out/'audit.so')],check=True)
    lib=ctypes.CDLL(str(out/'audit.so'));p=ctypes.c_void_p;d=ctypes.c_double;i=ctypes.c_int
    for name,types,result in [('create',[p,i,i,i],p),('destroy',[p],None),
        ('step',[p,i,d,d,d,d,ctypes.POINTER(d)],i),('pixels',[p],p),('width',[p],i),
        ('height',[p],i),('mode',[p],i)]:
        function=getattr(lib,'audit_'+name);function.argtypes=types;function.restype=result
    frames,_=atlas(('cat.png','cat-silver.png')[args.character]);source=frames[1]
    scale=110/max(frame.height for frame in frames[:6])
    pixels=source.convert('RGBa').tobytes('raw','BGRa');memory=ctypes.create_string_buffer(pixels,len(pixels))
    cases=[]
    for body_width,body_height in ((.78,.96),(1,1),(1.26,.98)):
        width=source.width*scale*body_width;height=source.height*scale*body_height
        for growth in (.82,1.):
            for facing in (-1,1):
                for speed in (18.,29.,38.,45.,80.,140.):
                    instance=lib.audit_create(memory,source.width,source.height,args.character)
                    if not instance:raise RuntimeError('Production rig rejected original artwork')
                    root=0;previous=None;anchors=0;reach=0;drift=0;floor=0;clamped=0;invalid=False
                    try:
                        for frame in range(args.frames):
                            travel=facing*speed/60;root+=travel;values=(d*36)()
                            if not lib.audit_step(instance,int(speed>100),travel/growth,1/60,width,height,values):
                                raise RuntimeError('Production dispatcher/raster failed')
                            reach=max(reach,values[20]);clamped+=int(values[35])!=0
                            world=[(root+facing*values[27+leg*2]*growth,values[28+leg*2]*growth,
                                bool(values[6+leg*3]),values[23+leg]) for leg in range(4)]
                            if previous:
                                for current,old in zip(world,previous):
                                    if current[2] and old[2] and current[3]==old[3]:
                                        anchors+=1;drift=max(drift,abs(current[0]-old[0]),abs(current[1]-old[1]))
                            previous=world
                            w,h=lib.audit_width(instance),lib.audit_height(instance)
                            raw=ctypes.string_at(lib.audit_pixels(instance),w*h*4)
                            image=Image.frombytes('RGBA',(w,h),raw,'raw','BGRA');alpha=image.getchannel('A')
                            bounds=alpha.point(lambda x:255 if x>=153 else 0).getbbox()
                            if bounds:
                                floor=max(floor,(bounds[3]-(source.height-2))*height/source.height*growth)
                                invalid=invalid or bounds[0]==0 or bounds[2]==w or bounds[1]==0 or bounds[3]==h
                        mode=lib.audit_mode(instance)
                        body_speed=speed/growth
                        expected_mode=2 if body_speed>100 else 1 if body_speed>35 else 0
                        case={'character':args.character,'body_width':body_width,'body_height':body_height,
                            'growth':growth,'facing':facing,'desktop_speed':speed,'body_speed':body_speed,
                            'actual_mode':mode,'expected_mode':expected_mode,'frames':args.frames,
                            'stance_segments':anchors,'max_world_stance_drift_px':drift,
                            'max_reach_error_bodypx':reach,'max_painted_below_floor_px':floor,
                            'clamped_frames':clamped,'opaque_output_edge':invalid}
                        cases.append(case)
                    finally:lib.audit_destroy(instance)
    failed=[case for case in cases if case['max_world_stance_drift_px']>1e-7 or
        case['max_reach_error_bodypx']>.01 or case['max_painted_below_floor_px']>0 or
        case['clamped_frames'] or case['opaque_output_edge'] or case['actual_mode']!=case['expected_mode']]
    report={'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'working_tree':True,
        'source_hashes':hashes,'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope':'Production C++ Locomotion/Evaluate/Rig; desktop displacement divided by actual growth; original PNGs',
        'limits':'Linux CPU replay; reflected contacts cover both directions; native GDI+ reflection and live Box2D are separate',
        'status':'passed' if not failed else 'failed','cases':cases,'failed_cases':failed}
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'cases':len(cases),'failed':len(failed),'output':str(out/'audit.json')}))
    if failed:raise SystemExit(1)


if __name__=='__main__':main()
