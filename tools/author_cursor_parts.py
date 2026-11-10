#!/usr/bin/env python3
"""Bake purposeful painted forelimbs into an editable canonical rig UV canvas.

This is texture UV transport, not interpolated full-cat imagery. Original head, tail, outer hips and grounded rear soles remain selected.
Purpose-painted fur fills only the chest exposed by moving forelimbs.
"""
from pathlib import Path
import json
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/'assets/cursor-rig/rig-project.json'

def main():
    spec=json.loads(PROJECT.read_text()); image=np.asarray(Image.open(ROOT/spec['source']).convert('RGBA')).astype(float)
    width,height=spec['canonical_size'];result=np.zeros((height,width*3,4),np.uint8)
    yy,xx=np.mgrid[0:height,0:width];u=(xx+.5)/width;v=(yy+.5)/height
    for index,part in enumerate(spec['forelimbs']):
        a,b,c=np.array(part['painted_joint_path']);s=np.clip((v-.735)/.223,0,1)
        # Quadratic rounds the single painted elbow into a connected skin.
        def centre(t):
            first=a+(b-a)*(t[...,None]/.5)
            last=b+(c-b)*((t[...,None]-.5)/.5)
            q=np.clip((t-.4)/.2,0,1)[...,None];left=a+(b-a)*.8;right=b+(c-b)*.2
            middle=(1-q)**2*left+2*q*(1-q)*b+q*q*right
            return np.where((t<.4)[...,None],first,np.where((t>.6)[...,None],last,middle))
        point=centre(s);tangent=centre(np.minimum(1,s+.005))-centre(np.maximum(0,s-.005))
        length=np.maximum(.0001,np.linalg.norm(tangent,axis=2));tangent/=length[...,None]
        normal=np.stack([tangent[:,:,1],-tangent[:,:,0]],axis=2)
        root=.425 if index==0 else .685
        canonical_x=root+(.010 if index==0 else 0)*(np.clip((v-.735)/.223,0,1)**2*(3-2*np.clip((v-.735)/.223,0,1)))
        transverse=(u-canonical_x)/.088
        extent=np.where(v<.735,(v-.735)/.223,np.where(v>.958,(v-.958)/.223,0))
        point+=normal*transverse[...,None]*part['painted_half_width']+tangent*extent[...,None]*part['painted_length']
        px=np.clip(point[:,:,0],0,image.shape[1]-1.001);py=np.clip(point[:,:,1],0,image.shape[0]-1.001)
        x0=px.astype(int);y0=py.astype(int);fx=(px-x0)[...,None];fy=(py-y0)[...,None]
        sampled=(image[y0,x0]*(1-fx)*(1-fy)+image[y0,x0+1]*fx*(1-fy)+image[y0+1,x0]*(1-fx)*fy+image[y0+1,x0+1]*fx*fy)
        keep=(np.abs(transverse)<=1.0)&(v>=.716)&(v<.997)
        # Remove generated background dust without erasing antialiased fur.
        edge=np.clip((1-np.abs(transverse))/.12,0,1);edge=edge*edge*(3-2*edge)
        sampled[:,:,3]*=edge
        sampled[~keep]=0;sampled[sampled[:,:,3]<=16]=0
        result[:,index*width:(index+1)*width]=np.rint(sampled).clip(0,255).astype(np.uint8)
    # Purpose-painted, armless torso. The inverse UV map registers rear soles
    # independently of the neck width; fixed original face is never resampled.
    body=np.asarray(Image.open(ROOT/'assets/cursor-rig/source-torso.png').convert('RGBA')).astype(float)
    tx=180+(u-.245)/.650*940
    ty=np.where(v<.910,140+(v-.665)/.245*860,1000+(v-.910)/.087*140)
    spread=np.clip((v-.885)/.065,0,1);spread=spread*spread*(3-2*spread)
    foot_x=np.where(u<.565,430+(u-.330)*1495,875+(u-.810)*1495)
    tx=tx*(1-spread)+foot_x*spread
    tx=np.clip(tx,0,body.shape[1]-1.001);ty=np.clip(ty,0,body.shape[0]-1.001)
    x0=tx.astype(int);y0=ty.astype(int);fx=(tx-x0)[...,None];fy=(ty-y0)[...,None]
    torso=(body[y0,x0]*(1-fx)*(1-fy)+body[y0,x0+1]*fx*(1-fy)+body[y0+1,x0]*(1-fx)*fy+body[y0+1,x0+1]*fx*fy)
    cut=np.clip((v-.905)/.018,0,1)*np.clip((u-.455)/.025,0,1)*np.clip((.685-u)/.025,0,1)
    torso[:,:,3]*=1-cut
    torso[(v<.645)|(v>.999)]=0;torso[torso[:,:,3]<=16]=0
    result[:,width*2:]=np.rint(torso).clip(0,255).astype(np.uint8)
    Image.fromarray(result).save(ROOT/spec['output'])
    print(ROOT/spec['output'])
if __name__=='__main__':main()
