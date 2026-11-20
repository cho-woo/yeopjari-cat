"""Compare all Snowdrop frame registrations and colored outlines to a previous APK."""
import argparse
import io
import json
import zipfile
from functools import lru_cache
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw


class Frames:
    def __init__(self, path):
        self.root = path
        self.apk = zipfile.ZipFile(path) if path.is_file() else None

    def read(self, filename):
        return self.apk.read('assets/motions/'+filename) if self.apk else (self.root/filename).read_bytes()

    @lru_cache(maxsize=6)
    def sheet(self, filename):
        return Image.open(io.BytesIO(self.read(filename))).convert('RGBA')

    def frame(self, spec, frame):
        rec = spec['frames'][frame]
        x,y,w,h = rec['rect']
        return self.sheet(spec['pages'][rec['page']]['file']).crop((x,y,x+w,y+h))


def inspect(image):
    rgb = np.array(image,dtype=np.int16)
    alpha = rgb[:,:,3]
    chroma = np.ptp(rgb[:,:,:3],axis=2)
    # Count visibly chromatic border pixels next to a demonstrably neutral
    # interior. Ear, prop or iris colors in a nearby interior exclude a site.
    mask = np.pad((alpha>242).astype('uint8'),1)
    dist = cv2.distanceTransform(mask,cv2.DIST_L2,5)[1:-1,1:-1]
    core = dist>3.5
    nearby = cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(13,13))
    has_core = cv2.dilate(core.astype('uint8'),nearby)>0
    pigment = cv2.dilate(np.where(core,chroma,0).astype('uint8'),nearby)
    score = chroma * alpha.astype(np.float32)/255
    return (dist<=2.8) & has_core & (pigment<=16) & (score>=26)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--previous',type=Path,required=True)
    p.add_argument('--current',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    old,new = Frames(a.previous),Frames(a.current)
    names=[item['file'] for item in json.loads(new.read('manifest.json'))['clips'] if item['coat']==10]
    names.append('c10-neutral.json')
    summary=[]
    example=[]
    for filename in names:
        oldspec,newspec=(json.loads(src.read(filename)) for src in (old,new))
        name=newspec['name']
        assert len(oldspec['frames'])==len(newspec['frames'])
        for field in ('sequence','reference_height','paws','prop','prop_visible'):
            assert oldspec.get(field)==newspec.get(field),(name,field)
        before,after,peak=0,0,0
        streaks=dict(before=0,after=0)
        for i,(s,t) in enumerate(zip(oldspec['frames'],newspec['frames'])):
            assert s['anchor']==t['anchor'] and s['rect'][2:]==t['rect'][2:],(name,i,'registration')
            u,v=old.frame(oldspec,i),new.frame(newspec,i)
            assert u.getchannel('A').tobytes()==v.getchannel('A').tobytes(),(name,i,'alpha')
            m,n=inspect(u),inspect(v)
            b,c=int(m.sum()),int(n.sum())
            before+=b;after+=c
            for label,mask in [('before',m),('after',n)]:
                if mask.any():
                    components=cv2.connectedComponentsWithStats(mask.astype('uint8'),8)[2][1:]
                    if components[:,4].max()>=12:
                        streaks[label]+=1
            if b-c>peak:
                peak=b-c
                best=(name,i,u.copy(),v.copy(),m.copy())
        if peak:
            example.append((peak,best))
        summary.append(dict(clip=name,frames=len(newspec['frames']),before=before,after=after,frames_with_cluster=streaks))
    total=lambda k:sum(row[k] for row in summary)
    report=dict(clips=len(names),frames=total('frames'),identical_alpha_anchors_and_timings=True,
                colored_edge_pixel_exposures_before=total('before'),colored_edge_pixel_exposures_after=total('after'),results=summary)
    for key in ('before','after'):
        report['frames_with_cluster_'+key]=sum(item['frames_with_cluster'][key] for item in summary)
    (a.out/'edges.json').write_text(json.dumps(report,indent=2))
    assert total('after')<total('before'),report
    samples=sorted(example,key=lambda x:-x[0])[:6]
    sheet=Image.new('RGB',(540,145*len(samples)),'#283039')
    draw=ImageDraw.Draw(sheet)
    for j,(peak,(name,frame,u,v,mask)) in enumerate(samples):
        draw.text((6,j*145+2),f'{name}  {frame}    before / after / baseline flagged pixels',fill='white')
        marked=np.array(u)
        marked[mask,:]=[255,60,150,255]
        for k,im in enumerate([u,v,Image.fromarray(marked)]):
            ratio=min(168/im.width,124/im.height)
            tile=im.resize((round(im.width*ratio),round(im.height*ratio)),Image.Resampling.NEAREST)
            sheet.paste(tile,(k*180+(180-tile.width)//2,j*145+18),tile)
    if samples:
        sheet.save(a.out/'edge-review.png')
    print({k:v for k,v in report.items() if k!='results'})


if __name__=='__main__':
    main()
