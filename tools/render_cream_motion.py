#!/usr/bin/env python3
"""Portable production C++ rig review. Not a Windows runtime recording."""
import ctypes, json, subprocess, time
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/cream-motion'
OUT.mkdir(parents=True,exist_ok=True)
objects=[]
for source in sorted((ROOT/'third_party/box2d/src').glob('*.c')):
    target=OUT/(source.stem+'.o');objects.append(str(target))
    if not target.exists() or target.stat().st_mtime<source.stat().st_mtime:
        subprocess.run(['gcc','-std=c17','-O2','-fPIC','-DNDEBUG','-D_POSIX_C_SOURCE=200809L',
            '-ffp-contract=off','-mno-avx','-mno-avx2','-Ithird_party/box2d/include',
            '-Ithird_party/box2d/src','-c',str(source),'-o',str(target)],cwd=ROOT,check=True)
assert objects, 'Run the repository dependency setup first (Box2D is required).'
subprocess.run(['ar','rcs',str(OUT/'box2d.a'),*objects],check=True)
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-Isrc',
    'tools/cream_motion_probe.cpp','src/cat_physics.cpp','-Ithird_party/box2d/include',
    str(OUT/'box2d.a'),'-o',str(OUT/'probe.so')],cwd=ROOT,check=True)
lib=ctypes.CDLL(str(OUT/'probe.so'));p=ctypes.c_void_p;d=ctypes.c_double;i=ctypes.c_int
for name,args,result in [('create',[p,i,i],p),('destroy',[p],None),('step',[p,d,d,i],i),
    ('pose',[p,d],i),('pixels',[p],p),('sit',[p],d),('distance',[p],d)]:
    f=getattr(lib,'cream_'+name);f.argtypes=args;f.restype=result
parts=Image.open(ROOT/'assets/cream-rig-parts.png').convert('RGBa')
partsbuf=ctypes.create_string_buffer(parts.tobytes('raw','BGRa'))
rig=lib.cream_create(partsbuf,parts.width,parts.height)
assert rig
def drawing(label,travel=0,zoom=2):
    raw=ctypes.string_at(lib.cream_pixels(rig),377*337*4)
    # Native raster is premultiplied BGRA; unpremultiply only for PNG export.
    img=Image.frombytes('RGBa',(377,337),raw,'raw','BGRa').convert('RGBA')
    bounds=img.getchannel('A').point(lambda a:255 if a>32 else 0).getbbox()
    assert bounds and bounds[0]>0 and bounds[1]>0 and bounds[2]<377 and bounds[3]<337,bounds
    assert bounds[3]<=302,('below floor',bounds)
    scale=110/221*zoom
    img=img.resize((round(377*scale),round(337*scale)),Image.Resampling.BICUBIC)
    canvas=Image.new('RGBA',(280*zoom,190*zoom),'#edf0f4');draw=ImageDraw.Draw(canvas)
    ground=160*zoom
    draw.line((0,ground,280*zoom,ground),fill='#72838c')
    for x in range(-20,310,10):
        xx=(x-travel%10)*zoom;draw.line((xx,ground,xx,ground+4*zoom),fill='#9aa8b1')
    canvas.alpha_composite(img,(round(140*zoom-193*scale),round(ground-299*scale)))
    draw.text((10,10),label,fill='#24343d')
    return canvas.convert('RGB')
poses=[]
for sit in (0,.2,.4,.6,.8,1):
    assert lib.cream_pose(rig,sit);poses.append(drawing(f'sit={sit:.1f}'))
sheet=Image.new('RGB',(560*3,380*2),'white')
for n,img in enumerate(poses):sheet.paste(img,(n%3*560,n//3*380))
sheet.save(OUT/'postures.png')
frames=[];samples=[]
for n in range(480):
    seconds=n/60;moving=1<=seconds<5.5;speed=22 if moving else 0
    assert lib.cream_step(rig,1/60,speed,int(moving))
    if n%2==0:frames.append(drawing(f'{seconds:.2f}s  sit={lib.cream_sit(rig):.2f}',lib.cream_distance(rig)))
    if n in (90,108,126,144,162,180,198,216):samples.append(frames[-1])
frames[0].save(OUT/'walk-stand-sit.gif',save_all=True,append_images=frames[1:],duration=[30,30,40]*(len(frames)//3),loop=0)
sheet=Image.new('RGB',(560*4,380*2),'white')
for n,img in enumerate(samples):sheet.paste(img,(n%4*560,n//4*380))
sheet.save(OUT/'walking.png')
start=time.process_time()
for _ in range(300):assert lib.cream_step(rig,1/60,22,1)
mean_ms=(time.process_time()-start)*1000/300
lib.cream_destroy(rig)
report={'portable_physics_and_raster_ms_mean':round(mean_ms,3),'windows_runtime_measured':False}
(OUT/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'preview':str(OUT),'rendered_frames':480,'cutout_parts':5, 'new_authored_animation_frames':0,'renderer':'production C++ rig and Box2D, portable export'}))
