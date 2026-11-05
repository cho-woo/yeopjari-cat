#!/usr/bin/env python3
"""Existing 2D art at real root travel speeds, rendered through production C++.

The default release source is v2.0.3. Neither source PNGs nor app state change.
This is portable motion review; Windows/GDI+ execution is a separate check.
"""
import argparse
import ctypes
import hashlib
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw
from render_gait_review import library
from render_motion_preview import atlas

ROOT=Path(__file__).resolve().parents[1]
RELEASE='8c34de2f22367a40e7ef811f6e86fd918f397322'
HEADERS=('cat_animation.hpp','cat_deformation.hpp','cat_expression.hpp',
         'cat_locomotion_rig.hpp','cat_preferences.hpp','pet_state.hpp','cat_gait.hpp')


def compact_gif(frames,durations,output):
    """Crop unused camera space; keep the cat at its actual display size."""
    compact=[]
    for source in frames:
        source=source.convert('RGB');width=source.width//2
        frame=Image.new('RGB',(480,360))
        frame.paste(source.crop((0,0,240,360)),(0,0))
        frame.paste(source.crop((width,0,width+240,360)),(240,0))
        compact.append(frame)
    # A shared GIF palette reduces file size without rescaling the artwork or
    # dropping frames. All canvases are opaque; retaining the previous canvas
    # lets the encoder emit differences while still erasing previous paws.
    palette_board=Image.new('RGB',(480,360*10))
    for index in range(10):palette_board.paste(compact[min(len(compact)-1,index*len(compact)//10)],(0,index*360))
    palette=palette_board.quantize(colors=128,method=Image.Quantize.MEDIANCUT)
    compact=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in compact]
    output.parent.mkdir(parents=True,exist_ok=True)
    compact[0].save(output,save_all=True,append_images=compact[1:],duration=durations,loop=0,disposal=1,optimize=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref',default=RELEASE)
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/2d-gait-motion')
    parser.add_argument('--seconds',type=float,default=3)
    parser.add_argument('--fps',type=int,default=50)
    parser.add_argument('--stills-only',action='store_true')
    parser.add_argument('--docs-output',type=Path,help='Also export compact adult walk/run GIFs to this directory')
    parser.add_argument('--after-run-speed',type=float,default=165)
    parser.add_argument('--after-young-run-speed',type=float,default=189.75)
    args=parser.parse_args()
    if not 0<args.seconds<=10 or args.fps not in (25,50,100):
        parser.error('seconds must be 0..10; fps must be 25,50,100 for exact GIF timing')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    commit=subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=ROOT,text=True).strip()
    paths=[]
    for label in ('baseline','current'):
        path=output/label;path.mkdir(exist_ok=True);paths.append(path)
        for header in HEADERS:
            if label=='baseline':
                result=subprocess.run(['git','show',f'{commit}:src/{header}'],cwd=ROOT,capture_output=True)
                if result.returncode and header=='cat_gait.hpp':continue
                result.check_returncode();data=result.stdout
            else:
                if header=='cat_gait.hpp' and not (ROOT/'src'/header).exists():continue
                data=(ROOT/'src'/header).read_bytes()
            (path/header).write_bytes(data)
    libs=[library(output/'baseline.so',paths[0],True),library(output/'current.so',paths[1],False)]
    atlases=[atlas('cat.png')[0],atlas('cat-silver.png')[0]]
    sources=[frames[1] for frames in atlases]
    scales=[110/max(frame.height for frame in frames[:6]) for frames in atlases]
    rigs=[[],[]]
    names=('Cream','Silver')
    scenarios=[('adult-walk',0,1.0,22.0,22.0),('young-walk',0,.82,25.3,25.3),
               ('adult-run',2,1.0,165.0,args.after_run_speed),
               ('young-run',2,.82,189.75,args.after_young_run_speed)]
    manifest={'renderer':'Production C++ Rig::Raster + Evaluate + Locomotion; Pillow final placement, not Windows GDI+',
              'baseline_commit':commit,'fps':args.fps,'duration_seconds':args.seconds,
              'source_sha256':{str((Path('src')/h)):hashlib.sha256((paths[1]/h).read_bytes()).hexdigest() for h in HEADERS if (paths[1]/h).exists()},
              'scenarios':{},'artwork_sha256':{name:hashlib.sha256((ROOT/'assets'/name).read_bytes()).hexdigest() for name in ('cat.png','cat-silver.png')}}
    panel_width=max(512,math.ceil(220+max(s[3] for s in scenarios)*args.seconds),math.ceil(220+max(s[4] for s in scenarios)*args.seconds))
    try:
        for lib,version in zip(libs,range(2)):
            for character,source in enumerate(sources):
                data=source.convert('RGBa').tobytes('raw','BGRa')
                buffer=ctypes.create_string_buffer(data,len(data))
                rig=lib.gait_review_create(buffer,source.width,source.height,character)
                if not rig:raise RuntimeError('Failed to prepare source art')
                rigs[version].append(rig)

        def raster(character,gait,distance,version,growth,step=None):
            lib=libs[version];rig=rigs[version][character];source=sources[character]
            width_body,height_body=source.width*scales[character],source.height*scales[character]
            parameters=(ctypes.c_double*5)()
            okay=lib.gait_review_raster(rig,gait,distance/growth,width_body,height_body) if step is None else lib.gait_review_step(rig,gait,step[0]/growth,step[1],width_body,height_body,parameters)
            if not okay:raise RuntimeError('Production raster rejected sample')
            if step is not None:profile_samples.append({'character':character,'version':version,'world_distance':distance,'stride':parameters[0],'duty':parameters[1],'lift':parameters[2],'phase':parameters[3],'body_speed':parameters[4]})
            width,height=lib.gait_review_width(rig),lib.gait_review_height(rig)
            data=ctypes.string_at(lib.gait_review_pixels(rig),width*height*4)
            image=Image.frombytes('RGBa',(width,height),data,'raw','BGRa')
            scale=scales[character]*growth
            # One resampling step and fractional placement keep subpixel motion.
            image=image.resize((round(width*scale*2),round(height*scale*2)),Image.Resampling.BICUBIC).convert('RGBA')
            offset=(lib.gait_review_inset(rig)*scales[character]+lib.gait_review_offset(rig))*growth
            return image,offset

        def panel(character,gait,distance,version,growth,speed,tracking,foot_only=False,step=None):
            zoom=2;size=((512 if tracking else panel_width)*2,360);ground=290
            canvas=Image.new('RGBA',size,'#edf0f4');pen=ImageDraw.Draw(canvas)
            scroll=distance if tracking else 0
            pen.line((0,ground,size[0],ground),fill='#66798d',width=1)
            for world in range(-1000,2000,10):
                x=(world-scroll)*zoom
                if 0<=x<size[0]:pen.line((x,ground,x,ground+9),fill='#95a5b6',width=1)
            image,offset=raster(character,gait,distance,version,growth,step)
            center=110+(0 if tracking else distance)
            x=center*zoom-image.width/2
            y=ground-image.height+offset*zoom
            placed=image.transform(size,Image.Transform.AFFINE,(1,0,-x,0,1,-y),Image.Resampling.BICUBIC)
            canvas.alpha_composite(placed)
            label='v2.0.3' if version==0 else 'working source'
            pen.text((12,12),f'{names[character]} | {label} | {speed:.2f}px/s | growth {growth:.2f}',fill='#24394d')
            pen.text((12,326),('tracking camera / moving ground' if tracking else 'fixed camera / actual root movement')+f' | root {distance:.2f}px',fill='#425b74')
            if foot_only:
                # Exact twofold view, with no extra resampling beyond the pane.
                return canvas.crop((round(center*zoom-155),190,round(center*zoom+155),315)).convert('RGB')
            return canvas.convert('RGB')

        # Show the exact source partition. Four output slots also accommodate
        # archived three-limb rigs; their missing fourth slot is zero.
        masks=Image.new('RGB',(2000,960),'#edf0f4');pen=ImageDraw.Draw(masks)
        for character,source in enumerate(sources):
            for version,lib in enumerate(libs):
                row=character*2+version
                thumb=source.resize((round(source.width*.65),round(source.height*.65)),Image.Resampling.BICUBIC)
                masks.paste(thumb,(0,row*240),thumb)
                pen.text((5,row*240+223),f'{names[character]} '+('v2.0.3' if version==0 else 'working source'),fill='#24394d')
                data=source.load();values=(ctypes.c_double*4)()
                layers=[Image.new('RGBA',source.size) for _ in range(4)]
                pixels=[layer.load() for layer in layers]
                for y in range(source.height):
                    for x in range(source.width):
                        lib.gait_review_mask(character,(x+.5)/source.width,(y+.5)/source.height,values)
                        r,g,b,a=data[x,y]
                        for leg in range(4):pixels[leg][x,y]=(r,g,b,round(a*values[leg]))
                for leg,layer in enumerate(layers):
                    thumb=layer.resize((round(layer.width*.65),round(layer.height*.65)),Image.Resampling.BICUBIC)
                    masks.paste(thumb,((leg+1)*400,row*240),thumb)
                    pen.text(((leg+1)*400+5,row*240+223),f'leg {leg}',fill='#24394d')
        masks.save(output/'source-leg-masks.png')

        for name,gait,growth,before_speed,after_speed in scenarios:
            speeds=(before_speed,after_speed)
            strides=[lib.gait_review_parameter(gait,0) for lib in libs]
            manifest['scenarios'][name]={'world_speeds_px_s':speeds,'growth':growth,'body_strides_px':strides,
                                         'cycles_per_second':[speed/growth/stride for speed,stride in zip(speeds,strides)]}
            for tracking in (() if args.stills_only else (False,True)):
                frames=[];profile_samples=[]
                for version,lib in enumerate(libs):
                    for rig in rigs[version]:lib.gait_review_reset(rig)
                for frame in range(round(args.seconds*args.fps)):
                    time=frame/args.fps
                    view_width=512 if tracking else panel_width
                    board=Image.new('RGB',(view_width*2,360))
                    for character in range(2):
                        for version,speed in enumerate(speeds):
                            view=panel(character,gait,time*speed,version,growth,speed,tracking,step=(speed/args.fps if frame else 0,1/args.fps if frame else 0))
                            # Store at actual 110px body size while rendering placement at2x.
                            board.paste(view.resize((view_width,180),Image.Resampling.BICUBIC),(version*view_width,character*180))
                    frames.append(board)
                duration=[round((i+1)*1000/args.fps)-round(i*1000/args.fps) for i in range(len(frames))]
                frames[0].save(output/(f'{name}-'+('tracking.gif' if tracking else 'moving.gif')),save_all=True,
                               append_images=frames[1:],duration=duration,loop=0,disposal=2)
                if args.docs_output and tracking and name in ('adult-walk','adult-run'):
                    compact_gif(frames,duration,args.docs_output/f'v2.0.4-{name[6:]}.gif')
                if tracking:(output/f'{name}-samples.json').write_text(json.dumps(profile_samples,indent=2)+'\n')
            # One complete stride at12phases, with isolated foot view.
            sheet=Image.new('RGB',(6*310,4*125))
            for version,lib in enumerate(libs):
                for phase in range(12):
                    distance=phase/12*strides[version]*growth
                    image=panel(0,gait,distance,version,growth,speeds[version],True,True)
                    sheet.paste(image,((phase%6)*310,(version*2+phase//6)*125))
            sheet.save(output/f'{name}-feet.png')
            # Actual persistent contact targets after the speed and stride
            # have settled. Each version spans one of its own gait cycles.
            live_sheet=Image.new('RGB',(6*310,8*125))
            live_samples=[]
            for character in range(2):
                for version,lib in enumerate(libs):
                    profile_samples=[];lib.gait_review_reset(rigs[version][character])
                    speed=speeds[version];distance=0.0
                    for _ in range(75):
                        distance+=speed*.02
                        raster(character,gait,distance,version,growth,step=(speed*.02,.02))
                    stride=profile_samples[-1]['stride'];dt=stride*growth/speed/12
                    for phase in range(12):
                        delta=speed*dt if phase else 0;distance+=delta
                        view=panel(character,gait,distance,version,growth,speed,True,True,step=(delta,dt if phase else 0))
                        sample=profile_samples[-1]
                        ImageDraw.Draw(view).text((5,110),f'{names[character]} '+('v2.0.3' if version==0 else 'working')+f' t={1.5+phase*dt:.3f}s phase={sample["phase"]:.2f}',fill='#24394d')
                        live_samples.append(sample)
                        live_sheet.paste(view,((phase%6)*310,(character*4+version*2+phase//6)*125))
            live_sheet.save(output/f'{name}-settled-feet.png')
            (output/f'{name}-settled-samples.json').write_text(json.dumps(live_samples,indent=2)+'\n')
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(json.dumps({'output':str(output),'scenarios':manifest['scenarios']}))
    finally:
        for version,lib in enumerate(libs):
            for rig in rigs[version]:lib.gait_review_destroy(rig)


if __name__=='__main__':main()
