#!/usr/bin/env python3
"""Encode actual Windows GDI+ diagnostic frames; never synthesize new poses."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('archive',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/media')
    parser.add_argument('--prefix',default='walking-native')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.archive) as archive, tempfile.TemporaryDirectory() as temporary:
        names=sorted(n for n in archive.namelist() if Path(n).name.startswith('dpi100-') and n.endswith('.png'))
        assert len(names)==210, len(names)
        frames=[]
        for index,name in enumerate(names):
            assert Path(name).name==f'dpi100-{10000+index}.png'
            data=archive.read(name);image=Image.open(io.BytesIO(data));assert image.size==(720,510)
            frames.append(image.convert('RGB'));Path(temporary,f'frame-{index:05d}.png').write_bytes(data)
        frames[0].save(args.output/(args.prefix+'.gif'),save_all=True,append_images=frames[1:],duration=50,loop=0,disposal=2,optimize=False)
        subprocess.run(['ffmpeg','-y','-v','error','-framerate','20','-i',str(Path(temporary,'frame-%05d.png')),
                        '-c:v','libx264','-pix_fmt','yuv420p','-movflags','+faststart',str(args.output/(args.prefix+'.mp4'))],check=True)
        duration=float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','default=noprint_wrappers=1:nokey=1',str(args.output/(args.prefix+'.mp4'))],text=True))
        assert abs(duration-10.5)<.01
        for dpi in (100,125,150):
            sample=next(n for n in archive.namelist() if Path(n).name==f'dpi{dpi}-10096.png')
            (args.output/f'{args.prefix}-dpi-{dpi}.png').write_bytes(archive.read(sample))
        trace=next(n for n in archive.namelist() if n.endswith('ground-drive/trace.csv'))
        (args.output/(args.prefix+'-trace.csv')).write_bytes(archive.read(trace))
        proof={'frames':len(names),'review_fps':20,'physics_hz':60,'duration_seconds':duration,
               'source':'Windows GDI+ / Box2D offscreen diagnostic, not a desktop screen recording',
               'before':'v2.0.16 Current diagnostic stop-wait omission reproduced with identical original8 PNGs; not the old executable',
               'after':'shipping BeginGroundStop / AdvanceGroundStop / DisplayedMotion / action clock',
               'new_accepted_art_frames':0,
               'source_archive_sha256':hashlib.sha256(args.archive.read_bytes()).hexdigest()}
        (args.output/(args.prefix+'.json')).write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof))

if __name__=='__main__':main()
