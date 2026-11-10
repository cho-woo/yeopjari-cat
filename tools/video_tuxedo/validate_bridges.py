#!/usr/bin/env python3
"""Check exact video alpha and residual warm coat chroma, apart from visual QA.

The warm-color check covers the complete foreground (including visible tails),
except actual pink skin/blush. It cannot prove correct black/white pattern
placement; the per-frame 110 px contact boards are a separate visual check.
"""
from __future__ import annotations
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build/flow-deps'))
import cv2

def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--matte-dir',type=Path,required=True);ap.add_argument('--config',type=Path,required=True);args=ap.parse_args()
    config=json.loads(args.config.read_text());records=[];aggregate=hashlib.sha256()
    for i in config['indices']:
        name=f'{i+1:04d}.png';src=np.array(Image.open(args.matte_dir/name).convert('RGBA'));out=np.array(Image.open(args.output/'rgba'/name).convert('RGBA'));old=np.array(Image.open(args.baseline/'rgba'/name).convert('RGBA'))
        assert np.array_equal(src[:,:,3],out[:,:,3]),f'Alpha changed: {i}'
        sr=src[:,:,:3].astype(np.float32);r,g,b=sr[:,:,0],sr[:,:,1],sr[:,:,2]
        pink=((r-g)-1.6*(g-b)>18)&(r>110)
        coat=(src[:,:,3]>230)&~pink
        def warm(a):
            lab=cv2.cvtColor(a[:,:,:3].astype(np.float32)/255,cv2.COLOR_RGB2LAB)
            return int(np.count_nonzero(coat&(lab[:,:,1]>10)&(lab[:,:,2]>15)))
        before=warm(old);after=warm(out)
        aggregate.update(name.encode());aggregate.update((args.output/'rgba'/name).read_bytes())
        records.append({'source_frame':i,'alpha_exact':True,'full_foreground_warm_coat_pixels_before':before,'full_foreground_warm_coat_pixels_after':after,'rgba_sha256':digest(args.output/'rgba'/name)})
    result={'schema':1,'frame_count':len(records),'alpha_exact_all':True,'warm_check':'entire foreground including visible tails; actual pink skin/blush excluded; Lab a>10 and b>15','warm_pixels_before':sum(r['full_foreground_warm_coat_pixels_before'] for r in records),'warm_pixels_after':sum(r['full_foreground_warm_coat_pixels_after'] for r in records),'visual_check':'All 41 frames inspected separately at fixed 110px; color checks do not prove pattern quality.','aggregate_method':'SHA256 of ascending configured filename bytes followed by PNG file bytes','aggregate_rgba_sha256':aggregate.hexdigest(),'config_sha256':digest(args.config),'transfer_script_sha256':digest(Path(__file__).with_name('transfer.py')),'new_motion_poses':0,'frames':records}
    (args.output/'frozen-export.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='frames'},indent=2))

if __name__=='__main__':main()
