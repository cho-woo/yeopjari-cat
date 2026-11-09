#!/usr/bin/env python3
"""Show current complete action drawings selected by the actual C++ sampler.

This is a diagnostic composition at the app's 110px reference size, not a
Windows desktop recording. It changes no source pixels or per-frame scale.
"""
from pathlib import Path
import argparse
import csv
import json
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw

from import_care_sprites import generated, load_entries

ROOT = Path(__file__).resolve().parents[1]
FPS, SECONDS = 60, 7


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/action-continuity')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    manifest = ROOT/'assets/care-sprites/manifest.json'
    entries = load_entries(manifest)
    with tempfile.TemporaryDirectory() as temporary:
        temp = Path(temporary)
        (temp/'action-assets.hpp').write_bytes(generated(entries)[0])
        (temp/'probe.cpp').write_text(r'''
#include "action-assets.hpp"
#include "cat_animation.hpp"
#include <iostream>
int main() {
    using namespace cat::sprite_sequence;
    for(unsigned tick=0;tick<420;++tick) {
        const auto ms=static_cast<std::uint64_t>(tick*1000./60);
        for(const auto clip:{Clip::Eat,Clip::Treat,Clip::Fish}) {
            const auto* asset=cat::sprite_assets::Find(clip,0,cat::sprite_assets::kCareSprites);
            const auto frame=SampleProgram(clip,asset->program,ms).frame;
            std::cout<<tick<<','<<static_cast<unsigned>(clip)<<','<<ms<<','<<frame<<'\n';
        }
        const auto* jump=cat::sprite_assets::Find(Clip::Jump,0,cat::sprite_assets::kCareSprites);
        const auto landing=cat::animation::LandingPose(ms,1,1);
        if(landing.spriteClip!=Clip::Jump) return 2;
        std::cout<<tick<<",6,"<<ms<<','<<SampleProgram(Clip::Jump,jump->program,0,landing.spriteOptions).frame<<'\n';
        const auto play=cat::animation::Evaluate(cat::animation::Motion::ToyPaw,ms,ms,1);
        if(play.spriteClip!=Clip::Punch || play.spriteTimed) return 3;
        std::cout<<tick<<",4,"<<ms<<','<<play.spriteFrame<<'\n';
    }
}
''')
        executable = temp/'probe'
        subprocess.run(['g++','-std=c++17','-O2',str(temp/'probe.cpp'),'-I',str(ROOT/'src'),'-o',str(executable)],check=True)
        trace = subprocess.check_output([str(executable)],text=True)
    (output/'trace.csv').write_text('tick,clip,elapsed_ms,source_frame\n'+trace)
    selected = {(int(t),int(c)):int(f) for t,c,_,f in csv.reader(trace.splitlines())}
    clips = {entry['clip']:entry for entry in entries}
    rows = [('eat',5,'Kibble'),('treat',7,'Treat'),('fish',3,'Fish'),
            ('jump',6,'Landing and recovery'),('punch',4,'Toy paw')]
    images = {}
    for name,_,_ in rows:
        entry=clips[name]
        pixels=np.array(Image.open(ROOT/'assets/care-sprites'/entry['filename']).convert('RGBA'))
        pixels[pixels[:,:,3]<=16]=0
        source=Image.fromarray(pixels)
        images[name]=[]
        for frame in entry['frames']:
            x,y,w,h=frame['rect']; scale=110/entry['reference_height']
            image=source.crop((x,y,x+w,y+h)).resize((round(w*scale),round(h*scale)),Image.Resampling.LANCZOS)
            images[name].append((image,frame['anchor'][0]*scale,frame['anchor'][1]*scale))
    video=output/'action-continuity.mp4'
    encoder=subprocess.Popen(['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24',
        '-s','1100x300','-r',str(FPS),'-i','-','-an','-c:v','libx264','-crf','19',
        '-pix_fmt','yuv420p','-movflags','+faststart',str(video)],stdin=subprocess.PIPE)
    for tick in range(FPS*SECONDS):
        board=Image.new('RGBA',(1100,300),'#f7f5ef');draw=ImageDraw.Draw(board)
        draw.text((20,14),'Same complete actor / fixed 110px reference / actual C++ action sampler',fill='#30414a')
        draw.text((20,274),'Diagnostic composition, not a Windows desktop recording',fill='#67777d')
        for column,(name,clip,label) in enumerate(rows):
            frame=selected[tick,clip];center=column*220+110;ground=235
            draw.text((column*220+18,51),label,fill='#30414a')
            draw.line((column*220+10,ground,column*220+210,ground),fill='#c7d0d2')
            image,ax,ay=images[name][frame];board.alpha_composite(image,(round(center-ax),round(ground-ay)))
            draw.text((column*220+18,247),f'{tick/FPS:.2f}s / drawing {frame}',fill='#67777d')
        rgb=board.convert('RGB')
        if tick in (0,40,80,160,320):rgb.save(output/f'frame-{tick:03}.png')
        encoder.stdin.write(rgb.tobytes())
    encoder.stdin.close()
    if encoder.wait():raise RuntimeError('ffmpeg encoding failed')
    subprocess.run(['ffmpeg','-v','error','-i',str(video),'-f','null','-'],check=True)
    (output/'preview.json').write_text(json.dumps({'fps':FPS,'seconds':SECONDS,'reference_height':110,
        'kind':'diagnostic composition, not a desktop recording','source_pixels_changed':False,
        'sampler':'src/cat_sprite_sequence.hpp + src/cat_animation.hpp'},indent=2)+'\n')
    print(video)


if __name__=='__main__':main()
