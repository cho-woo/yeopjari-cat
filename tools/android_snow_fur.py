"""Snowdrop fur retouch on registered poses. Coordinates are percentages of
the visible source rectangle (including its prop), before facing or app scale.
Only warm pigment in the marked fur is replaced. The atlas poses and timings
remain those of the original retained videos.
"""
import cv2
import numpy as np
from PIL import Image

# These actions carry the registered fur correction in the shared atlas,
# before the head material is applied. Android should only clean the outline.
SHARED_FUR_ACTIONS = {"groom", "knead", "toy", "fish", "eat", "stumble", "tail-chase"}

# (first frame, last frame, polygon). Boundaries are placed in clear fur or
# outside the silhouette. The separate windows follow changes of prop/pose.
FUR_REGIONS = {
    'groom': [(0, 157, [(0,65),(21,68),(31,77),(30,100),(0,100)])],
    'toy': [(0, 157, [(0,59),(13,60),(22,70),(21,95),(0,98)])],
    'fish': [(38, 123, [(0,13),(20,35),(22,57),(20,77),(0,78)])],
    'eat': [(31, 124, [(85,0),(100,0),(100,84),(92,84),(84,52)])],
    'knead': [(0, 157, [(0,66),(19,68),(29,79),(27,99),(0,100)])],
    'tail-chase': [(6, 23, [(0,67),(20,69),(33,78),(31,100),(0,100)])],
    'ball-pounce': [(0, 123, [(0,68),(12,67),(25,79),(23,95),(0,97)])],
    'tower': [(66, 83, [(0,18),(14,23),(20,39),(20,48),(5,45),(0,39)]),
              (107, 118, [(17,0),(29,0),(36,16),(33,25),(19,25)])],
    'box': [(137, 154, [(0,10),(13,17),(22,32),(23,47),(7,47),(0,36)])],
    'stumble': [(37, 50, [(0,63),(23,60),(39,70),(57,70),(57,95),(0,100)])],
}


def retouch(image, action, frame):
    polygons = [p for first,last,p in FUR_REGIONS.get(action, ()) if first <= frame <= last]
    if not polygons:
        return image
    box = image.getchannel('A').getbbox()
    if not box:
        return image
    pixels = np.array(image)
    x,y,r,b = box
    patch = pixels[y:b,x:r]
    h,w = patch.shape[:2]
    mask = np.zeros((h,w), np.uint8)
    for polygon in polygons:
        points = np.rint(np.array(polygon)*[(w-1)/100,(h-1)/100]).astype(np.int32)
        cv2.fillPoly(mask, [points], 255)
    roi = mask > 0
    rgb = patch[:,:,:3].astype(np.float32)
    red,green,blue = rgb.transpose(2,0,1)
    # Feather the change into already-neutral fur. Cool shadows, transparent
    # pixels, dark outlines and saturated pink paw pads retain their colors.
    weight = np.minimum(np.clip((red-blue-3)/18,0,1),np.clip((red-green-1)/10,0,1))
    weight *= np.clip((green-blue+4)/6,0,1) * np.clip((red-50)/30,0,1)
    weight *= roi & (patch[:,:,3]>0)
    if not weight.any():
        return image
    # Neighbouring neutral fur supplies the local light/shade instead of
    # leaving a dark grayscale imprint of a formerly ginger tail stripe.
    luma = rgb @ np.array([.2126,.7152,.0722], dtype=np.float32)
    samples = ((abs(red-blue)<9) & (abs(red-green)<8) & (luma>125)
               & (patch[:,:,3]>190)).astype(np.float32)
    smooth = lambda a: cv2.GaussianBlur(a,(0,0),3)
    support = smooth(samples)
    shade = np.divide(smooth(samples*luma),support,out=np.full_like(luma,226),where=support>.025)
    fur = np.stack([shade+1,shade,shade-1],axis=2)
    out = np.rint(rgb+(fur-rgb)*weight[:,:,None]).clip(0,255).astype(np.uint8)
    patch[:,:,:3] = out
    return Image.fromarray(pixels)
