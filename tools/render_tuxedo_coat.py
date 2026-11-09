#!/usr/bin/env python3
"""Review exact C++ material on original RGBA; no pose or source-image edits."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BACKGROUND = '#eaeef3'

def fallback_frames():
    # Match LoadAtlasResource's fixed cells, transparent row gutters and alpha.
    for name, columns, count in [('cat.png',4,8), ('cat-actions.png',4,8),
            ('cream-extra.png',2,4), ('cream-gestures.png',2,4),
            ('cream-recline.png',2,2), ('cream-hang.png',1,1), ('cream-sleep.png',2,2)]:
        source = Image.open(ROOT/'assets'/name).convert('RGBA')
        data = np.array(source); data[data[:,:,3] <= 16] = 0
        source = Image.fromarray(data); rows = count//columns
        cuts = [i*source.height//rows for i in range(rows+1)]
        if columns == 2:
            for split in range(1, rows):
                target = split*source.height//rows; radius = source.height//rows//4
                cuts[split] = min(range(max(cuts[split-1]+1,target-radius),
                    min(source.height-(rows-split),target+radius)+1),
                    key=lambda y: (int((data[y,:,3]>16).sum()),abs(y-target)))
        for index in range(count):
            column, row = index%columns, index//columns
            frame = source.crop((column*source.width//columns,cuts[row],
                (column+1)*source.width//columns,cuts[row+1]))
            yield frame.crop(frame.getbbox())

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT/'build/tuxedo-coat')
    output = parser.parse_args().output; output.mkdir(parents=True, exist_ok=True)
    clips = json.loads((ROOT/'assets/care-sprites/manifest.json').read_text())['clips']
    cache = {}; fallback = []
    with tempfile.TemporaryDirectory() as temporary:
        executable = Path(temporary)/'coat'
        subprocess.run(['g++','-std=c++17','-O2',str(ROOT/'tools/coat_probe.cpp'),'-o',str(executable)],check=True)
        def material(image, clip, frame):
            data = np.array(image); data[data[:,:,3] <= 16] = 0
            original = Image.fromarray(data.copy()); alpha = data[:,:,3:4].astype(np.uint16)
            data[:,:,:3] = ((data[:,:,:3].astype(np.uint16)*alpha+127)//255).astype(np.uint8)
            raw = subprocess.check_output([str(executable),str(image.width),str(image.height),str(clip),str(frame)],input=data[:,:,[2,1,0,3]].tobytes())
            result = np.frombuffer(raw,dtype=np.uint8).reshape(image.height,image.width,4).copy()[:,:,[2,1,0,3]]
            assert np.array_equal(data[:,:,3],result[:,:,3]), (clip,frame,'alpha changed')
            assert np.all(result[:,:,:3].astype(np.uint16) <= alpha), (clip,frame,'invalid premultiplication')
            result[:,:,:3] = np.minimum(255,(result[:,:,:3].astype(np.uint16)*255+alpha//2)//np.maximum(1,alpha)).astype(np.uint8)
            return original, Image.fromarray(result)
        for index, clip in enumerate(clips):
            source = Image.open(ROOT/'assets/care-sprites'/clip['source']).convert('RGBA')
            for frame, metadata in enumerate(clip['frames']):
                x,y,width,height = metadata['rect']
                pair = material(source.crop((x,y,x+width,y+height)), index if index<8 else index+1, frame)
                pair[1].save(output/f'{clip["clip"]}-{frame:02}.png'); cache[(clip['clip'],frame)] = pair
        for frame, image in enumerate(fallback_frames()):
            fallback.append(material(image,8,frame)); fallback[-1][1].save(output/f'fallback-{frame:02}.png')
    def paste(board,clip,frame,coat,center,ground,height=110):
        metadata = clip['frames'][frame]; image = cache[(clip['clip'],frame)][coat]
        scale = height/clip['reference_height']
        image = image.resize((round(image.width*scale),round(image.height*scale)),Image.Resampling.LANCZOS)
        board.alpha_composite(image,(round(center-metadata['anchor'][0]*scale),round(ground-metadata['anchor'][1]*scale)))
    board = Image.new('RGBA',(1080,1600),BACKGROUND); draw = ImageDraw.Draw(board)
    for row,clip in enumerate(clips):
        for column,frame in enumerate([0,clip['frame_count']//2,clip['frame_count']-1]):
            for coat in [0,1]:
                x = column*360+coat*180
                draw.text((x+10,row*160+8),f'{clip["clip"]} {frame} / {"Tuxedo" if coat else "Cream"}',fill='#263340')
                paste(board,clip,frame,coat,x+85,row*160+145)
    board.convert('RGB').save(output/'contact-110.png')
    rows = sum((clip['frame_count']+7)//8 for clip in clips)
    allframes = Image.new('RGBA',(1280,rows*155),BACKGROUND); draw = ImageDraw.Draw(allframes); row=0
    for clip in clips:
        for frame in range(clip['frame_count']):
            x = (frame%8)*160; y = (row+frame//8)*155
            draw.text((x+8,y+8),f'{clip["clip"]} {frame}',fill='#263340')
            paste(allframes,clip,frame,1,x+78,y+140)
        row += (clip['frame_count']+7)//8
    allframes.convert('RGB').save(output/'all-frames-110.png')
    board = Image.new('RGBA',(1280,8*160),BACKGROUND); draw = ImageDraw.Draw(board)
    for frame,pair in enumerate(fallback):
        for coat,image in enumerate(pair):
            x = (frame%4)*320+coat*160; y=(frame//4)*160
            draw.text((x+8,y+8),f'{frame} / {"Tuxedo" if coat else "Cream"}',fill='#263340')
            scale=110/image.height; image=image.resize((round(image.width*scale),110),Image.Resampling.LANCZOS)
            board.alpha_composite(image,(round(x+80-image.width/2),y+145-image.height))
    board.convert('RGB').save(output/'fallback-110.png')
    for filename in ['contact-110.png','all-frames-110.png','fallback-110.png']:
        print(output/filename)

if __name__ == '__main__':
    main()
