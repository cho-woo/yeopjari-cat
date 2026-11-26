"""Registered color differences from matching rest/side poses, measured in sRGB.

One offset per clip throughout its run. Fur shading remains in the source; skin,
eyes, props and antialiasing must not be used as exposure references.
"""
import cv2
import numpy as np
from PIL import Image

# Values measured against the corresponding coat's seated portrait and side pose.
OFFSETS = {
    0: {'groom':(6,7,5),'jump':(3,4,-1),'knead':(6,6,4),'ball':(2,3,-1),
        'toy':(5,6,5),'brake':(3,4,-2),'slide':(3,4,-2),'stumble':(3,4,-1),
        'fish':(6,7,5),'fly':(7,6,5),'walk':(3,4,7),'run':(2,4,-3),'eat':(7,7,4),
        'sleep':(3,3,5),'settle':(6,6,3),'tail-chase':(6,6,3),'roll':(6,6,3),
        'ball-pounce':(7,6,5),'touch-hiss':(-2,0,-6)},
    8: {'groom':(-1,1,3),'knead':(-2,2,3),'toy':(0,4,5),'fish':(-1,3,3),
        'fly':(0,4,3),'walk':(0,-1,3),'run':(-2,-3,-3),'eat':(0,4,4),
        'sleep':(1,-1,4),'ball-pounce':(-1,5,5),'touch-hiss':(2,6,0)},
    10: {'touch-hiss':(-5,-6,-7)}
}


def correct_material(pose, coat, action, protected=None):
    pixels = np.array(pose)
    rgb = pixels[:, :, :3].astype(np.float32)
    r, g, b = rgb.transpose(2, 0, 1)
    alpha = pixels[:, :, 3]
    delta = OFFSETS.get(coat, {}).get(action)
    def ramp(value, lo, hi):
        return np.clip((value-lo)/(hi-lo), 0, 1)
    if delta is not None:
        if coat == 0:
            # Ginger hair is yellow-orange; the pink ear membrane is redder.
            fur = ramp(r,120,156)*ramp(r-g,8,22)*ramp(g-b,4,15)*ramp(b,51,75)
            fur *= 1-ramp(r-g-1.38*(g-b),8,20)
        elif coat == 8:
            fur = ramp(r,24,37)*(1-ramp(r,104,132))
            fur *= (1-ramp(abs(r-g),17,27))*(1-ramp(abs(r-b),28,41))
        else:
            fur = ramp(np.minimum.reduce([r,g,b]),150,190)*(1-ramp(np.ptp(rgb,axis=2),10,22))
        fur *= ramp(alpha,165,215)
        if action in ('fish','toy','eat','ball-pounce'):
            # The isolated upper head and back supply the material. Keep the
            # bowl, toy, fish and any ball at the lower edge in their own colors.
            box=pose.getchannel('A').getbbox()
            if box:
                y=(np.arange(pose.height)-box[1])/max(1,box[3]-box[1])
                fur *= (1-ramp(y,.48,.62))[:,None]
        rgb += fur[:,:,None]*np.asarray(delta,dtype=np.float32)

    if coat == 10 and action == 'ball' and protected is not None:
        # This clip contains only the cat; its ball is drawn by scene physics.
        # Keep the face and pink pads. The lower body is white in the identity art.
        ys=np.nonzero(np.asarray(protected))[0]
        if ys.size:
            body=ramp(np.arange(pose.height)-ys.max(),9,16)[:,None]
            strength=body*ramp(r-b,3,20)*ramp(g-b,-2,7)*(1-ramp(r-g-1.38*(g-b),12,27))
            luma=rgb@np.array([.2126,.7152,.0722],dtype=np.float32)
            neutral=np.stack((luma+1,luma,luma-1),axis=2)
            rgb+=(neutral-rgb)*strength[:,:,None]
    # Only correct color unsupported by a visibly opaque interior. This also
    # handles green-keyed furniture without desaturating its actual green panels.
    distance=cv2.distanceTransform(np.pad((alpha>242).astype('uint8'),1),cv2.DIST_L2,5)[1:-1,1:-1]
    core=distance>3.5
    kernel=cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(13,13))
    has_core=cv2.dilate(core.astype('uint8'),kernel)>0
    edge=(distance<3)&has_core&(alpha>0)
    if coat==10:
        chroma=np.ptp(rgb,axis=2)
        nearby=cv2.dilate(np.where(core,chroma,0).astype(np.float32),kernel)
        unsupported=edge&(nearby<=17)&(chroma>19)
        if unsupported.any():
            patch=rgb[unsupported]
            luma=patch@np.array([.2126,.7152,.0722],dtype=np.float32)
            ratio=np.minimum(1,15/np.maximum(1,np.ptp(patch,axis=1)))
            rgb[unsupported]=luma[:,None]+(patch-luma[:,None])*ratio[:,None]
    else:
        excess=g-np.maximum(r,b)
        interior=cv2.dilate(np.where(core,excess,0).astype(np.float32),kernel)
        halo=edge&(interior<3)&(excess>1)
        rgb[:,:,1][halo]=np.maximum(r,b)[halo]
    pixels[:,:,:3]=np.rint(rgb).clip(0,255).astype(np.uint8)
    result=Image.fromarray(pixels)
    if protected is not None:
        result.paste(pose,mask=protected)
    return result

