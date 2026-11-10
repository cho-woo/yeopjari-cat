#!/usr/bin/env python3
"""Offline white OddEye material bake; never invents poses or changes alpha.

The original Cream source is the geometry/detail reference. Explicit per-frame
iris regions are exported for review (and mirrored-eye rendering), with material
masks for original skin and authored prop protection. Tux uses its separate
retained original paintings and the shared soft charcoal/ivory palette.
"""
from __future__ import annotations
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from scipy import ndimage as ndi
from PIL import Image,ImageChops,ImageDraw,ImageFilter
from lock_tuxedo_identity import material_masks,sha,maskhash
from import_styled_sprites import runtime_care_entries
ROOT=Path(__file__).resolve().parents[1]
STYLES=ROOT/'assets/styled-sprites'
# Closed eyes have no recolored iris. Original dark eyelid/ink is preserved.
CLOSED={101:{5,6,7},102:{0,2},105:{3},112:{0,1},113:set(range(2,14)),
        114:{1,3,4,5,6,7},116:{3,6},120:{17,21,22}}
FALLBACK={101:(4,8),102:(4,8),105:(2,4),106:(2,4),107:(2,2),110:(1,1),112:(2,2)}

def frame_rects(base,size,care):
    if base in care:return [f['rect'] for f in care[base]['frames']]
    cols,count=FALLBACK[base];rows=count//cols;w,h=size
    return [[i%cols*w//cols,i//cols*h//rows,(i%cols+1)*w//cols-i%cols*w//cols,
             (i//cols+1)*h//rows-i//cols*h//rows] for i in range(count)]

def eye_material(source,rect,base,frame):
    x,y,w,h=rect;a=np.asarray(source.crop((x,y,x+w,y+h)));hsv=np.asarray(Image.fromarray(a).convert('RGB').convert('HSV'))
    r,g,b=a[:,:,:3].transpose(2,0,1);yy,xx=np.mgrid[:h,:w]
    candidates=(a[:,:,3]>180)&(hsv[:,:,0]>=5)&(hsv[:,:,0]<=43)&(hsv[:,:,1]>110)&(hsv[:,:,2]>35)&(hsv[:,:,2]<225)
    candidates&=(xx>w*.26)&(xx<w*.99)&(yy<h*.64)
    # A lying head occupies the right part of its wide fallback cell.
    if base==107:candidates&=(xx>w*.63)
    if base==110:candidates&=(yy<h*.43)
    if frame in CLOSED.get(base,set()):return [],np.zeros((h,w),np.uint8)
    lab,n=ndi.label(ndi.binary_dilation(candidates,iterations=2));regions=[]
    for index,sl in enumerate(ndi.find_objects(lab),1):
        if sl is None:continue
        ys,xs=sl;component=(lab[sl]==index)&candidates[sl];area=int(component.sum());cw=xs.stop-xs.start;ch=ys.stop-ys.start
        core=(r[sl]<150)&(g[sl]<110)&(b[sl]<75)
        if area<max(30,w*h*.00023) or area>6500 or ch<max(9,h*.032) or not .22<cw/ch<2.2 or np.count_nonzero(core&component)<12:continue
        regions.append((float((ys.start+ys.stop)/2),area,[xs.start,ys.start,xs.stop,ys.stop],index))
    # Two highest round deep-brown regions are the eye pair. A genuine profile
    # contains only one. This excludes tail stripes and the mouth below the eyes.
    regions.sort();regions=regions[:2];regions.sort(key=lambda e:e[2][0]);mask=np.zeros((h,w),np.uint8);eyes=[]
    for ordinal,(_,area,box,index) in enumerate(regions):
        # In the unflipped right-facing source the visible near eye is right.
        # Front views: image-left is anatomical right, image-right is left.
        side='right' if ordinal==0 else 'left';pigment=1 if side=='right' else 2
        mask[(lab==index)&candidates]=pigment
        eyes.append({'rect':[x+box[0],y+box[1],box[2]-box[0],box[3]-box[1]],'anatomical_side':side,'material':'gold' if pigment==1 else 'blue','pixels':area})
    return eyes,mask

def white_coat(source,prop,eyes):
    masks=material_masks(source,prop);rgb=np.asarray(source.convert('RGB')).astype(np.float32);a=np.asarray(source.getchannel('A'));h,w=a.shape
    skin=np.asarray(masks['skin'])>0;props=np.asarray(masks['props'])>0
    maximum=rgb.max(2);luma=rgb@np.array([.2126,.7152,.0722]);fur=(a>0)&~skin&~props&(maximum>=55)&(eyes==0)
    # Remove orange material while keeping the exact painted fur texture and
    # its soft occlusion shadows. Dark eyelid/whisker ink and skin stay intact.
    value=np.clip(186+(luma-100)*.45,152,251)
    neutral=np.stack((value+2,value+1,value),axis=2)
    result=rgb.copy();result[fur]=neutral[fur]
    # Continuous original pink chroma preserves the cheek/ear gradient; a
    # binary skin mask alone would leave tiny harsh red islands after whitening.
    skin_weight=np.clip((5*(rgb[:,:,0]-rgb[:,:,1])-8*(rgb[:,:,1]-rgb[:,:,2]))/80,0,1)
    skin_weight*=((rgb.max(2)-rgb.min(2))>8)&(rgb[:,:,0]>90)
    result=result*(1-skin_weight[:,:,None])+rgb*skin_weight[:,:,None]
    hsv=np.asarray(source.convert('RGB').convert('HSV')).copy()
    for pigment,hue in ((1,27),(2,145)):
        mask=eyes==pigment;hsv[:,:,0][mask]=hue;hsv[:,:,1][mask]=np.minimum(180,hsv[:,:,1][mask]);hsv[:,:,2][mask]=np.minimum(240,hsv[:,:,2][mask].astype(float)*1.12).astype(np.uint8)
    painted=np.asarray(Image.fromarray(hsv,'HSV').convert('RGB'));result[eyes>0]=painted[eyes>0]
    result[props|skin]=rgb[props|skin]
    out=Image.fromarray(np.clip(np.dstack((result,a)),0,255).astype(np.uint8),'RGBA')
    return out,{'alpha_exact':out.getchannel('A').tobytes()==source.getchannel('A').tobytes(),
        'props_rgb_exact':not ImageChops.multiply(ImageChops.difference(out.convert('RGB'),source.convert('RGB')).convert('L'),masks['props']).getbbox(),
        'skin_rgb_exact':not ImageChops.multiply(ImageChops.difference(out.convert('RGB'),source.convert('RGB')).convert('L'),masks['skin']).getbbox(),
        'props_rgb_sha256':maskhash(out.convert('RGB'),masks['props']),'skin_rgb_sha256':maskhash(out.convert('RGB'),masks['skin'])}

def parts():
    source=Image.open(ROOT/'assets/cursor-rig/forelimbs.png').convert('RGBA');a=np.asarray(source).copy();rgb=a[:,:,:3].astype(float);h,w=rgb.shape[:2];yy=np.mgrid[:h,:w][0]/h
    masks=material_masks(source);skin=np.asarray(masks['skin'])>0;luma=rgb@np.array([.2126,.7152,.0722]);shade=np.clip(luma/240,.3,1.06)
    for look in ('tuxedo','odd-eye'):
        if look=='tuxedo':
            weight=np.clip((yy-.875)/.045,0,1);weight=weight*weight*(3-2*weight)
            color=np.array([69,75,86])[None,None,:]*(1-weight[:,:,None])+np.array([241,232,219])[None,None,:]*weight[:,:,None]
                    # Third panel is the attached torso, with a broad rounded ivory
            # bib and white hind socks. A gentle feather keeps fur edges soft.
            if w>=987:
                xx=np.mgrid[:h,:w][1];u=(xx%329)/329
                side=np.clip(np.maximum((.44-u)/.09,(u-.70)/.09),0,1)
                foot=np.clip((yy-.91)/.045,0,1);dark=side*(1-foot)
                torso=np.array([69,75,86])[None,None,:]*dark[:,:,None]+np.array([241,232,219])[None,None,:]*(1-dark[:,:,None])
                color=np.where((xx>=658)[:,:,None],torso,color)
        else:color=np.broadcast_to([249,247,243],rgb.shape)
        painted=np.clip(color*shade[:,:,None],0,255);painted[skin]=rgb[skin];out=a.copy();out[:,:,:3]=painted
        Image.fromarray(out).save(ROOT/f'assets/cursor-rig/forelimbs-{look}.png')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');args=p.parse_args()
    if args.check:
        d=json.loads((STYLES/'odd-eye-policy.json').read_text());assert d['tool_sha256']==sha(__file__)
        for e in d['atlases']:
            assert sha(ROOT/e['base_source'])==e['input_sha256'];assert sha(STYLES/e['source'])==e['output_sha256']
            assert hashlib.sha256(Image.open(STYLES/e['source']).getchannel('A').tobytes()).hexdigest()==e['source_alpha_sha256']
        print('PASS: OddEye all16 source, output, alpha and tool hashes');return
    manifest=json.loads((STYLES/'manifest.json').read_text());cream=[e for e in manifest['atlases'] if e['character']==8]
    care={e['resource']:e for e in runtime_care_entries(ROOT)};entries=[];policy={'schema':1,'design':'snowdrop-white-odd-eye-original-cream-geometry','new_motion_poses':0,'eye_identity':{'left':'blue','right':'gold'},'atlases':[]};annotations=[]
    maskdir=STYLES/'odd-eye-masks';maskdir.mkdir(exist_ok=True)
    for i,e in enumerate(cream):
        base=e['base_resource_id'];source=Image.open(ROOT/e['base_source']).convert('RGBA');eye=np.zeros((source.height,source.width),np.uint8);record=[]
        for frame,rect in enumerate(frame_rects(base,source.size,care)):
            eyes,local=eye_material(source,rect,base,frame);x,y,w,h=rect;eye[y:y+h,x:x+w]=local;record.append({'frame':frame,'eyes':eyes})
        propfile=STYLES/'identity-masks'/e['source'];prop=Image.open(propfile).convert('L') if propfile.exists() else None
        out,checks=white_coat(source,prop,eye);filename=f'c10-r{base}.png';out.save(STYLES/filename)
        Image.fromarray(eye,'L').save(maskdir/filename)
        entries.append({'character':10,'base_resource_id':base,'resource_id':600+i,'source':filename})
        policy['atlases'].append({'base_resource_id':base,'base_source':e['base_source'],'source':filename,'input_sha256':sha(ROOT/e['base_source']),'output_sha256':sha(STYLES/filename),'source_alpha_sha256':hashlib.sha256(source.getchannel('A').tobytes()).hexdigest(),'iris_mask':f'odd-eye-masks/{filename}','iris_mask_sha256':sha(maskdir/filename),**checks})
        annotations.append({'base_resource_id':base,'source':filename,'frames':record});print(base,sum(len(f['eyes']) for f in record),checks,flush=True)
    # Existing style registration remains untouched; OddEye uses Cream anchors.
    for e in cream:e.pop('sha256',None)
    manifest['atlases']=cream+entries;manifest['design']='soft-charcoal-tuxedo-and-snowdrop-odd-eye-v26';(STYLES/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    policy['tool_sha256']=sha(__file__);(STYLES/'odd-eye-policy.json').write_text(json.dumps(policy,indent=2)+'\n');(STYLES/'odd-eye-iris-regions.json').write_text(json.dumps({'schema':1,'atlases':annotations},indent=2)+'\n');parts()

if __name__=='__main__':main()
