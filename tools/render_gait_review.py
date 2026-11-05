#!/usr/bin/env python3
"""Compare actual release/current locomotion rigs at the same traveled distance.

All raster/articulation runs in production C++; Pillow only places its result.
This is a portable review, not a Windows/GDI+ execution. Source art is read-only.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import subprocess
import time
from PIL import Image, ImageDraw, ImageChops
from render_motion_preview import atlas
from render_expression_preview import companion_crops

ROOT=Path(__file__).resolve().parents[1]
NAMES=('Cream','Silver','Black','Calico','Siamese','Munchkin','Dog','Rabbit','Tuxedo','British')
GAITS=('walk','stalk','run')
P=ctypes.c_void_p
D=ctypes.c_double


def library(out,include,baseline):
    command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(include),str(ROOT/'tools/gait_review.cpp'),'-o',str(out)]
    if baseline:command.append('-DGAIT_REVIEW_BASELINE')
    subprocess.run(command,check=True)
    lib=ctypes.CDLL(str(out))
    signatures={'create':([P,ctypes.c_int,ctypes.c_int,ctypes.c_int],P),'destroy':([P],None),
                'raster':([P,ctypes.c_int,D,D,D],ctypes.c_int),'pixels':([P],P),
                'width':([P],ctypes.c_int),'height':([P],ctypes.c_int),'inset':([P],D),'offset':([P],D),
                'benchmark':([P,ctypes.c_int,ctypes.c_int,D,D],D)}
    for name,(arguments,result) in signatures.items():
        function=getattr(lib,'gait_review_'+name);function.argtypes=arguments;function.restype=result
    return lib


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref',default='da08a7e87da254486e39ec7261dd5ef17803d728')
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/gait-v203')
    parser.add_argument('--benchmark-frames',type=int,default=0)
    args=parser.parse_args()
    if args.benchmark_frames<0:parser.error('--benchmark-frames must be nonnegative')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    archived=out/'baseline';archived.mkdir(exist_ok=True)
    commit=subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=ROOT,text=True).strip()
    headers=('cat_animation.hpp','cat_deformation.hpp','cat_expression.hpp','cat_locomotion_rig.hpp','cat_preferences.hpp','pet_state.hpp')
    for name in headers:
        (archived/name).write_bytes(subprocess.check_output(['git','show',f'{commit}:src/{name}'],cwd=ROOT))
    libs=[library(out/'before.so',archived,True),library(out/'after.so',ROOT/'src',False)]
    legacy=[atlas('cat.png')[0],atlas('cat-silver.png')[0]]
    sources=[items[1] for items in legacy]+companion_crops(2)
    scales=[110/max(im.height for im in items[:6]) for items in legacy]
    scales+=[110/im.height for im in companion_crops(0)]
    rigs=[[],[]]
    benchmarks={};raw_benchmarks={};checked=0;ground_range=[float('inf'),float('-inf')]
    try:
        for version,lib in enumerate(libs):
            for c,source in enumerate(sources):
                raw=source.convert('RGBa').tobytes('raw','BGRa')
                memory=ctypes.create_string_buffer(raw,len(raw))
                rig=lib.gait_review_create(memory,source.width,source.height,c)
                if not rig:raise RuntimeError('Prepare failed: '+NAMES[c])
                rigs[version].append(rig)

        if args.benchmark_frames:
            for c,source in enumerate(sources[:2]):
                raw_benchmarks[NAMES[c]]={}
                for gait,name in enumerate(GAITS):
                    values=[]
                    for version,lib in enumerate(libs):
                        value=lib.gait_review_benchmark(rigs[version][c],gait,args.benchmark_frames,
                                                       source.width*scales[c],source.height*scales[c])
                        if value<0:raise RuntimeError('Benchmark raster failed')
                        values.append(round(value,4))
                    raw_benchmarks[NAMES[c]][name]={'v2.0.2_ms':values[0],'refined_ms':values[1]}

        def draw(c,gait,distance,version,zoom=1,body=(1,1),audit=False):
            lib=libs[version];rig=rigs[version][c];source=sources[c]
            if not lib.gait_review_raster(rig,gait,distance,source.width*scales[c]*body[0],source.height*scales[c]*body[1]):
                raise RuntimeError(f'Raster failed: {NAMES[c]}, {GAITS[gait]}, distance={distance}, body={body}')
            width,height=lib.gait_review_width(rig),lib.gait_review_height(rig)
            raw=ctypes.string_at(lib.gait_review_pixels(rig),width*height*4)
            if audit:
                premultiplied=Image.frombytes('RGBA',(width,height),raw,'raw','BGRA')
                alpha=premultiplied.getchannel('A')
                for channel in ('R','G','B'):
                    if ImageChops.subtract(premultiplied.getchannel(channel),alpha).getbbox():
                        raise RuntimeError('Invalid premultiplied alpha: '+NAMES[c])
                bounds=alpha.point(lambda a:255 if a>32 else 0).getbbox()
                if not bounds or bounds[0]<=0 or bounds[2]>=width:
                    raise RuntimeError('Toe clipped at horizontal output edge: '+NAMES[c])
                body_raw=source.convert('RGBa').tobytes('raw','BGRa');padding=(width-source.width)//2
                for row in range(int(source.height*.68)):
                    start=(row*width+padding)*4
                    if raw[start:start+source.width*4]!=body_raw[row*source.width*4:(row+1)*source.width*4]:
                        raise RuntimeError('Head/torso source changed: '+NAMES[c])
                solid=alpha.point(lambda a:255 if a>153 else 0).getbbox()
                # Position of the lowest substantial paw pixel relative to
                # the physical support, including renderer compression/inset.
                ground=(solid[3]-(height-2))*scales[c]*body[1]+lib.gait_review_offset(rig)
                ground_range[0]=min(ground_range[0],ground);ground_range[1]=max(ground_range[1],ground)
                if ground < -1.8 or ground > 1.2:
                    raise RuntimeError(f'Lost planted support: {NAMES[c]} {GAITS[gait]} {body} {distance}: {ground:.3f}px')
            image=Image.frombytes('RGBa',(width,height),raw,'raw','BGRa')
            image=image.resize((round(width*scales[c]*zoom*body[0]),round(height*scales[c]*zoom*body[1])),Image.Resampling.BICUBIC).convert('RGBA')
            canvas=Image.new('RGBA',(240*zoom,180*zoom),'#edf0f4');pen=ImageDraw.Draw(canvas)
            ground=145*zoom
            pen.line((0,ground,canvas.width,ground),fill='#718396',width=zoom)
            for x in range(-50,300,6):
                px=round((x-distance%6)*zoom);pen.line((px,ground,px,ground+4*zoom),fill='#9ba9b8',width=zoom)
            offset=lib.gait_review_inset(rig)*scales[c]*body[1]+lib.gait_review_offset(rig)
            # Subpixel translation matches the runtime's floating placement;
            # rounding here hid the subtle weight shift at normal display size.
            x=110*zoom-image.width/2;y=ground-image.height+offset*zoom
            placed=image.transform(canvas.size,Image.Transform.AFFINE,(1,0,-x,0,1,-y),Image.Resampling.BICUBIC)
            canvas.alpha_composite(placed)
            pen.text((5*zoom,6*zoom),f'{NAMES[c]} {GAITS[gait]} | '+('v2.0.2' if version==0 else 'refined'),fill='#273a4b')
            pen.text((5*zoom,162*zoom),f'travel {distance:.1f}px | tracking camera',fill='#4c657b')
            return canvas.convert('RGB')

        for gait,name in enumerate(GAITS):
            speed=(38,16,92)[gait]
            frames=[]
            for frame in range(61):
                distance=frame*speed/30
                # Two species, two versions at real 110px character size.
                sheet=Image.new('RGB',(480,360))
                for row,c in enumerate((0,2)):
                    for version in (0,1):sheet.paste(draw(c,gait,distance,version),(version*240,row*180))
                frames.append(sheet)
            frames[0].save(out/f'{name}-before-after.gif',save_all=True,append_images=frames[1:],duration=33)
            sheet=Image.new('RGB',(1440,1440))
            for row,c in enumerate((0,2)):
                for column,phase in enumerate((0,.25,.5)):
                    for version in (0,1):
                        sheet.paste(draw(c,gait,phase*(24,20,32)[gait],version,2),(column*480,(row*2+version)*360))
            sheet.save(out/f'{name}-contact-2x.png')

        # Source resources, full cycle, gaits and body shapes. This checks the
        # real artwork path instead of relying only on synthetic limb tests.
        for c,source in enumerate(sources):
            start=time.perf_counter();iterations=0
            for gait in range(3):
                for body in ((1,1),(.82,1.04),(1.25,.96)):
                    for frame in range(25):
                        draw(c,gait,frame*(24,20,32)[gait]/24,1,body=body,audit=True)
                        checked+=1;iterations+=1
            benchmarks[NAMES[c]]=round((time.perf_counter()-start)*1000/iterations,3)
        sheet=Image.new('RGB',(1440,1800))
        for c in range(10):
            for gait in range(3):
                for column,phase in enumerate((.18,.64)):
                    sheet.paste(draw(c,gait,phase*(24,20,32)[gait],1),((gait*2+column)*240,c*180))
        sheet.save(out/'all-species-gaits-1x.png')
        files=['src/'+name for name in headers]+['tools/gait_review.cpp','tools/render_gait_review.py']
        manifest={'renderer':'Production C++ rigs; Pillow final placement, not Windows/GDI+',
                  'baseline_commit':commit,'artwork_cases':checked,'lowest_planted_paw_relative_to_support_px':ground_range,'preview_ms_per_frame_including_Pillow':benchmarks,
                  'raw_rig_benchmark_frames':args.benchmark_frames,'raw_rig_benchmark':raw_benchmarks,
                  'source_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}}
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(json.dumps({'output':str(out),'artwork_cases':checked,'raw_rig_benchmark':raw_benchmarks}))
    finally:
        for version,lib in enumerate(libs):
            for rig in rigs[version]:lib.gait_review_destroy(rig)


if __name__=='__main__':main()
