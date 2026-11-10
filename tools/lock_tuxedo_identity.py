#!/usr/bin/env python3
"""Bake one shared fur palette into every existing Tuxedo runtime atlas.

This changes material RGB only. Alpha, texture geometry, white-mark locations,
frame rectangles, timing and physical anchors remain the original authored data.
Pink skin/blush, olive/gold irises, and explicit semantic prop masks are protected.
There is no per-frame runtime recoloring or image decoding.
"""
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from PIL import Image,ImageChops,ImageFilter,ImageMath
ROOT=Path(__file__).resolve().parents[1]

CURVE_INPUT=[0,10,25,50,80,110,145,175,195,220,255]
CURVE_OUTPUT=[38,42,54,70,84,98,149,206,229,239,248]

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def spec():return {'schema':1,'method':'shared_soft_charcoal_cream_fur_rgb_only','curve_input':CURVE_INPUT,'curve_output':CURVE_OUTPUT,'dark_fur_rgb':[69,75,86],'light_fur_rgb':[241,232,219],'preserve':'semantic props; original pink skin/blush; original olive-gold iris and eye ink; all original alpha','new_motion_poses':0}

def tone(value):
    for i in range(1,len(CURVE_INPUT)):
        if value<=CURVE_INPUT[i]:
            a,b=CURVE_INPUT[i-1:i+1];x,y=CURVE_OUTPUT[i-1:i+1]
            return round(x+(value-a)*(y-x)/(b-a))
    return CURVE_OUTPUT[-1]

def count(mask):return mask.histogram()[255]
def both(a,b):return ImageChops.multiply(a,b)
def either(a,b):return ImageChops.lighter(a,b)
def maxdiff(a,b):
    r,g,b=ImageChops.difference(a,b).split();return either(either(r,g),b)
def maskhash(rgb,mask):
    return hashlib.sha256(Image.composite(rgb,Image.new('RGB',rgb.size),mask).tobytes()).hexdigest()

def material_masks(image,prop_mask=None):
    source=image.convert('RGBA');rgb=source.convert('RGB');r,g,b=rgb.split()
    maximum=either(either(r,g),b);minimum=ImageChops.darker(ImageChops.darker(r,g),b)
    chroma=ImageChops.subtract(maximum,minimum)
    # Integer form of (R-G)-1.6*(G-B)>16, with enough chroma for true skin.
    evaluate=getattr(ImageMath,'unsafe_eval',None) or getattr(ImageMath,'eval')
    skin=evaluate("convert(((r-g)*5-(g-b)*8)>80,'L')",r=r.convert('I'),g=g.convert('I'),b=b.convert('I')).point(lambda v:255 if v else 0)
    skin=both(both(skin,r.point(lambda v:255 if v>90 else 0)),chroma.point(lambda v:255 if v>25 else 0))
    h,s,v=rgb.convert('HSV').split()
    iris=both(both(h.point(lambda x:255 if 28<=x<=85 else 0),s.point(lambda x:255 if x>65 else 0)),v.point(lambda x:255 if x>35 else 0)).filter(ImageFilter.MaxFilter(9))
    # Near-black tail/back material is fur, not eye ink. Protect dark pixels
    # only around the authored iris and pink facial features; otherwise the
    # old coat's almost-black flecks survive as harsh spots on blue charcoal.
    # A thresholded Gaussian support provides the same feather-safe facial
    # guard without an expensive 51x51 rank filter in every offline check.
    facial=either(iris.filter(ImageFilter.GaussianBlur(8)).point(lambda x:255 if x else 0),
                  skin.filter(ImageFilter.GaussianBlur(3)).point(lambda x:255 if x else 0))
    ink=both(maximum.point(lambda x:255 if x<25 else 0),facial)
    props=Image.new('L',image.size)
    if prop_mask is not None:
        if prop_mask.size!=image.size:raise ValueError('Prop preservation mask canvas mismatch')
        props=prop_mask.convert('L').point(lambda x:255 if x else 0)
    protected=either(either(skin,iris),either(ink,props));foreground=source.getchannel('A').point(lambda x:255 if x else 0)
    fur=both(foreground,ImageChops.invert(protected))
    return {'source':source,'rgb':rgb,'skin':skin,'iris':iris,'props':props,'protected':protected,'foreground':foreground,'fur':fur}

def apply_palette(image:Image.Image,prop_mask:Image.Image|None=None):
    masks=material_masks(image,prop_mask);source=masks['source'];rgb=masks['rgb']
    y=rgb.convert('L',(.2126,.7152,.0722,0))
    # Blue charcoal shadows and warm ivory highlights soften the old pure
    # black/white contrast. One shared material curve is baked offline.
    values=[tone(x) for x in range(256)]
    def channel(offset_low,offset_high):
        return y.point([max(0,min(255,round(value+(offset_low*(1-min(1,max(0,(value-105)/105)))+
            offset_high*min(1,max(0,(value-105)/105)))))) for value in values])
    neutral=Image.merge('RGB',(channel(0,0),channel(6,-7),channel(17,-18)))
    result=Image.composite(neutral,rgb,masks['fur']).convert('RGBA');result.putalpha(source.getchannel('A'))
    assert result.getchannel('A').tobytes()==source.getchannel('A').tobytes()
    assert not both(maxdiff(result.convert('RGB'),rgb),masks['protected']).getbbox()
    checks={'fur_pixels':count(masks['fur']),'alpha_exact':True}
    for key in ('props','skin','iris'):
        checks['protected_'+key+'_pixels']=count(both(masks[key],masks['foreground']))
        checks[key+'_rgb_exact']=True;checks[key+'_rgb_sha256']=maskhash(rgb,masks[key])
    return result,checks

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-dir',type=Path,default=ROOT/'assets/styled-sprites');p.add_argument('--mask-dir',type=Path,required=True);p.add_argument('--output',type=Path);p.add_argument('--check',action='store_true');args=p.parse_args()
    if args.check:
        policy=json.loads((args.source_dir/'identity-policy.json').read_text())
        for entry in policy['atlases']:
            path=args.source_dir/entry['source'];mask_path=args.mask_dir/entry['source']
            assert sha(path)==entry['output_sha256'],f'Identity PNG drift: {path}'
            if entry['prop_mask']:assert sha(mask_path)==entry['prop_mask_sha256']
            image=Image.open(path).convert('RGBA');masks=material_masks(image,Image.open(mask_path) if mask_path.exists() else None)
            assert hashlib.sha256(image.getchannel('A').tobytes()).hexdigest()==entry['source_alpha_sha256']
            for key in ('props','skin','iris'):
                assert maskhash(masks['rgb'],masks[key])==entry[key+'_rgb_sha256'],f'Protected {key} drift: {path}'
        print(json.dumps({'identity_atlases':len(policy['atlases']),'alpha_and_protected_rgb':'passed','dependencies':'Pillow only'}));return
    if args.output is None:p.error('--output is required when baking')
    args.output.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((args.source_dir/'manifest.json').read_text());policy=spec();policy['atlases']=[]
    for entry in manifest['atlases']:
        filename=entry['source'];path=args.source_dir/filename;mask_path=args.mask_dir/filename
        with Image.open(path) as image:
            mask=Image.open(mask_path) if mask_path.exists() else None
            result,checks=apply_palette(image,mask);result.save(args.output/filename)
        policy['atlases'].append({'base_resource_id':entry['base_resource_id'],'source':filename,'input_sha256':sha(path),'output_sha256':sha(args.output/filename),'source_alpha_sha256':hashlib.sha256(Image.open(path).convert('RGBA').getchannel('A').tobytes()).hexdigest(),'prop_mask':filename if mask_path.exists() else None,'prop_mask_sha256':sha(mask_path) if mask_path.exists() else None,**checks})
        print(entry['base_resource_id'],checks,flush=True)
    policy['tool_sha256']=sha(__file__)
    (args.output/'identity-policy.json').write_text(json.dumps(policy,indent=2)+'\n')

if __name__=='__main__':main()
