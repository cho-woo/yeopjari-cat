#!/usr/bin/env python3
"""Read-only artwork inspection using the production C++ expression sampler.

Composites test results on a contact sheet; this is not GDI+/Win32 validation.
"""
from pathlib import Path
import ctypes
import subprocess
from PIL import Image, ImageDraw
from render_motion_preview import atlas

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/previews/expressions'

def companion_crops(cell=0):
    im=Image.open(ROOT/'assets/companion-poses.png').convert('RGBA')
    mask=im.getchannel('A').point(lambda a:255 if a>16 else 0)
    clean=Image.new('RGBA',im.size);clean.paste(im,(0,0),mask)
    w,h=im.size;rows=[0]
    for split in range(1,8):
        target,radius=split*h//8,h//32
        rows.append(min(range(target-radius,target+radius+1),key=lambda y:(mask.crop((0,y,w,y+1)).histogram()[255],abs(y-target))))
    rows.append(h)
    result=[]
    for row in range(8):
        y0,y1=rows[row:row+2];cuts=[0]
        for split in range(1,8):
            target,radius=split*w//8,w//8//5
            cuts.append(min(range(target-radius,target+radius+1),key=lambda x:(mask.crop((x,y0,x+1,y1)).histogram()[255],abs(x-target))))
        cuts.append(w);x0,x1=cuts[cell:cell+2]
        l,t,r,b=mask.crop((x0,y0,x1,y1)).getbbox()
        result.append(clean.crop((max(x0,x0+l-2),max(y0,y0+t-2),min(x1,x0+r+2),min(y1,y0+b+2))))
    return result

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    binary=OUT/'expression_preview.so'
    subprocess.run(['g++','-std=c++17','-O2','-shared','-fPIC',str(ROOT/'tools/expression_preview.cpp'),'-o',str(binary)],check=True)
    lib=ctypes.CDLL(str(binary));fn=lib.expression_preview
    fn.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_int];fn.restype=ctypes.c_int
    crops=[atlas('cat.png')[0][4],atlas('cat-silver.png')[0][4]]+companion_crops()
    lids=[atlas('cat.png')[0][5],atlas('cat-silver.png')[0][5]]+companion_crops(5)
    sheet=Image.new('RGB',(1250,2460),'#e6e9ee');draw=ImageDraw.Draw(sheet)
    labels=['Cream','Silver','Black','Calico','Siamese','Munchkin','Dog','Rabbit','Tuxedo','British']
    for c,crop in enumerate(crops):
        raw=bytearray(crop.tobytes('raw','BGRA'))
        for p in range(0,len(raw),4):
            for channel in range(3):raw[p+channel]=(raw[p+channel]*raw[p+3]+127)//255
        source=ctypes.create_string_buffer(bytes(raw));destination=ctypes.create_string_buffer(len(raw))
        rawLid=bytearray(lids[c].tobytes('raw','BGRA'))
        for p in range(0,len(rawLid),4):
            for channel in range(3):rawLid[p+channel]=(rawLid[p+channel]*rawLid[p+3]+127)//255
        closed=ctypes.create_string_buffer(bytes(rawLid))
        for e,name in enumerate(['neutral','blink','yawn','pride','startled']):
            assert fn(source,crop.width,crop.height,destination,c,e,closed,lids[c].width,lids[c].height)
            result=bytearray(destination.raw)
            for p in range(0,len(result),4):
                alpha=result[p+3]
                for channel in range(3):result[p+channel]=min(255,(result[p+channel]*255+alpha//2)//alpha) if alpha else 0
            im=Image.frombytes('RGBA',crop.size,bytes(result),'raw','BGRA')
            height=210;width=round(im.width*height/im.height)
            im=im.resize((width,height),Image.Resampling.LANCZOS)
            x=e*250+(250-width)//2;y=c*246+30
            sheet.paste(im,(x,y),im);draw.text((e*250+8,c*246+8),f'{labels[c]} / {name}',fill='#202a36')
    path=OUT/'all-species-expressions.png';sheet.save(path);print(path)

if __name__=='__main__':main()
