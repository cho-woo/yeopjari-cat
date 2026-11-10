#!/usr/bin/env python3
"""Make an explicitly offline, fixed-scale original/Tuxedo video comparison."""
from __future__ import annotations
import argparse,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--matte-dir',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    p.add_argument('--indices',required=True,help='Comma-separated zero-based source indices.')
    p.add_argument('--repeat',type=int,default=2)
    p.add_argument('--reference-height',type=float,default=417)
    p.add_argument('--extra-dir',type=Path,help='Additional bridge frames; existing main frames take precedence.')
    args=p.parse_args();indices=[int(n) for n in args.indices.split(',')]
    out=args.output_dir/'comparison-24fps.mp4';frames=[]
    for i in indices:
        src=np.array(Image.open(args.matte_dir/f'{i+1:04d}.png').convert('RGBA'))
        target=args.output_dir/'rgba'/f'{i+1:04d}.png'
        if not target.exists() and args.extra_dir:target=args.extra_dir/f'{i+1:04d}.png'
        dst=np.array(Image.open(target).convert('RGBA'))
        assert np.array_equal(src[:,:,3],dst[:,:,3]),f'Alpha mismatch source {i}'
        b=Image.new('RGB',(720,360),'#d9e1e6');d=ImageDraw.Draw(b)
        d.text((20,15),'OFFLINE APPEARANCE REVIEW / original video geometry / 24 FPS',fill='#26333d')
        d.text((20,38),'Fixed 110 px character reference; this is not Windows app capture.',fill='#26333d')
        for j,arr in enumerate([src,dst]):
            scale=110/args.reference_height;im=Image.fromarray(arr).resize((round(710*scale),round(592*scale)),Image.Resampling.LANCZOS)
            x=40+360*j;y=112
            d.line((20+360*j,y+round(550*scale),340+360*j,y+round(550*scale)),fill='#8799a8')
            b.paste(im,(x,y),im)
            d.text((30+360*j,88),'Original Cream' if j==0 else 'Tuxedo / unchanged source pose',fill='#26333d')
        d.text((20,320),f'Source frame {i} / body, paws, silhouette and alpha are unchanged.',fill='#26333d')
        frames.append(b.tobytes())
    proc=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','720x360','-framerate','24','-i','-','-an','-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out)],stdin=subprocess.PIPE)
    for _ in range(args.repeat):
        for f in frames:proc.stdin.write(f)
    proc.stdin.close()
    if proc.wait():raise RuntimeError('ffmpeg encoding failed')
    subprocess.run(['ffmpeg','-v','error','-i',str(out),'-f','null','-'],check=True)
    report={'kind':'offline_appearance_review_not_windows_capture','fps':24,'fixed_reference_height':args.reference_height,'logical_character_height':110,'source_indices':indices,'repeat':args.repeat,'frame_count':len(indices)*args.repeat,'duration_seconds':len(indices)*args.repeat/24,'all_source_alpha_identical':True,'full_ffmpeg_decode_passed':True}
    (args.output_dir/'comparison-proof.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out)

if __name__=='__main__':main()
