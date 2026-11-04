#!/usr/bin/env python3
"""Portable developer preview using production C++ Evaluate and Warp via ctypes.

Requires Pillow only for developer raster previews. Source PNGs are read-only.
This is NOT Win32 execution: GDI+ bicubic sampling, outfit drawing, Box2D and
window/state integration are outside this preview. Crops, reference scales,
premultiplied blends, local joints and 1/32 joint quantization match main.cpp.
"""
from __future__ import annotations
import argparse
import ctypes
import functools
import hashlib
import json
import math
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build' / 'previews'
MOTIONS = {'ear': (14, 2200), 'belly': (16, 4300), 'knead': (18, 5000), 'loaf': (17, 3000), 'scratch': (20, 2000)}
ALL_MOTIONS = ['Idle','Walk','Sleep','Eat','Groom','Stretch','Watch','Stalk','Pounce','Zoomies','TailChase','Punch','Jump','Treat','EarFlick','TailRaise','BellyRoll','Loaf','Knead','Peek','Scratch','Sneeze','ToyPaw','BoxSit']
ZOOM = 2
SIZE = (520, 420)
GROUND = 368


def bridge():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    library = OUTPUT / 'motion_preview.so'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-shared','-fPIC',
                    str(ROOT/'tools/motion_preview.cpp'),'-o',str(library)],check=True)
    lib = ctypes.CDLL(str(library))
    doubles = ctypes.POINTER(ctypes.c_double)
    lib.preview_evaluate.argtypes = [ctypes.c_int,ctypes.c_ulonglong,ctypes.c_int,doubles]
    lib.preview_evaluate.restype = ctypes.c_int
    lib.preview_warp.argtypes = [ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,doubles,ctypes.c_int]
    lib.preview_warp.restype = ctypes.c_int
    lib.preview_mask.argtypes = [ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    lib.preview_mask.restype = ctypes.c_int
    lib.preview_bone.argtypes = [ctypes.c_int,ctypes.c_double,ctypes.c_double,doubles]
    lib.preview_blend_bone.argtypes = [ctypes.c_int,ctypes.c_double,ctypes.c_double,
                                     ctypes.c_int,ctypes.c_double,ctypes.c_double,ctypes.c_double,doubles]
    lib.preview_clearance.argtypes = [ctypes.c_double,ctypes.c_double,ctypes.c_double,ctypes.c_int]
    lib.preview_clearance.restype = ctypes.c_double
    return lib


def atlas(name, columns=4, count=8):
    image = Image.open(ROOT/'assets'/name).convert('RGBA')
    alpha = image.getchannel('A')
    mask = alpha.point(lambda value: 255 if value > 16 else 0)
    # Match runtime cleanup: fully zero all channels at alpha <=16.
    cleaned = Image.new('RGBA',image.size)
    cleaned.paste(image,(0,0),mask)
    width,height = image.size
    rows = count//columns
    cuts = [row*height//rows for row in range(rows+1)]
    if columns == 2:
        for split in range(1,rows):
            target,radius = split*height//rows,height//rows//4
            choices = []
            for y in range(max(cuts[split-1]+1,target-radius),min(height-(rows-split),target+radius)+1):
                occupied = mask.crop((0,y,width,y+1)).histogram()[255]
                choices.append((occupied,abs(y-target),y))
            cuts[split] = min(choices)[2]
    frames,rectangles = [],[]
    for index in range(count):
        col,row = index%columns,index//columns
        x0,x1 = col*width//columns,(col+1)*width//columns
        y0,y1 = cuts[row],cuts[row+1]
        bounds = mask.crop((x0,y0,x1,y1)).getbbox()
        if not bounds:
            raise RuntimeError(f'Empty frame {name}:{index}')
        left,top,right,bottom = bounds
        box = (max(x0,x0+left-2),max(y0,y0+top-2),min(x1,x0+right+2),min(y1,y0+bottom+2))
        frames.append(cleaned.crop(box))
        rectangles.append(box)
    return frames,rectangles


class Renderer:
    def __init__(self):
        self.lib = bridge()
        self.sheets = {}
        self.rectangles = {}
        for name,columns,count in [('cat.png',4,8),('cat-silver.png',4,8),('cat-actions.png',4,8),
                ('cat-silver-actions.png',4,8),('cat-jump-churu.png',2,8),('cat-gestures.png',2,8),('cat-recline.png',2,4)]:
            self.sheets[name],self.rectangles[name] = atlas(name,columns,count)
        (OUTPUT/'atlas-crops.json').write_text(json.dumps(self.rectangles,indent=2)+'\n')
        sources = ['src/cat_animation.hpp','src/cat_deformation.hpp','src/cat_render_geometry.hpp','src/cat_belly_rig.hpp',
                   'tools/motion_preview.cpp'] + ['assets/'+name for name in self.sheets]
        provenance = {'renderer':'Portable C++ Evaluate/Warp + Pillow; not Win32/GDI+',
                      'zoom':ZOOM,'joint_quantization':32,'outfits':False,'physics':False,
                      'sources_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}}
        (OUTPUT/'preview-info.json').write_text(json.dumps(provenance,indent=2)+'\n')

    def pose(self,motion,elapsed,direction=1):
        data = (ctypes.c_double*20)()
        if not self.lib.preview_evaluate(motion,elapsed,direction,data):
            raise RuntimeError('C++ Evaluate rejected input')
        return tuple(data)

    def sprite(self,character,frame):
        base = self.sheets['cat.png' if character==0 else 'cat-silver.png']
        if frame>=24:
            return self.sheets['cat-recline.png'][character*2+frame-24],110/440
        if frame>=20:
            sheet=self.sheets['cat-gestures.png']
            height=max(sheet[character*4].height,sheet[character*4+1].height)
            return sheet[character*4+frame-20],110/height
        if frame>=16:
            sheet=self.sheets['cat-jump-churu.png']
            height=max(sheet[character*4+2].height,sheet[character*4+3].height)
            return sheet[character*4+frame-16],110/height
        if frame>=8:
            sheet=self.sheets['cat-actions.png' if character==0 else 'cat-silver-actions.png']
            return sheet[frame-8],110/sheet[6].height
        return base[frame],110/max(x.height for x in base[:6])

    @functools.lru_cache(maxsize=512)
    def skin(self,character,frame,joints,part=0):
        source,_ = self.sprite(character,frame)
        premultiplied=source.convert('RGBa')
        if frame not in (4,6,7,8,9,10,15,16,18,24,25):
            return premultiplied
        data=premultiplied.tobytes('raw','BGRa')
        source_bytes=ctypes.create_string_buffer(data,len(data))
        destination=ctypes.create_string_buffer(len(data))
        values=(ctypes.c_double*8)(*joints)
        shape=3 if frame==16 else 4 if frame==7 else 5 if frame in (9,10) else 6 if frame==18 else 7 if frame==8 else 8 if frame==15 else 2 if frame==25 else 1 if frame in (6,24) else 0
        if not self.lib.preview_warp(source_bytes,source.width,source.height,destination,values,shape):
            raise RuntimeError('Production Warp rejected source crop')
        if part:
            masked=ctypes.create_string_buffer(len(data))
            if not self.lib.preview_mask(destination,source.width,source.height,masked,frame,int(part==2)):
                raise RuntimeError('Production head/body mask rejected source')
            destination=masked
        return Image.frombytes('RGBa',source.size,destination.raw,'raw','BGRa')

    def layer(self,character,frame,pose):
        source,scale=self.sprite(character,frame)
        skin=self.skin(character,frame,pose[11:19],1 if pose[19] else 0)
        width=source.width*scale*pose[3]*ZOOM
        height=source.height*scale*pose[4]*ZOOM
        angle=pose[5]
        center=SIZE[0]/2+pose[1]*ZOOM
        bottom=GROUND-pose[2]*ZOOM
        clearance=self.lib.preview_clearance(width,height,angle,int(pose[7])) if pose[10] else 0
        pivot_y=bottom-clearance-(height/2 if pose[7] else 0)
        radians=angle*math.pi/180
        cosine,sine=math.cos(radians),math.sin(radians)
        mirror=-1 if pose[6] else 1
        # Inverse of production translate -> rotate -> mirror transform.
        ax=mirror*cosine*source.width/width
        bx=mirror*sine*source.width/width
        cx=source.width/2-ax*center-bx*pivot_y
        ay=-sine*source.height/height
        by=cosine*source.height/height
        cy=(source.height/2 if pose[7] else source.height)-ay*center-by*pivot_y
        return skin.transform(SIZE,Image.Transform.AFFINE,(ax,bx,cx,ay,by,cy),Image.Resampling.BICUBIC)

    def head_layer(self,character,pose):
        # One canonical head follows the body bones. The source face never fades.
        source,scale=self.sprite(character,4)
        skin=self.skin(character,4,pose[11:14]+(0.0,)*5,2)
        first_frame=int(pose[0])
        second_frame=int(pose[8]) if pose[8]>=0 else first_frame
        first,first_scale=self.sprite(character,first_frame)
        second,second_scale=self.sprite(character,second_frame)
        bone=(ctypes.c_double*3)()
        self.lib.preview_blend_bone(first_frame,first.width*first_scale*pose[3]*ZOOM,
            first.height*first_scale*pose[4]*ZOOM,second_frame,second.width*second_scale*pose[3]*ZOOM,
            second.height*second_scale*pose[4]*ZOOM,pose[9],bone)
        source_bone=(ctypes.c_double*3)()
        self.lib.preview_bone(4,source.width,source.height,source_bone)
        anchor_x=source.width/2+source_bone[0]
        anchor_y=source.height+source_bone[1]
        scale_x=scale*ZOOM*pose[3]
        scale_y=scale*ZOOM*pose[4]
        center=SIZE[0]/2+pose[1]*ZOOM
        bottom=GROUND-pose[2]*ZOOM
        cosine,sine=math.cos(math.radians(bone[2])),math.sin(math.radians(bone[2]))
        mirror=-1 if pose[6] else 1
        ax=mirror*cosine/scale_x;bx=sine/scale_x
        ay=-mirror*sine/scale_y;by=cosine/scale_y
        cx=anchor_x-ax*center-bx*bottom-(cosine*bone[0]+sine*bone[1])/scale_x
        cy=anchor_y-ay*center-by*bottom-(-sine*bone[0]+cosine*bone[1])/scale_y
        return skin.transform(SIZE,Image.Transform.AFFINE,(ax,bx,cx,ay,by,cy),Image.Resampling.BICUBIC)

    def render(self,character,motion,elapsed,label):
        pose=self.pose(motion,elapsed)
        first=self.layer(character,int(pose[0]),pose)
        if pose[8]>=0 and pose[9]>0:
            second=self.layer(character,int(pose[8]),pose)
            weight=math.floor(min(1,max(0,pose[9]))*255+.5)
            first=Image.blend(first,second,weight/255)
        background=Image.new('RGBA',SIZE,(237,240,244,255))
        draw=ImageDraw.Draw(background)
        draw.line((0,GROUND,SIZE[0],GROUND),fill=(100,114,130,255),width=2)
        draw.text((12,12),f'{"Cream" if character==0 else "Silver"} | {label} | {elapsed} ms',fill=(30,40,50))
        draw.text((12,390),'2x zoom | production C++ pose/mesh | portable preview',fill=(65,75,85))
        background.alpha_composite(first.convert('RGBA'))
        if pose[19]:background.alpha_composite(self.head_layer(character,pose).convert('RGBA'))
        return background.convert('RGB')


def contact(renderer,name,times):
    motion,_=MOTIONS[name]
    sheet=Image.new('RGB',(SIZE[0]*len(times),SIZE[1]*2),(255,255,255))
    for char in range(2):
        for col,time in enumerate(times):
            sheet.paste(renderer.render(char,motion,time,name),(col*SIZE[0],char*SIZE[1]))
    target=OUTPUT/f'{name}-contact.png'
    sheet.save(target)
    print(target,flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quick',action='store_true',help='Render only ear/belly contact sheets')
    parser.add_argument('--fps',type=int,default=20)
    parser.add_argument('--all-stills',action='store_true')
    args=parser.parse_args()
    if args.fps<1 or args.fps>60:parser.error('--fps must be 1..60')
    renderer=Renderer()
    contact(renderer,'ear',[0,310,1110,1800])
    contact(renderer,'belly',[0,800,1900,3500])
    if args.quick:return
    contact(renderer,'knead',[0,210,630,1050])
    contact(renderer,'loaf',[0,175,350,1500])
    contact(renderer,'scratch',[0,180,800,1400])
    for name,(motion,duration) in MOTIONS.items():
        frames=[]
        for elapsed in range(0,duration+1,max(1,1000//args.fps)):
            frame=Image.new('RGB',(SIZE[0]*2,SIZE[1]))
            for char in range(2):
                frame.paste(renderer.render(char,motion,elapsed,name),(char*SIZE[0],0))
            frames.append(frame)
        path=OUTPUT/f'{name}.gif'
        frames[0].save(path,save_all=True,append_images=frames[1:],loop=0,duration=1000//args.fps,disposal=2)
        print(path,flush=True)
    if args.all_stills:
        for char in range(2):
            sheet=Image.new('RGB',(SIZE[0]*4,SIZE[1]*6),(255,255,255))
            for motion,name in enumerate(ALL_MOTIONS):
                sheet.paste(renderer.render(char,motion,800,name),((motion%4)*SIZE[0],(motion//4)*SIZE[1]))
            path=OUTPUT/f'all-motions-{char}.png';sheet.save(path);print(path,flush=True)


if __name__=='__main__':main()
