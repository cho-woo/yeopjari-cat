#!/usr/bin/env python3
"""Pack only Cream's legacy cells; source pixels are copied without retouching.

Input: directory containing the v2.0.13 mixed legacy atlases. Reproduce via
`git archive 0979f25 assets | tar -x -C /tmp/old-cats`, then pass its assets dir.
The care sheets are deliberately untouched. This removes retired characters
from distributed resources while keeping canonical crop pixels identical.
"""
import argparse
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
JOBS=[('cat-jump-churu.png','cream-extra.png',2,8,4),
      ('cat-gestures.png','cream-gestures.png',2,8,4),
      ('cat-recline.png','cream-recline.png',2,4,2),
      ('companions-hang.png','cream-hang.png',5,10,1),
      ('companions-sleep.png','cream-sleep.png',4,20,2)]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);args=parser.parse_args()
    for source,target,columns,count,keep in JOBS:
        image=Image.open(args.source/source).convert('RGBA');a=np.array(image)
        a[a[:,:,3]<=16]=0;image=Image.fromarray(a)
        w,h=image.size;rows=count//columns;cuts=[i*h//rows for i in range(rows+1)]
        if columns==2 or source=='companions-sleep.png':
            for split in range(1,rows):
                mid=split*h//rows;radius=h//rows//4
                low=max(cuts[split-1]+1,mid-radius);high=min(h-(rows-split),mid+radius)
                cuts[split]=min(range(low,high+1),key=lambda y:(int((a[y,:,3]>16).sum()),abs(y-mid)))
        frames=[]
        for index in range(keep):
            x0=(index%columns)*w//columns;x1=(index%columns+1)*w//columns
            y0,y1=cuts[index//columns:index//columns+2]
            ys,xs=np.where(a[y0:y1,x0:x1,3]>16)
            box=(max(x0,x0+int(xs.min())-2),max(y0,y0+int(ys.min())-2),
                 min(x1,x0+int(xs.max())+3),min(y1,y0+int(ys.max())+3))
            frames.append(image.crop(box))
        out_columns=min(2,keep);cell_w=max(f.width for f in frames)+8;cell_h=max(f.height for f in frames)+8
        packed=Image.new('RGBA',(cell_w*out_columns,cell_h*(keep//out_columns)))
        for i,frame in enumerate(frames):packed.paste(frame,((i%out_columns)*cell_w+4,(i//out_columns)*cell_h+4))
        packed.save(ROOT/'assets'/target)
        print(f'{source}: kept {keep}/{count} cells -> {target}')

if __name__=='__main__':main()
