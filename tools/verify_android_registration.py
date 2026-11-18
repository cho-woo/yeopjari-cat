"""Verify visible source pixels and sole registration after Android atlas packing."""
import argparse
import json
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageChops
from export_android_motions import OUT, ROOT, VIDEO, read, sha


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    index=read(OUT/'manifest.json')
    assert index['source_identity_sha256']==sha(VIDEO/'identity-v29.json')
    video=read(VIDEO/'manifest.json')['clips']
    treat=next(c for c in read(ROOT/'assets/care-sprites/manifest.json')['clips'] if c['clip']=='treat')
    originals={c['clip']:c for c in [*video,treat]}
    styled=read(ROOT/'assets/styled-sprites/manifest.json')['atlases']
    results=[]
    for item in index['clips']:
        dest=read(OUT/item['file'])
        name,coat=dest['name'],dest['coat']
        # The fixed alpha/RGB movies and the Win32 baked poses have their own
        # native registration; here compare the paired Windows sprite atlases.
        if name not in originals:
            continue
        c=originals[name]
        source=ROOT/dest['source']
        assert sha(source)==dest['source_sha256']
        registration=c if coat==0 else next(s for s in styled if s['character']==coat and s['source']==source.name)
        anchors=registration.get('anchors',[f['anchor'] for f in c['frames']])
        assert c['sequence']==dest['sequence'] and c['reference_height']==dest['reference_height']
        assert len(c['frames'])==len(dest['frames'])
        @lru_cache(maxsize=4)
        def page(i):
            with Image.open(OUT/dest['pages'][i]['file']) as im:
                return im.convert('RGBA')
        with Image.open(source) as im:
            sheet=im.convert('RGBA')
        for i,(old,new) in enumerate(zip(c['frames'],dest['frames'])):
            x,y,w,h=old['rect']
            original=sheet.crop((x,y,x+w,y+h))
            bounds=original.getbbox()
            assert new['anchor']==[anchors[i][0]-bounds[0],anchors[i][1]-bounds[1]],(name,coat,i,'anchor')
            x,y,w,h=new['rect']
            packed=page(new['page']).crop((x,y,x+w,y+h))
            original=original.crop(bounds)
            assert packed.size==original.size
            assert not ImageChops.difference(original,packed).getbbox(alpha_only=False),(name,coat,i,'pixels')
        page.cache_clear()
        results.append(dict(name=name,coat=coat,frames=len(c['frames'])))
    assert len(results)==69
    report=dict(passed=True,source='Windows registered sprite atlases',clips=len(results),
        checked_frames=sum(r['frames'] for r in results),identity_sha256=index['source_identity_sha256'],results=results)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(report['clips'],report['checked_frames'],'pixel and anchor comparisons passed')


if __name__=='__main__':
    main()
