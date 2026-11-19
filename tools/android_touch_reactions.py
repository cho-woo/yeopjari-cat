"""Register the original Dropshot takes on the shared seated artwork's fixed root."""
import hashlib
import json

import cv2
import numpy as np
from PIL import Image

from pack_video_actions import eye_material, foreground, snow_material


def snow_face(pose, mask):
    # The take warms up when the ears fold. Preserve actual ear and muzzle
    # pigment within the head, so those shades cannot appear on the tail.
    pixels = np.asarray(pose)
    r,g,b,a = pixels.transpose(2,0,1).astype(np.float32)
    left,top,right,bottom = pose.getbbox()
    y,x = np.indices(mask.shape)
    head = (a>160) & (y>=top) & (y<top+min(30,(right-left)*.34))
    hx = np.where(head)[1]
    width = hx.max()-hx.min()+1
    u,v = (x-hx.min())/width,(y-top)/width
    def ramp(value):
        t=np.clip(value,0,1)
        return t*t*(3-2*t)
    def oval(cx,cy,rx,ry):
        return ramp((1-((u-cx)/rx)**2-((v-cy)/ry)**2)*3)
    ears = np.maximum(oval(.12,.21,.14,.25),oval(.88,.21,.14,.25))
    face = oval(.5,.66,.60,.45)*(1-ramp((v-.81)/.08))
    pink = ramp((r-g-8)/25)*ramp((b-g+8)/15)*np.maximum(ears,face)
    luma=.2126*r+.7152*g+.0722*b
    # Preserve soft white fur shading while removing the ginger cheek stripes.
    white=np.interp(luma,[0,65,125,185,255],[155,193,220,242,255])
    relief=ramp((g+4-r)/20)*ramp((luma-70)/75)
    white=white*(1-relief)+np.maximum(145,luma)*relief
    rgb=np.repeat(white[:,:,None],3,axis=2)
    # Folded ginger ears can turn almost amber under warm lighting. Only
    # the two inner ear regions receive the Snowdrop artwork's peach color.
    ear_weight=ears*ramp((r-g-5)/36)
    peach=np.stack((np.minimum(255,r+4),.73*g+61,.68*b+82),axis=2)
    rgb=rgb*(1-ear_weight[:,:,None])+peach*ear_weight[:,:,None]
    # Eyes, eyelids, nostrils, and the open mouth have a continuous dark
    # outline. Keep their shading through the blink and the closing mouth.
    features=face*ramp((150-np.maximum.reduce((r,g,b)))/50)
    mouth=face*ramp((v-.47)/.13)*ramp((r-g-14)/32)*ramp((195-g)/45)*ramp((b-g+22)/16)
    pigment=np.maximum.reduce((pink*.50,mouth,features))[:,:,None]
    rgb=rgb*(1-pigment)+pixels[:,:,:3]*pigment
    colored,_=snow_material(pose,mask,strict_skin=True)
    rgb[mask>0]=np.asarray(colored)[:,:,:3][mask>0]
    result=np.dstack((rgb.clip(0,255).round().astype(np.uint8),pixels[:,:,3]))
    result[pixels[:,:,3]==0,:3]=0
    return Image.fromarray(result)


def touch_entries(root, output, pack, sequence):
    directory = root/'mobile/touch-reactions'
    registration = json.loads((directory/'registration.json').read_text(encoding='utf-8'))
    for item in registration['sources']:
        path = directory/item['file']
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError(f'Unexpected touch source: {path}')
        capture = cv2.VideoCapture(str(path))
        poses, snow, protected = [], [], []
        try:
            if capture.get(cv2.CAP_PROP_FPS) != item['fps']:
                raise ValueError(f'Unregistered source clock: {path}')
            for frame in range(item['last']+1):
                ok, pixels = capture.read()
                if not ok:
                    raise ValueError(f'Missing frame {frame} in {path}')
                if frame < item['first']:
                    continue
                matte = foreground(cv2.cvtColor(pixels, cv2.COLOR_BGR2RGB))
                # Chroma compression can leave green just inside the opaque
                # outline. Limit despill to the outer edge, preserving tuxedo eyes.
                pixels_rgba = np.array(matte)
                r,g,b,a = pixels_rgba.transpose(2,0,1)
                edge = cv2.distanceTransform((a>180).astype(np.uint8),cv2.DIST_L2,5)<3.5
                g[edge] = np.minimum(g[edge],np.maximum(r,b)[edge])
                matte = Image.fromarray(pixels_rgba)
                target = round(registration['canonical_canvas']*96/registration['reference_height'])
                pose = matte.resize((target,target),Image.Resampling.LANCZOS)
                poses.append(pose)
                if item['coat']=='cream':
                    left,top,right,bottom = pose.getbbox()
                    _, iris = eye_material(pose,(left,top,right-left,bottom-top),'touch-hiss')
                    mask = np.zeros((target,target),np.uint8)
                    mask[top:bottom,left:right] = iris
                    snow.append(snow_face(pose,mask))
                    protected.append(Image.fromarray(np.where(mask,255,0).astype(np.uint8)))
        finally:
            capture.release()
        root_pt = [v*target/registration['canonical_canvas'] for v in item['canonical_root']]
        height = target*registration['reference_height']/registration['canonical_canvas']
        for coat in ((0,10) if item['coat']=='cream' else (8,)):
            entry = pack(output,'touch-hiss',coat,snow if coat==10 else poses,
                         [root_pt]*len(poses),height,sequence(len(poses)),
                         protected=iter(protected) if coat==10 else None)
            entry.update(source=path.relative_to(root).as_posix(),source_sha256=item['sha256'],
                         glance_end=item['glance_end']-item['first'])
            yield entry
