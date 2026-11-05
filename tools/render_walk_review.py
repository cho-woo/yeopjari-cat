#!/usr/bin/env python3
"""Review ordinary 2D walking against the released v2.0.4 implementation.

Runs production C++ with persistent contact state. PNG artwork is read-only.
The colored markers identify toe targets, not inferred 3D bones. Pillow final
placement is a portable preview and does not replace Windows/GDI+ execution.
"""
import argparse
import ctypes
import hashlib
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw
from render_motion_preview import atlas

ROOT = Path(__file__).resolve().parents[1]
RELEASE = '30737e9'
HEADERS = ('cat_animation.hpp', 'cat_deformation.hpp', 'cat_expression.hpp',
           'cat_locomotion_rig.hpp', 'cat_preferences.hpp', 'pet_state.hpp', 'cat_gait.hpp')
LEG_NAMES = ('near hind', 'far hind', 'near front', 'far front')
COLORS = ('#c43933', '#ac62bc', '#167bb8', '#29934b')


def library(output, include):
    bridge = output.with_suffix('.cpp')
    bridge.write_text('#include "' + str(ROOT/'tools/gait_review.cpp') + '"\n' + r'''
extern "C" void walk_review_feet(void* p, double* output) {
    auto& review = *static_cast<Review*>(p);
    const auto feet = review.locomotion.Feet(cat::animation::Motion::Walk);
    for (int i=0; i<4; ++i) {
        output[i*4] = feet[i].x; output[i*4+1] = feet[i].y;
        output[i*4+2] = feet[i].stance; output[i*4+3] = feet[i].contact;
    }
}
extern "C" void walk_review_offsets(double* output) {
    for (int i=0; i<4; ++i) output[i] = cat::gait::LegPhase(i,.68);
}
''')
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-DGAIT_REVIEW_TRACKED_FEET', '-I'+str(include),
                    str(bridge), '-o', str(output)], check=True)
    lib = ctypes.CDLL(str(output))
    p, d, i = ctypes.c_void_p, ctypes.c_double, ctypes.c_int
    signatures = {'create': ([p,i,i,i],p), 'destroy': ([p],None),
                  'step': ([p,i,d,d,d,d,ctypes.POINTER(d)],i),
                  'width': ([p],i), 'height': ([p],i), 'pixels': ([p],p),
                  'inset': ([p],d), 'offset': ([p],d), 'reset': ([p],None),
                  'mask': ([i,d,d,ctypes.POINTER(d)],None)}
    for name,(args,result) in signatures.items():
        function = getattr(lib,'gait_review_'+name)
        function.argtypes, function.restype = args, result
    lib.walk_review_feet.argtypes = [p,ctypes.POINTER(d)]
    lib.walk_review_feet.restype = None
    lib.walk_review_offsets.argtypes = [ctypes.POINTER(d)]
    lib.walk_review_offsets.restype = None
    return lib


def save_gif(frames, path, milliseconds):
    # Shared palette; motion timing and original display scale are unchanged.
    palette_board = Image.new('RGB',(frames[0].width,frames[0].height*8))
    for n in range(8):
        palette_board.paste(frames[n*len(frames)//8],(0,n*frames[0].height))
    palette = palette_board.quantize(colors=192,method=Image.Quantize.MEDIANCUT)
    frames = [frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=milliseconds,
                   loop=0,disposal=1,optimize=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref',default=RELEASE)
    parser.add_argument('--output',type=Path,default=ROOT/'build/previews/walk-v204-review')
    parser.add_argument('--speed',type=float,default=22)
    parser.add_argument('--seconds',type=float,default=2.4)
    parser.add_argument('--stills-only',action='store_true')
    args = parser.parse_args()
    if not 0<args.speed<=45 or not 1.2<=args.seconds<=6:
        parser.error('ordinary walk speed must be 0..45 px/s; duration 1.2..6 seconds')
    output = args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    commit = subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=ROOT,text=True).strip()
    snapshots = []
    for version in ('baseline','current'):
        path = output/version; path.mkdir(exist_ok=True); snapshots.append(path)
        for header in HEADERS:
            data = subprocess.check_output(['git','show',f'{commit}:src/{header}'],cwd=ROOT) if version=='baseline' else (ROOT/'src'/header).read_bytes()
            (path/header).write_bytes(data)
    libs = [library(output/(name+'.so'),path) for name,path in zip(('baseline','current'),snapshots)]
    atlases = [atlas(name)[0] for name in ('cat.png','cat-silver.png')]
    sources = [frames[1] for frames in atlases]
    scales = [110/max(frame.height for frame in frames[:6]) for frames in atlases]
    rigs = [[],[]]
    toe_x = [[],[]]
    samples = []
    offsets = []
    for lib in libs:
        values = (ctypes.c_double*4)(); lib.walk_review_offsets(values); offsets.append(list(values))
    try:
        for version,lib in enumerate(libs):
            for character,source in enumerate(sources):
                data = source.convert('RGBa').tobytes('raw','BGRa')
                memory = ctypes.create_string_buffer(data,len(data))
                rig = lib.gait_review_create(memory,source.width,source.height,character)
                if not rig: raise RuntimeError('Could not prepare source artwork')
                rigs[version].append(rig)
                # Locate the visible toe center from the original mask's
                # bottom forty pixels. This does not invent missing limb art.
                weights = [0.0]*4; moments = [0.0]*4
                alpha = source.getchannel('A'); values = (ctypes.c_double*4)()
                for y in range(source.height-40,source.height):
                    for x in range(source.width):
                        lib.gait_review_mask(character,(x+.5)/source.width,(y+.5)/source.height,values)
                        for leg in range(4):
                            weight = alpha.getpixel((x,y))*values[leg]
                            weights[leg] += weight; moments[leg] += weight*(x+.5)
                toe_x[version].append([moments[leg]/weights[leg] for leg in range(4)])

        def capture(character,version,dt):
            lib,rig,source = libs[version],rigs[version][character],sources[character]
            parameters = (ctypes.c_double*5)()
            if not lib.gait_review_step(rig,0,args.speed*dt,dt,source.width*scales[character],source.height*scales[character],parameters):
                raise RuntimeError(f'Walk raster rejected character {character}, version {version}')
            width,height = lib.gait_review_width(rig),lib.gait_review_height(rig)
            raw = ctypes.string_at(lib.gait_review_pixels(rig),width*height*4)
            image = Image.frombytes('RGBa',(width,height),raw,'raw','BGRa')
            feet = (ctypes.c_double*16)(); lib.walk_review_feet(rig,feet)
            offset = lib.gait_review_inset(rig)*scales[character]+lib.gait_review_offset(rig)
            return image,offset,[list(feet[n*4:n*4+4]) for n in range(4)],list(parameters)

        def panel(state,character,version,time,zoom=2,trace=False,moving=False):
            source = sources[character]; image,offset,feet,parameters = state
            image = image.resize((round(image.width*scales[character]*zoom),round(image.height*scales[character]*zoom)),Image.Resampling.BICUBIC).convert('RGBA')
            canvas = Image.new('RGBA',(240*zoom,180*zoom),'#edf0f4'); pen = ImageDraw.Draw(canvas)
            ground = 145*zoom
            distance = args.speed*time; center = 90+distance if moving else 110
            for world in range(-100,300,10):
                x = (world-(0 if moving else distance))*zoom
                pen.line((x,ground,x,ground+4*zoom),fill='#99a7b7',width=1)
            pen.line((0,ground,canvas.width,ground),fill='#64758b',width=1)
            x = center*zoom-image.width/2; y = ground-image.height+offset*zoom
            placed = image.transform(canvas.size,Image.Transform.AFFINE,(1,0,-x,0,1,-y),Image.Resampling.BICUBIC)
            canvas.alpha_composite(placed)
            name = ('Cream','Silver')[character]; label = 'v2.0.4' if version==0 else 'working'
            pen.text((6*zoom,5*zoom),f'{name} | {label} | {args.speed:g}px/s',fill='#24394d')
            pen.text((6*zoom,162*zoom),f't={time:.2f}s phase={parameters[3]:.2f}',fill='#24394d')
            if trace:
                for leg,(fx,fy,stance,_) in enumerate(feet):
                    tx = (center-source.width*scales[character]/2+toe_x[version][character][leg]*scales[character]+fx)*zoom
                    ty = ground+fy*zoom
                    radius = 2*zoom
                    pen.ellipse((tx-radius,ty-radius,tx+radius,ty+radius),fill=COLORS[leg] if stance else None,outline=COLORS[leg],width=2)
                    pen.line((tx,ty,tx,ground+(8+leg*4)*zoom),fill=COLORS[leg],width=1)
            return canvas.convert('RGB')

        # Settle the production gait before judging its repeated cycle.
        for version in range(2):
            for character in range(2):
                for _ in range(75): capture(character,version,.02)
        frames=[]; traces=[]; moving=[]; closeups=[]
        count = round(args.seconds*50)
        for frame in range(count):
            board = Image.new('RGB',(480,360)); trace_board = Image.new('RGB',(960,720)); move_board = Image.new('RGB',(480,360))
            states=[]; time=frame/50
            for character in range(2):
                character_states=[]
                for version in range(2):
                    state = capture(character,version,.02 if frame else 0); character_states.append(state)
                    samples.append({'frame':frame,'character':character,'version':version,'feet':state[2],
                                    'stride':state[3][0],'phase':state[3][3],'offset':state[1]})
                    if not args.stills_only:
                        view = panel(state,character,version,time)
                        board.paste(view.resize((240,180),Image.Resampling.BICUBIC),(version*240,character*180))
                        trace_board.paste(panel(state,character,version,time,trace=True),(version*480,character*360))
                        move_board.paste(panel(state,character,version,time,moving=True).resize((240,180),Image.Resampling.BICUBIC),(version*240,character*180))
                states.append(character_states)
            if not args.stills_only:
                frames.append(board); traces.append(trace_board); moving.append(move_board)
            # Sixteen evenly spaced actual samples across the first full walk
            # cycle; labels record the sample time, so interpolation is visible.
            duration = 24/args.speed
            if frame in {round(n*duration*50/16) for n in range(16)}:
                closeups.append((time,states))
        for character in range(2):
            sheet = Image.new('RGB',(4*360,8*145),'#edf0f4')
            for version in range(2):
                for index,(time,states) in enumerate(closeups[:16]):
                    view = panel(states[character][version],character,version,time,zoom=3)
                    crop = view.crop((150,300,510,435+10))
                    ImageDraw.Draw(crop).text((5,2),('v2.0.4' if version==0 else 'working')+f' t={time:.2f}',fill='#24394d')
                    sheet.paste(crop,((index%4)*360,(version*4+index//4)*145))
            sheet.save(output/(('cream','silver')[character]+'-walk-feet.png'))
        if not args.stills_only:
            save_gif(frames,output/'walk-real-time.gif',20)
            save_gif(frames,output/'walk-slow-4x.gif',80)
            save_gif(traces,output/'walk-traced-slow-4x.gif',80)
            save_gif(moving,output/'walk-root-travel.gif',20)
        timeline = Image.new('RGB',(1060,300),'white'); pen = ImageDraw.Draw(timeline)
        for version in range(2):
            pen.text((8,8+version*150),'v2.0.4' if version==0 else 'working source',fill='#24394d')
            for leg,name in enumerate(LEG_NAMES):
                y = 30+version*150+leg*25
                pen.text((8,y),name,fill=COLORS[leg])
                for sample in samples:
                    if sample['character']!=0 or sample['version']!=version: continue
                    x = 100+sample['frame']/count*950
                    if sample['feet'][leg][2]:pen.rectangle((x,y,x+950/count,y+15),fill=COLORS[leg])
        timeline.save(output/'stance-timeline.png')
        legend = dict(zip(LEG_NAMES,COLORS))
        manifest = {'renderer':'Production persistent walk C++; Pillow final placement, not Windows/GDI+',
                    'baseline_commit':commit,'speed_px_s':args.speed,'fps':50,'warmup_seconds':1.5,
                    'seconds':args.seconds,'phase_offsets':dict(zip(('v2.0.4','working'),offsets)),
                    'leg_order':LEG_NAMES,'trace_colors':legend,
                    'source_sha256':{str(Path('src')/h):hashlib.sha256((snapshots[1]/h).read_bytes()).hexdigest() for h in HEADERS},
                    'artwork_sha256':{name:hashlib.sha256((ROOT/'assets'/name).read_bytes()).hexdigest() for name in ('cat.png','cat-silver.png')}}
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        (output/'contact-samples.json').write_text(json.dumps(samples,indent=2)+'\n')
        print(json.dumps({'output':str(output),'phase_offsets':manifest['phase_offsets']}))
    finally:
        for version,lib in enumerate(libs):
            for rig in rigs[version]: lib.gait_review_destroy(rig)


if __name__=='__main__':
    main()
