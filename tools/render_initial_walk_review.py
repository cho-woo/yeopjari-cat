#!/usr/bin/env python3
"""Compare the user's actual v1.0 upload with the current four-pose walk.

The uploaded main.cpp is kept isolated. Its simple Render formulas are replayed
here; current animation headers compile into a read-only C++ bridge. Final
raster placement uses Pillow, not Windows/GDI+ execution. No runtime/art edits.
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from PIL import Image, ImageDraw
from render_original_walk_review import archived_layer
from render_walk_review import save_gif

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_atlas(path):
    image=Image.open(path).convert('RGBA'); mask=image.getchannel('A').point(lambda a:255 if a>16 else 0)
    cleaned=Image.new('RGBA',image.size); cleaned.paste(image,(0,0),mask)
    frames=[]; rectangles=[]
    for index in range(8):
        x0=index%4*image.width//4; x1=(index%4+1)*image.width//4
        y0=index//4*image.height//2; y1=(index//4+1)*image.height//2
        left,top,right,bottom=mask.crop((x0,y0,x1,y1)).getbbox()
        box=(max(x0,x0+left-2),max(y0,y0+top-2),min(x1,x0+right+2),min(y1,y0+bottom+2))
        rectangles.append(box); frames.append(cleaned.crop(box))
    return frames,rectangles


def bridge(output,commit):
    include=output/'current-headers'; include.mkdir(exist_ok=True)
    if commit=='WORKTREE':
        for path in (ROOT/'src').glob('*.hpp'):(include/path.name).write_bytes(path.read_bytes())
    else:
        paths=subprocess.check_output(['git','ls-tree','-r','--name-only',commit,'src'],cwd=ROOT,text=True).splitlines()
        for path in paths:
            if path.endswith('.hpp'):(include/Path(path).name).write_bytes(subprocess.check_output(['git','show',f'{commit}:{path}'],cwd=ROOT))
    source=output/'bridge.cpp'; source.write_text(r'''
#include "cat_animation.hpp"
struct Review { cat::animation::Locomotion gait; double elapsed=0; };
extern "C" void* initial_review_create() { return new Review; }
extern "C" void initial_review_destroy(void* p) { delete static_cast<Review*>(p); }
extern "C" int initial_review_step(void* p,double distance,double dt,double* output) {
    auto& review=*static_cast<Review*>(p);
    if(!review.gait.Advance(cat::animation::Motion::Walk,distance,dt))return 0;
    review.elapsed+=dt;
    const auto ms=static_cast<std::uint64_t>(review.elapsed*1000);
    const auto pose=cat::animation::Evaluate(cat::animation::Motion::Walk,ms,500000+ms,1,review.gait);
    output[0]=pose.frame;output[1]=pose.blendFrame;output[2]=pose.blend;
    output[3]=pose.x;output[4]=pose.lift;output[5]=pose.scaleX;output[6]=pose.scaleY;
    output[7]=pose.angle;output[8]=review.gait.OriginalWalkPhase();return 1;
}
''')
    library=output/'current-walk.so'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(include),str(source),'-o',str(library)],check=True)
    lib=ctypes.CDLL(str(library)); p=ctypes.c_void_p; d=ctypes.c_double
    lib.initial_review_create.restype=p
    lib.initial_review_destroy.argtypes=[p]
    lib.initial_review_step.argtypes=[p,d,d,ctypes.POINTER(d)]
    lib.initial_review_step.restype=ctypes.c_int
    return lib


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,required=True,help='Isolated uploaded v1.0 directory, containing main.cpp, EXE and extracted assets/cat.png')
    parser.add_argument('--current-ref',default='HEAD')
    parser.add_argument('--require-single-frame',action='store_true',help='Reject any current walking sample that blends or deforms the original drawing')
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/uploaded-initial-walk-review')
    args=parser.parse_args(); ref=args.reference.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    working=args.current_ref=='WORKTREE'
    commit=subprocess.check_output(['git','rev-parse','HEAD' if working else args.current_ref],cwd=ROOT,text=True).strip()
    version_source=(ROOT/'src/app_version.h').read_text() if working else subprocess.check_output(['git','show',f'{commit}:src/app_version.h'],cwd=ROOT,text=True)
    app_version=re.search(r'YEOPJARI_VERSION_STRING "([^"]+)"',version_source).group(1)
    prefix='initial-v10-v'+app_version.replace('.','')
    main_source=(ref/'main.cpp').read_text()
    required=('(ticks - g.motionStart) / 150','(std::sin(ticks / 700.0) + 1.0) * 0.38','g.surface.height - 1 - breath')
    if not all(expression in main_source for expression in required): raise RuntimeError('Uploaded source differs from reviewed v1.0 formulas')
    build_info=json.loads((ref/'BUILD_INFO.json').read_text())
    if digest(ref/'YeopjariCat.exe')!=build_info['exe_sha256']: raise RuntimeError('Uploaded executable checksum mismatch')
    original_art=ref/'assets/cat.png'
    current_art=(ROOT/'assets/cat.png').read_bytes() if working else subprocess.check_output(['git','show',f'{commit}:assets/cat.png'],cwd=ROOT)
    if original_art.read_bytes()!=current_art: raise RuntimeError('Original and current art differ; compare separate atlases before proceeding')
    frames,rectangles=source_atlas(original_art); reference_height=max(frame.height for frame in frames[:6]); scale=110/reference_height
    lib=bridge(output,'WORKTREE' if working else commit); instance=lib.initial_review_create()
    speed=22.; dt=.04; count=90; states=[]; samples=[]; native=[]; close=[]

    def old_pose(time):
        elapsed=round(time*1000); frame=(elapsed//150)%4
        breath=(math.sin((500000+elapsed)/700)+1)*.38
        return [frame,-1,0,0,breath,1,1,0]

    def panel(version,pose,time,zoom=1,moving=True,labels=True):
        canvas=Image.new('RGBA',(260*zoom,190*zoom),'#edf0f4'); draw=ImageDraw.Draw(canvas)
        world_center=80+speed*time
        # v1.0 rendered exactly at window center; its whole HWND was rounded.
        # Current renderer retains the horizontal remainder within the HWND.
        center=math.floor(world_center+.5) if version==0 else world_center
        if not moving:center-=speed*time
        for world in range(-100,400,10):
            x=(world-(0 if moving else speed*time))*zoom
            draw.line((x,155*zoom,x,159*zoom),fill='#9aa8b8')
        draw.line((0,155*zoom,260*zoom,155*zoom),fill='#6a7888')
        # Both renderers reserve the same final transparent contact pixel.
        canvas.alpha_composite(archived_layer(frames,scale,pose,center,154,canvas.size,zoom))
        if labels:
            name='ACTUAL v1.0 | hard frame 150ms' if version==0 else 'CURRENT | distance-driven walk'
            draw.text((4*zoom,5*zoom),name,fill='#24394d')
            draw.text((4*zoom,172*zoom),f't={time:.2f}s  frame={int(pose[0])}  mix={pose[2]:.2f}',fill='#24394d')
        return canvas.convert('RGB')

    try:
        for index in range(count):
            time=index*dt; values=(ctypes.c_double*9)()
            if not lib.initial_review_step(instance,speed*dt if index else 0,dt if index else 0,values): raise RuntimeError('Current evaluator rejected sample')
            original,current=old_pose(time),list(values)
            if not (current[3:8]==[0,0,1,1,0]): raise RuntimeError('Current rendering contract has changed')
            if args.require_single_frame and (current[1]!=-1 or current[2]!=0):
                raise RuntimeError('Current walk still mixes two original drawings')
            states.append((original,current))
            samples.append({'time':time,'v10_frame':original[0],'v10_breath_px':original[4],
                            'current_frame':current[0],'current_blend_frame':current[1],'current_blend':current[2],'current_phase':current[8]})
            board=Image.new('RGB',(520,190)); detailed=Image.new('RGB',(1560,570))
            for version,pose in enumerate((original,current)):
                board.paste(panel(version,pose,time),(version*260,0))
                detailed.paste(panel(version,pose,time,3,False),(version*780,0))
            native.append(board); close.append(detailed)
        save_gif(native,output/(prefix+'-real-time-1x.gif'),40)
        save_gif(close,output/(prefix+'-detail-3x.gif'),40)
        # Include the former crossfade intervals, not just held frames.
        indexes=(0,3,4,7,8,11,12,15)
        for detail,box in [('full',(45,120,450,465)),('face',(180,120,440,355)),('paws',(45,350,450,470))]:
            width,height=box[2]-box[0],box[3]-box[1]
            sheet=Image.new('RGB',(4*width,4*height),'#edf0f4')
            for version in range(2):
                for column,index in enumerate(indexes):
                    time=index*dt
                    view=panel(version,states[index][version],time,3,False,False).crop(box)
                    ImageDraw.Draw(view).text((4,4),('v1.0' if version==0 else 'current')+f' t={time:.2f}s',fill='#24394d')
                    sheet.paste(view,((column%4)*width,(version*2+column//4)*height))
            sheet.save(output/f'{prefix}-{detail}-sequence.png')
        native[3].save(output/(prefix+'-at-120ms.png'))
        single_frame=all(row['current_blend_frame']==-1 and row['current_blend']==0 for row in samples)
        proof={
            'reference':'Actual user uploaded v1.0.0, not inferred v1.3/v1.9 history',
            'reference_paths_sha256':{name:digest(ref/name) for name in ('main.cpp','BUILD_INFO.json','YeopjariCat.exe','resources.rc','assets/cat.png')},
            'current_commit':commit,'working_tree':working,'current_version':app_version,'current_header_sha256':digest(output/'current-headers/cat_animation.hpp'),
            'tool_sha256':digest(Path(__file__)),'source_art_identical':True,'resource':'RCDATA type10 id101 extracted from supplied EXE',
            'speed_px_s':speed,'tick_ms':40,'reference_height_px':110,'atlas_reference_height_source_px':reference_height,
            'crop_rectangles':rectangles,'walking_visible_rectangle_heights_px':[frame.height*scale for frame in frames[:4]],
            'initial_walk':'floor(actionElapsedMs/150)%4, hard switch, no crossfade,600ms/cycle; window center integer rounded',
            'initial_breath':'(sin(systemTicks/700)+1)*0.38px entire-image vertical translation; systemTicks begins500000 for repeatability',
            'current_walk':'14 logical body px/cycle; '+('one complete original drawing, no blending' if single_frame else 'hold first60%, SmoothStep blend final40%')+'; zero decorative lift; subpixel horizontal root',
            'all_samples_single_frame':single_frame,
            'current_cycle_seconds':14/speed,'current_hold_ms':14/speed/4*(1 if single_frame else .6)*1000,'current_blend_ms':0 if single_frame else 14/speed/4*.4*1000,
            'placement':'Both use110/max(first6 cropped source heights), bottom-center individual crops and1px contact margin; no per-frame resizing to equal height',
            'limits':'C++ current evaluator and exact uploaded source formulas with Pillow final raster. Not live Windows/GDI+, Box2D or window interaction; constant actual travel replay only. v1.0 has no Box2D.',
            'samples':samples,
        }
        (output/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(output)
    finally:lib.initial_review_destroy(instance)


if __name__=='__main__':main()
