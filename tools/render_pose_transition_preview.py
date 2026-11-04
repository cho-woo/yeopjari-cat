#!/usr/bin/env python3
"""Small actual-art Loaf->Idle comparison using archived/current production C++.

Pillow rasterizes the warped premultiplied crop. This is a portable developer
preview, not Windows/GDI+ execution. No source art or application saves change.
GIFs play once, avoiding an invented Idle->Loaf snap at an artificial loop seam.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont
from render_motion_preview import atlas

ROOT=Path(__file__).resolve().parents[1]
HEADERS=['cat_animation.hpp','cat_deformation.hpp','cat_expression.hpp']

def digest(data):
    return hashlib.sha256(data).hexdigest()

def font(size):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',size)

def library(path,include,baseline):
    command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(include),str(ROOT/'tools/pose_transition_preview.cpp'),'-o',str(path)]
    if baseline:command.append('-DPREVIEW_BASELINE')
    subprocess.run(command,check=True)
    lib=ctypes.CDLL(str(path))
    doubles=ctypes.POINTER(ctypes.c_double)
    lib.transition_preview_pose.argtypes=[ctypes.c_uint,ctypes.c_uint,doubles]
    lib.transition_preview_pose.restype=ctypes.c_int
    lib.transition_preview_warp.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,doubles]
    lib.transition_preview_warp.restype=ctypes.c_int
    lib.transition_preview_metrics.argtypes=[doubles,doubles]
    return lib

def render_skin(lib,source,values):
    raw=source.convert('RGBa').tobytes('raw','BGRa')
    original=ctypes.create_string_buffer(raw,len(raw))
    destination=ctypes.create_string_buffer(len(raw))
    if not lib.transition_preview_warp(original,source.width,source.height,destination,values):
        raise RuntimeError('Production Warp failed')
    return Image.frombytes('RGBa',source.size,destination.raw,'raw','BGRa')

def raster(skin,scale,canvas,center,bottom):
    # Same inverse translate/scale as the existing motion preview. Premultiplied
    # bicubic sampling avoids transparent RGB fringes around the original face.
    return skin.transform(canvas,Image.Transform.AFFINE,
        (1/scale,0,skin.width/2-center/scale,0,1/scale,skin.height-bottom/scale),
        Image.Resampling.BICUBIC).convert('RGBA')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref',default='2fe5a17adea3478b492a3edddc442494cd62d063')
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/v2.0.2')
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    commit=subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=ROOT,text=True).strip()
    archived=out/'pose-transition-baseline';archived.mkdir(exist_ok=True)
    before_hash={}
    for name in HEADERS:
        data=subprocess.check_output(['git','show',f'{commit}:src/{name}'],cwd=ROOT)
        (archived/name).write_bytes(data);before_hash['src/'+name]=digest(data)
    before=library(out/'pose_transition_before.so',archived,True)
    after=library(out/'pose_transition_after.so',ROOT/'src',False)
    sheets=[atlas(name)[0] for name in ('cat.png','cat-silver.png')]
    for name in ('cat.png','cat-silver.png'):
        original=subprocess.check_output(['git','show',f'{commit}:assets/{name}'],cwd=ROOT)
        if original!=(ROOT/'assets'/name).read_bytes():raise RuntimeError('Art differs from baseline: '+name)
    states=[[],[]];metrics={}
    times=range(0,1000,20)
    for character,frames in enumerate(sheets):
        source=frames[4];scale=110/max(frame.height for frame in frames[:6])
        traces=[[],[]]
        for lib in (before,after):lib.transition_preview_reset()
        for t in times:
            images=[]
            for index,lib in enumerate((before,after)):
                values=(ctypes.c_double*8)();numbers=(ctypes.c_double*3)()
                if not lib.transition_preview_pose(t,character,values):raise RuntimeError('Noncanonical preview pose')
                lib.transition_preview_metrics(values,numbers)
                traces[index].append(list(numbers))
                images.append(render_skin(lib,source,values))
            if t<400 or t>=580:
                if images[0].tobytes()!=images[1].tobytes():raise RuntimeError('Before/after differs outside transition')
            states[character].append(images)
        for side,trace in zip(('before','after'),traces):
            if max(abs(row[1]-(.3**2+.02**2)**.5) for row in trace)>1e-12:
                raise RuntimeError('Head shape changed')
            if any(row[2]!=1 for row in trace):raise RuntimeError('Supporting body baseline moved')
            metrics[f'{("Cream","Silver")[character]}_{side}']={
                'max_head_step_px_at_1x':max(abs(a[0]-b[0])*source.height*scale for a,b in zip(trace,trace[1:])),
                'head_pair_distance_constant':True,'supporting_baseline_constant':True,
                'canonical_draw_height_px':source.height*scale}
    products=[]
    for zoom in (1,3):
        width,height=(640,420) if zoom==1 else (960,870)
        header,rowheight,margin=(64,160,24) if zoom==1 else (64,385,20)
        frames=[]
        for index,t in enumerate(times):
            frame=Image.new('RGBA',(width,height),'#f4f2ec');draw=ImageDraw.Draw(frame)
            draw.text((14,8),'LOAF -> IDLE  |  same original cat art',font=font(17),fill='#273640')
            draw.text((14,32),f'{zoom}x view | adult 110px reference | change at 400ms',font=font(13),fill='#53636e')
            for side,label in enumerate(('Before: immediate posture change','After: production 180ms transition')):
                draw.text((side*width//2+14,header-10),label,font=font(12 if zoom==1 else 15),fill='#354e5e')
            for character,sheet in enumerate(sheets):
                bottom=header+(character+1)*rowheight-margin
                draw.text((14,header+character*rowheight+16),('Cream','Silver')[character],font=font(13),fill='#53636e')
                draw.line((12,bottom,width-12,bottom),fill='#c7c3b8',width=1)
                scale=110/max(item.height for item in sheet[:6])*zoom
                for side in (0,1):
                    layer=raster(states[character][index][side],scale,frame.size,(side+.5)*width/2,bottom)
                    frame.alpha_composite(layer)
            draw=ImageDraw.Draw(frame)
            draw.text((14,height-24),'Portable C++ Evaluate/Warp + Pillow preview. Not a Windows capture.',font=font(12),fill='#69747b')
            frames.append(frame.convert('RGB'))
        # One global palette prevents frame-local palette changes from making
        # the unmodified face appear to change color during the transition.
        palette_sheet=Image.new('RGB',(width*4,height))
        for i,frame_index in enumerate((0,20,24,30)):palette_sheet.paste(frames[frame_index],(i*width,0))
        palette=palette_sheet.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
        indexed=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
        target=out/f'loaf-idle-before-after-{zoom}x.gif'
        indexed[0].save(target,save_all=True,append_images=indexed[1:],duration=20,optimize=False,disposal=1)
        products.append({'file':target.name,'width':width,'height':height,'duration_ms':1000,'bytes':target.stat().st_size,'sha256':digest(target.read_bytes())})
        contact=Image.new('RGB',(width,height*3))
        for row,frame_index in enumerate((19,20,24)):contact.paste(frames[frame_index],(0,row*height))
        if zoom==1:contact.save(out/'loaf-idle-transition-contact.png')
    sources=['src/'+name for name in HEADERS]+['src/cat_pose_transition.hpp','assets/cat.png','assets/cat-silver.png',
        'tools/pose_transition_preview.cpp','tools/render_pose_transition_preview.py','tools/render_motion_preview.py']
    provenance={'renderer':'Portable archived/current production C++ Evaluate/Warp + Pillow; NOT Win32/GDI+',
        'baseline_commit':commit,'baseline_source_sha256':before_hash,
        'current_source_sha256':{name:digest((ROOT/name).read_bytes()) for name in sources},
        'timeline':'0..399ms mature Loaf; 400..999ms Idle. Production transition runs 400..580ms.',
        'joint_quantization':32,'face':'same original canonical head, no expression changes or replacement art',
        'physics':'stationary supported body; no physics claims from this rendering preview',
        'metrics':metrics,'products':products}
    (out/'loaf-idle-preview-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(json.dumps({'products':products,'metrics':metrics},indent=2))

if __name__=='__main__':main()
