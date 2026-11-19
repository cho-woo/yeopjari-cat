"""Pack the registered Windows motion library for Android's Canvas renderer."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw
from android_snow_edges import clean_edges
from android_snow_fur import retouch

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'mobile/android/app/src/main/assets/motions'
NATIVE = ROOT / 'mobile/windows-gestures'
VIDEO = ROOT / 'assets/video-actions'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sequence(count):
    return dict(intro=[dict(frame=i, ms=round((i+1)*1000/24)-round(i*1000/24)) for i in range(count)], loop=[], outro=[])


def repaired_pose(pose, name, frame, protected=None):
    result = clean_edges(retouch(pose, name, frame))
    if protected is not None and result is not pose:
        result.paste(pose, mask=protected)
    return result


def iris_frames(sheet, frames):
    for frame in frames:
        x,y,w,h=frame['rect']
        yield sheet.crop((x,y,x+w,y+h)).point(lambda label: 255 if label else 0)


def movie_eyes(asset, name):
    if name=='ball-pounce':
        path=VIDEO/'selected/ball-v40/snowdrop-eyes.json'
        sw,sh=480,480
    else:
        path=ROOT/f'assets/furniture-v39/selected/review/snowdrop-{name}-eyes.json'
        sw,sh=asset['source_dimensions']
    w,h=asset['width'],asset['height']
    for eyes in read(path):
        mask=Image.new('L',(w,h))
        draw=ImageDraw.Draw(mask)
        for eye in eyes:
            x,y,rw,rh=eye['rect']
            draw.rectangle((int(x*w/sw),int(y*h/sh),
                            int(np.ceil((x+rw)*w/sw))-1,int(np.ceil((y+rh)*h/sh))-1),fill=255)
        yield mask


def pack(directory, name, coat, poses, anchors, reference, playlist, *, movie=False, protected=None):
    # Trim transparent margins without moving the world anchor. Identical exposures
    # share pixels but retain independent source timestamps and contact records.
    records, pages, pending, seen = [], [], [], {}
    def flush():
        if not pending:
            return
        w = max(im.width for im in pending)+2
        h = max(im.height for im in pending)+2
        page = Image.new('RGBA', (w*min(8,len(pending)), h*((len(pending)+7)//8)))
        page_id = len(pages)
        for i, im in enumerate(pending):
            x,y = (i%8)*w+1,(i//8)*h+1
            page.paste(im,(x,y))
        for rec in records:
            if rec['page']==page_id and 'slot' in rec:
                i=rec.pop('slot')
                rec['rect']=[(i%8)*w+1,(i//8)*h+1,pending[i].width,pending[i].height]
        filename=f'c{coat}-{name}-{page_id}.webp'
        path=directory/filename
        page.save(path, lossless=(not movie or coat == 10), quality=94, method=2, exact=True)
        pages.append(dict(file=filename,sha256=sha(path),width=page.width,height=page.height))
        pending.clear()

    for index, pose in enumerate(poses):
        if coat == 10:
            pose = repaired_pose(pose, name, index, next(protected) if protected is not None else None)
        box=pose.getbbox()
        if not box:
            raise ValueError(f'Empty {name}/{coat} frame {index}')
        crop=pose.crop(box)
        key=(crop.size,hashlib.sha256(crop.tobytes()).digest())
        anchor=[anchors[index][0]-box[0],anchors[index][1]-box[1]]
        if key in seen:
            record=dict(records[seen[key]],anchor=anchor)
        else:
            record=dict(page=len(pages),slot=len(pending),anchor=anchor)
            seen[key]=index
            pending.append(crop)
        records.append(record)
        if len(pending)==24:
            flush()
    flush()
    return dict(name=name,coat=coat,reference_height=reference,frames=records,pages=pages,sequence=playlist)


def export_native(exe):
    NATIVE.mkdir(exist_ok=True)
    artwork=ROOT/'release/YeopjariCat.exe'
    entries=[]
    for coat in (0,8,10):
        with subprocess.Popen([str(exe),str(artwork),str(coat)],stdout=subprocess.PIPE) as proc:
            while header:=proc.stdout.readline():
                name,count=header.decode().split()
                count=int(count)
                def poses():
                    for _ in range(count):
                        data=proc.stdout.read(224*192*4)
                        # GDI+ DIB is premultiplied BGRA. Pillow unpremultiplies RGBa.
                        rgba=np.frombuffer(data,np.uint8).reshape(192,224,4)[:,:,[2,1,0,3]]
                        yield Image.frombytes('RGBa',(224,192),rgba.tobytes()).convert('RGBA')
                entry=pack(NATIVE,name,coat,poses(),[(112,150)]*count,96,sequence(count))
                entry['source']='src/cat_animation.hpp / DrawPetStyle'
                entries.append(entry)
            if proc.wait()!=0:
                raise RuntimeError('Windows gesture capture failed')
    (NATIVE/'manifest.json').write_text(json.dumps(entries,separators=(',',':')),encoding='utf-8')


def movie_frames(path):
    capture=cv2.VideoCapture(str(path))
    try:
        while True:
            ok,frame=capture.read()
            if not ok:
                break
            rgb=cv2.cvtColor(frame,cv2.COLOR_BGR2RGB)
            h=rgb.shape[0]//2
            alpha=rgb[h:,:,0].copy()
            alpha[alpha<8]=0
            yield Image.fromarray(np.dstack((rgb[:h],alpha)))
    finally:
        capture.release()


def write_index(entries):
    index=[]
    for entry in entries:
        filename=f'c{entry["coat"]}-{entry["name"]}.json'
        (OUT/filename).write_text(json.dumps(entry,separators=(',',':')),encoding='utf-8')
        index.append(dict(name=entry['name'],coat=entry['coat'],file=filename))
        if entry['name']=='settle':
            frame=entry['frames'][entry['sequence']['intro'][-1]['frame']]
            x,y,w,h=frame['rect']
            neutral=f'c{entry["coat"]}-neutral'
            with Image.open(OUT/entry['pages'][frame['page']]['file']) as sheet:
                sheet.crop((x,y,x+w,y+h)).save(OUT/(neutral+'.webp'),lossless=True,exact=True)
            data=dict(name='neutral',coat=entry['coat'],reference_height=entry['reference_height'],
                sequence=sequence(1),pages=[dict(file=neutral+'.webp')],frames=[dict(page=0,rect=[0,0,w,h],anchor=frame['anchor'])])
            (OUT/(neutral+'.json')).write_text(json.dumps(data,separators=(',',':')),encoding='utf-8')
    manifest=dict(schema=3,source_identity_sha256=sha(VIDEO/'identity-v29.json'),clips=index)
    (OUT/'manifest.json').write_text(json.dumps(manifest,separators=(',',':')),encoding='utf-8')


def main():
    args=argparse.ArgumentParser()
    args.add_argument('--native-exe',type=Path,help='Refresh Windows procedural gestures from the offline renderer')
    args=args.parse_args()
    if args.native_exe:
        export_native(args.native_exe)
        return
    OUT.mkdir(parents=True, exist_ok=True)
    style=read(ROOT/'assets/styled-sprites/manifest.json')['atlases']
    registration=read(VIDEO/'registration.json')
    clips=read(VIDEO/'manifest.json')['clips']
    treat=next(c for c in read(ROOT/'assets/care-sprites/manifest.json')['clips'] if c['clip']=='treat')
    entries=[]
    for c in [*clips,treat]:
        name=c['clip']
        base_dir=VIDEO if c is not treat else ROOT/'assets/care-sprites'
        base=(base_dir/c['source']).relative_to(ROOT).as_posix()
        for coat in (0,8,10):
            spec=c if coat==0 else next(s for s in style if s['character']==coat and s['base_source']==base)
            src=(base_dir if coat==0 else ROOT/'assets/styled-sprites')/spec['source']
            if sha(src)!=spec['sha256']:
                raise ValueError(src)
            with Image.open(src) as sheet:
                sheet=sheet.convert('RGBA')
                def poses():
                    for f in c['frames']:
                        x,y,w,h=f['rect']
                        yield sheet.crop((x,y,x+w,y+h))
                anchors=spec.get('anchors',[f['anchor'] for f in c['frames']])
                protected = None
                if coat==10:
                    with Image.open(src.parent/'odd-eye-masks'/src.name) as mask_file:
                        protected=iris_frames(mask_file.convert('L'),c['frames'])
                entry=pack(OUT,name,coat,poses(),anchors,c['reference_height'],c['sequence'],protected=protected)
            entry.update(source=src.relative_to(ROOT).as_posix(),source_sha256=spec['sha256'],stride_pixels=c.get('stride_pixels',0))
            reg=registration.get(('tuxedo' if coat==8 else 'cream')+'-'+name,{})
            if name in ('ball','punch','reach','cursor-pounce'):
                entry['paws']=[p[1] for p in reg['paw_points_logical']]
            if name=='toy':
                entry['prop']=reg['prop_points_logical']
                entry['prop_visible']=reg['prop_visible']
            entries.append(entry)
        print('Registered',name,flush=True)
    balls=read(VIDEO/'selected/ball-v40/registration.json')
    for asset in read(ROOT/'assets/furniture-v39/manifest.json')['assets']+balls['assets']:
        name=asset.get('kind','ball-pounce')
        src=ROOT/asset['path']
        if sha(src)!=asset['sha256']:
            raise ValueError(src)
        count=asset['source_frames']
        entry=pack(OUT,name,asset['character'],movie_frames(src),[asset['anchor']]*count,asset['reference_height'],sequence(count),movie=True,
                   protected=movie_eyes(asset,name) if asset['character']==10 else None)
        assert len(entry['frames'])==count
        entry.update(source=asset['path'],source_sha256=asset['sha256'])
        if name=='ball-pounce':
            entry['paws']=balls['paw_points_logical']['tuxedo' if asset['character']==8 else 'cream']
        entries.append(entry)
    for entry in read(NATIVE/'manifest.json'):
        for page in entry['pages']:
            src=NATIVE/page['file']
            if sha(src)!=page['sha256']:
                raise ValueError(src)
            if entry['coat']!=10:
                shutil.copyfile(src,OUT/src.name)
        if entry['coat']==10:
            # Already-trimmed Win32 frames have anchors in that trim's space.
            native_pages = []
            for page in entry['pages']:
                with Image.open(NATIVE/page['file']) as sheet:
                    native_pages.append(sheet.convert('RGBA'))
            def native_poses():
                for frame in entry['frames']:
                    x,y,w,h=frame['rect']
                    yield native_pages[frame['page']].crop((x,y,x+w,y+h))
            processed = pack(OUT,entry['name'],10,native_poses(),
                [f['anchor'] for f in entry['frames']],entry['reference_height'],entry['sequence'])
            processed['source'] = entry['source']
            entry = processed
        entries.append(entry)
    write_index(entries)
    used={p['file'] for c in entries for p in c['pages']}
    used.update(f'c{coat}-neutral.webp' for coat in (0,8,10))
    for path in OUT.glob('*.webp'):
        if path.name not in used:
            path.unlink()
    print(len(entries),'coat-specific clips;',len(used),'pages;',sum(p.stat().st_size for p in OUT.iterdir()),'bytes')

if __name__=='__main__':
    main()
