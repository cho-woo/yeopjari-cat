#!/usr/bin/env python3
"""Review the actual C++ coat material and shared sequence sampler (not a desktop recording)."""
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]

def main():
    clips=json.loads((ROOT/'assets/care-sprites/manifest.json').read_text())['clips']
    output=ROOT/'docs/media';output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        exe=Path(temporary)/'coat'
        subprocess.run(['g++','-std=c++17','-O2',str(ROOT/'tools/coat_probe.cpp'),'-o',str(exe)],check=True)
        cache={}
        for row,c in enumerate(clips):
            sheet=Image.open(ROOT/'assets/care-sprites'/c['source'])
            for frame,f in enumerate(c['frames']):
                x,y,w,h=f['rect'];cream=sheet.crop((x,y,x+w,y+h));a=np.array(cream)
                a[a[:,:,3]<=16]=0;cream=Image.fromarray(a)
                a[:,:,:3]=((a[:,:,:3].astype('uint16')*a[:,:,3:4]+127)//255).astype('uint8')
                raw=subprocess.check_output([str(exe),str(w),str(h),str(row if row<8 else row+1),str(frame)],input=a[:,:,[2,1,0,3]].tobytes())
                b=np.frombuffer(raw,dtype='uint8').reshape(h,w,4).copy()[:,:,[2,1,0,3]]
                alpha=b[:,:,3:4].astype('uint16');b[:,:,:3]=np.minimum(255,(b[:,:,:3].astype('uint16')*255+alpha//2)//np.maximum(1,alpha))
                cache[(c['clip'],frame)]=(cream,Image.fromarray(b))
        # Side-by-side registration uses the same source referenceHeight/anchor.
        def paste(canvas,c,frame,coat,center,floor,height=120):
            im=cache[(c['clip'],frame)][coat];f=c['frames'][frame];scale=height/c['reference_height']
            im=im.resize((round(im.width*scale),round(im.height*scale)),Image.Resampling.LANCZOS)
            canvas.alpha_composite(im,(round(center-f['anchor'][0]*scale),round(floor-f['anchor'][1]*scale)))
        board=Image.new('RGBA',(1200,2100),(235,239,243,255));draw=ImageDraw.Draw(board)
        for row,c in enumerate(clips):
            for pair,frame in enumerate([0,c['frame_count']//2,c['frame_count']-1]):
                for coat in range(2):
                    left=pair*400+coat*200;paste(board,c,frame,coat,left+100,row*210+190)
                    draw.text((left+8,row*210+12),f'{"Cream" if coat==0 else "Tuxedo"} / {c["clip"]} {frame}',fill='#263340')
        board.convert('RGB').save(output/'two-cats-material.png')
        # Query the production C++ program, avoiding a guessed frame ordering.
        sampler=Path(temporary)/'sample.cpp';binary=Path(temporary)/'sample'
        sampler.write_text('#include "care_sprites.generated.hpp"\n#include <iostream>\nint main(){using namespace cat::sprite_sequence;for(unsigned t=0;t<12000;t+=50){auto clip=t<4000?Clip::Walk:t<5000?Clip::Sleep:Clip::Groom;auto* a=cat::sprite_assets::Find(clip,0,cat::sprite_assets::kCareSprites);Options o;o.stage=Stage::Looping;o.cyclePhase=t/2000.;auto f=SampleProgram(clip,a->program,t<5000?0:t-5000,o);std::cout<<t<<" "<<static_cast<int>(clip)<<" "<<(t>=4000&&t<5000?0:f.frame)<<"\\n";}}')
        subprocess.run(['g++','-std=c++17','-O2','-I',str(ROOT/'src'),str(sampler),'-o',str(binary)],check=True)
        samples=subprocess.check_output([str(binary)],text=True).splitlines();images=[]
        by_id={i if i<8 else i+1:c for i,c in enumerate(clips)}
        for line in samples:
            t,clip,frame=map(int,line.split());c=by_id[clip]
            canvas=Image.new('RGBA',(640,300),(235,239,243,255));d=ImageDraw.Draw(canvas)
            d.text((25,18),'v2.0.15 / v2.0.13 playback + two coats',fill='#263340')
            d.line((0,260,640,260),fill='#b1b8c2');d.text((90,45),'Cream',fill='#263340');d.text((410,45),'Tuxedo',fill='#263340')
            for coat in range(2):paste(canvas,c,frame,coat,150+coat*320+(t/4000.*55 if t<4000 else 55),260,140)
            images.append(canvas.convert('RGB'))
        images[0].save(output/'two-cats.gif',save_all=True,append_images=images[1:],duration=50,loop=0,disposal=2,optimize=False)
        subprocess.run(['ffmpeg','-y','-v','error','-i',str(output/'two-cats.gif'),'-pix_fmt','yuv420p','-movflags','+faststart',str(output/'two-cats.mp4')],check=True)
    print(output/'two-cats.gif')

if __name__=='__main__':main()
