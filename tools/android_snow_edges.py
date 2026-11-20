"""Suppress unsupported chroma along the keyed Snowdrop silhouette.

A green-screen spill or a subsampled video chroma block can survive inside the
matte, including at alpha=255. Compare each opponent channel to the range of
nearby opaque interior colors; do not erode the matte or copy a neighboring
color into the fur. Thin isolated features with no interior reference are kept.
"""
import cv2
import numpy as np
from PIL import Image

INNER = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7,7))
NEAR = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (13,13))


def clean_edges(image):
    pixels = np.array(image)
    alpha = pixels[:,:,3]
    opaque = (alpha >= 248).astype(np.uint8)
    core = cv2.erode(opaque, INNER, borderType=cv2.BORDER_CONSTANT, borderValue=0) > 0
    edge = (alpha > 0) & ~core & (cv2.dilate(core.astype(np.uint8),NEAR)>0)
    if not edge.any():
        return image
    rgb = pixels[:,:,:3].astype(np.float32)
    r,g,b = rgb.transpose(2,0,1)
    rg,bg = r-g,b-g

    def supported(channel):
        hi = cv2.dilate(np.where(core,channel,-255), NEAR)
        lo = cv2.erode(np.where(core,channel,255), NEAR)
        # Always allow neutral and local variations of at least a few levels.
        return np.clip(channel,np.minimum(lo,0)-4,np.maximum(hi,0)+4)

    sr,sb = supported(rg),supported(bg)
    changed = edge & ((sr != rg) | (sb != bg))
    if not changed.any():
        return image
    # Change chroma only. Premultiplication and the antialiased coverage are
    # handled once by the normal Android bitmap decoder.
    green = g + .2126*(rg-sr) + .0722*(bg-sb)
    fixed = np.stack((green+sr,green,green+sb),axis=2).round().clip(0,255)
    pixels[changed,:3] = fixed[changed]
    return Image.fromarray(pixels)
