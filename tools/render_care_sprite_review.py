#!/usr/bin/env python3
"""Register/review supplied action PNGs without changing their source bytes.

Registration is explicit metadata, never per-pose scaling. --register creates a
review manifest for these eight supplied cream-cat sheets. Normal operation reads
an existing manifest, checks hashes, compiles the real C++ sequence sampler, and
renders its choices using the same reference-height/anchor equation as DrawCare.
Pillow/numpy/scipy are review-tool dependencies, not application dependencies.

  python3 tools/render_care_sprite_review.py --register
  python3 tools/render_care_sprite_review.py --manifest assets/care-sprites/manifest.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageChops
from scipy.ndimage import label

ROOT = Path(__file__).resolve().parents[1]
LAYOUTS = {'groom':(4,4),'sleep':(4,2),'box':(4,2),'fish':(4,2),
           'punch':(4,3),'eat':(3,4),'jump':(4,4),'treat':(6,4)}
ENUMS = {name:name.title() for name in LAYOUTS}
# Heights are from an upright neutral cat, excluding bowls/hands/fish/boxes.
# Jump uses the final recovered standing pose, not a crouch or extended flight.
REFERENCE_HEIGHT = {'groom':244,'sleep':293,'box':294,'fish':411,
                    'punch':331,'eat':243,'jump':192,'treat':232}
BACKGROUND = (75,79,88,255)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def major_silhouettes(pixels, count, columns):
    labels, _ = label(pixels[:,:,3] > 64)
    areas = np.bincount(labels.ravel())
    identifiers = np.argsort(areas[1:])[-count:] + 1
    if len(identifiers) != count or min(areas[identifiers]) < 1500:
        raise ValueError('Expected complete, individually separated action silhouettes')
    objects = []
    for identifier in identifiers:
        yy, xx = np.where(labels == identifier)
        objects.append({'center':[float(xx.mean()),float(yy.mean())],
                        'bbox':[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]})
    objects.sort(key=lambda obj:obj['center'][1])
    return sum((sorted(objects[first:first+columns],key=lambda obj:obj['center'][0])
                for first in range(0,count,columns)), [])


def choose_gutter(projection, target, low, high):
    candidates=range(low,high+1)
    # Keep interpolation kernels in clear padding where the supplied art has
    # enough space. Two-pixel Groom gutters cannot be widened by metadata;
    # runtime per-frame bitmap isolation handles those without changing fur.
    def score(value):
        band=projection[max(0,value-2):min(len(projection),value+3)]
        return (int(band.sum()),int(projection[value]),abs(value-target))
    return min(candidates,key=score)


def gutter_cuts(pixels, objects, columns, rows):
    """Fit gutters to real silhouettes: Groom's large left margin is deliberate."""
    material = pixels[:,:,3] > 32
    cuts = []
    for axis, count in ((0,columns),(1,rows)):
        centers = []
        for index in range(count):
            selected = objects[index::columns] if axis == 0 else objects[index*columns:(index+1)*columns]
            centers.append(float(np.median([obj['center'][axis] for obj in selected])))
        extent = pixels.shape[1-axis]
        projection = material.sum(axis=axis)
        boundaries = [0]
        for index in range(1,count):
            target = (centers[index-1]+centers[index])/2
            radius = max(4,int((centers[index]-centers[index-1])*.25))
            boundaries.append(choose_gutter(projection,target,
                max(boundaries[-1]+1,int(target)-radius),
                min(extent-(count-index),int(target)+radius)))
        boundaries.append(extent)
        cuts.append(boundaries)
    return cuts


def cream_torso(pixels, bbox):
    x0,y0,x1,y1 = bbox
    region = pixels[y0:y1,x0:x1].astype(float)
    red, green, blue = (region[:,:,channel] for channel in range(3))
    yy, xx = np.indices(region.shape[:2])
    cream = ((region[:,:,3]>128)&(red>190)&(green>165)&(blue>120)&
             (red-green>0)&(red-green<45)&(green-blue>2)&(green-blue<50))
    band = cream&(yy>(y1-y0)*.65)&(yy<(y1-y0)*.88)
    return ([float(np.median(xx[band]))+x0,float(np.median(yy[band]))+y0]
            if band.any() else [(x0+x1)/2,(y0+y1)/2])


def box_anchor(pixels, bbox, index):
    # The box, not the kitten's changing head/foot pose, is the fixed world prop.
    x0,y0,x1,y1=bbox
    region=pixels[max(y0,y1-60):y1,x0:x1].astype(float)
    r,g,b=(region[:,:,channel] for channel in range(3))
    brown=(region[:,:,3]>128)&(r>90)&(r<250)&(r-g>20)&(g-b>15)&(b<150)
    labels,count=label(brown)
    if not count:raise ValueError('Box support could not be identified')
    areas=np.bincount(labels.ravel());largest=1+int(areas[1:].argmax())
    yy,xx=np.where(labels==largest)
    if not len(xx):raise ValueError('Box support could not be identified')
    # In frame0 the tail/forepaw outline touches the left wall color mask.
    # Register the inspected cardboard wall (x196..374), not that connected fur.
    return [285.0 if index==0 else (int(xx.min())+int(xx.max())+1)/2+x0,float(y1)]


def register(source_dir, manifest):
    entries=[];audit={}
    for clip,(columns,rows) in LAYOUTS.items():
        source=source_dir/(clip+'.png')
        image=Image.open(source)
        if image.mode!='RGBA':raise ValueError(f'{source}: real RGBA required')
        pixels=np.asarray(image)
        objects=major_silhouettes(pixels,columns*rows,columns)
        xcuts,ycuts=gutter_cuts(pixels,objects,columns,rows)
        ycuts_by_column=[]
        for col in range(columns):
            boundaries=[0];projection=(pixels[:,xcuts[col]:xcuts[col+1],3]>32).sum(axis=1)
            for row in range(1,rows):
                previous=objects[(row-1)*columns+col]['bbox']
                following=objects[row*columns+col]['bbox']
                low,high=previous[3],following[1]
                if low>high:raise ValueError(f'{clip}: overlapping row silhouettes')
                boundaries.append(choose_gutter(projection,(low+high)/2,low,high))
            boundaries.append(image.height);ycuts_by_column.append(boundaries)
        torso_to_support=(objects[15]['bbox'][3]-cream_torso(pixels,objects[15]['bbox'])[1]
                          if clip=='jump' else 0)
        frames=[];criteria=[]
        for index,obj in enumerate(objects):
            col,row=index%columns,index//columns
            x0,x1=xcuts[col:col+2];y0,y1=ycuts_by_column[col][row:row+2]
            bx0,by0,bx1,by1=obj['bbox']
            if not(x0<=bx0<bx1<=x1 and y0<=by0<by1<=y1):
                raise ValueError(f'{clip} frame{index}: adaptive gutter cuts visible material')
            ax,torso_y=cream_torso(pixels,obj['bbox']);ay=float(by1)
            criterion='cream torso support x; complete cat silhouette floor y'
            if clip=='box':
                ax,ay=box_anchor(pixels,obj['bbox'],index);criterion='fixed box center at box bottom; cat pose is not the anchor'
            elif clip=='fish':
                # First two fish extend below the inferred support of the cat.
                ax=[205,568,930,1270,200,560,915,1270][index]
                ay=[500,500,498,498,1040,1040,1039,1040][index]
                criterion='cat torso/paw support, excluding fish prop below first two frames'
            elif clip=='eat':
                ay=[267,265,267,524,524,524,782,782,783,1039,1039,1042][index]
                criterion='remeasured solid cream cat paw floor after prop-only bowl alignment; gray bowl is excluded'
            elif clip=='treat':
                ay=[243,242,242,242,243,244,516,516,516,516,516,516,
                    788,788,788,788,788,788,1062,1062,1062,1062,1062,1062][index]
                criterion='cream torso and cat paw floor; tube and human hand excluded'
            elif clip=='jump' and 4<=index<=11:
                # Remove authored canvas offsets once. Anchor a common virtual
                # support plane below the visible torso; physics supplies height.
                # This is a body registration offset, not extra screen motion.
                ay=torso_y+torso_to_support
                criterion='torso root plus standing frame15 torso-to-support offset; source blank offset removed; physics owns world height'
            anchor=[round(ax-x0,3),round(ay-y0,3)]
            if not(0<=anchor[0]<=x1-x0 and 0<=anchor[1]<=y1-y0):
                raise ValueError(f'{clip} frame{index}: anchor outside source rectangle')
            frames.append({'rect':[x0,y0,x1-x0,y1-y0],'anchor':anchor})
            criteria.append({'frame':index,'silhouette_bbox':obj['bbox'],'world_anchor_source':[ax,ay],
                             'criterion':criterion})
        entries.append({'clip':clip,'character':0,'source':str(source.relative_to(manifest.parent)),
                        'columns':columns,'rows':rows,'reference_height':REFERENCE_HEIGHT[clip],
                        'frames':frames,'sha256':digest(source)})
        audit[clip]={'sha256':digest(source),'size':list(image.size),'cuts_x':xcuts,'cuts_y':ycuts,
                     'cuts_y_by_column':ycuts_by_column,
                     'reference_height':REFERENCE_HEIGHT[clip],
                     'reference_pose':15 if clip=='jump' else 0,'frames':criteria}
    manifest.parent.mkdir(parents=True,exist_ok=True)
    manifest.write_text(json.dumps({'schema':1,'clips':entries},ensure_ascii=False,indent=2)+'\n')
    (manifest.parent/'registration.provenance.json').write_text(json.dumps({
        'schema':1,'method':'explicit source rectangles and anatomical support anchors; unchanged original PNG bytes',
        'alpha_material_threshold':64,'gutter_threshold':32,'target_reference_height_px':110,
        'one_reference_per_clip':True,'notes':[
            'Only cream character0 is registered; all other characters keep existing artwork.',
            'References are neutral standing/sitting cat height, excluding scene props.',
            'No individual pose height normalization or image rescaling is stored in assets.',
            'Jump source offsets are registered to a support plane; Box2D owns root motion.'],
        'clips':audit},ensure_ascii=False,indent=2)+'\n')


def sampled_frames(entries, output):
    """Compile the current sampler and its duration constants; never guess timing."""
    limits={'groom':'kGroomDefaultMs+640','sleep':'kSleepStoryIntroMs+6000',
            'box':'kBoxDurationMs+700','fish':'kFishDurationMs+600',
            'punch':'cat::animation::kPunchDurationMs+620','eat':'kEatDurationMs+600',
            'jump':'cat::animation::kJumpDurationMs+470','treat':'kTreatDurationMs+600'}
    cases=[{'key':entry['clip'],'clip':entry['clip'],'sheet_frames':len(entry['frames']),
            'limit':limits[entry['clip']],'step':80,'variant':0,'sleep':'Entering'} for entry in entries]
    names={entry['clip']:entry for entry in entries}
    if 'groom' in names:
        for variant in (1,2):
            cases.append({'key':f'groom-v{variant}','clip':'groom','sheet_frames':len(names['groom']['frames']),
                          'limit':'kGroomDefaultMs+640','step':80,'variant':variant,'sleep':'Entering'})
    if 'sleep' in names:
        for key,stage,limit,step in (('sleep-wake','Waking','kSleepWakeMs+600',80),
                                   ('sleep-rest','Resting','kSleepStoryRestMs',1000)):
            cases.append({'key':key,'clip':'sleep','sheet_frames':len(names['sleep']['frames']),
                          'limit':limit,'step':step,'variant':0,'sleep':stage})
    constants=('kGroomDefaultMs','kGroomMinMs','kGroomMaxMs','kSleepStoryIntroMs','kSleepStoryRestMs',
               'kSleepWakeMs','kBoxDurationMs','kEatDurationMs','kFishDurationMs','kTreatDurationMs',
               'kCareMinDurationMs','kCareMaxDurationMs','kCareOutroMs','kPunchImpactMs','kPunchSecondImpactMs','kLandingMs')
    cpp=['#include "cat_sprite_sequence.hpp"','#include "cat_animation.hpp"','#include <iostream>','int main(){',
         'using namespace cat::sprite_sequence; Options o;']
    for constant in constants:cpp.append(f'std::cout<<"C {constant} "<<{constant}<<"\\n";')
    cpp.append('std::cout<<"C kPunchHostDurationMs "<<cat::animation::kPunchDurationMs<<"\\n";')
    cpp.append('std::cout<<"C kJumpHostDurationMs "<<cat::animation::kJumpDurationMs<<"\\n";')
    for case in cases:
        key,clip=case['key'],case['clip'];step=case['step']
        cpp += ['o=Options{};',f'o.sheetFrames={case["sheet_frames"]}; o.variant={case["variant"]};',
                f'o.sleep=SleepStage::{case["sleep"]};',
                '{',f'const auto limit=(({case["limit"]})+{step}-1)/{step}*{step};',
                f'std::cout<<"M {key} "<<limit<<" {step}\\n";',
                f'for(unsigned ms=0;ms<=limit;ms+={step}){{']
        if clip=='jump':
            cpp+=['o.grounded=ms<220||ms>=1000; o.jump=ms<220?JumpPhase::Crouch:JumpPhase::Landing;',
                  'o.contactElapsedMs=ms>=1000?ms-1000:0; o.velocityY=320.-(static_cast<double>(ms)-220.)*.84;']
        cpp += [f'auto f=Sample(Clip::{ENUMS[clip]},ms,o);',
                f'std::cout<<"S {key} "<<ms<<" "<<f.frame<<" "<<f.groundContact<<" "<<f.localMs<<" "<<f.holdMs<<" "<<f.looping<<" "<<f.holding<<" "<<f.finished<<"\\n";',
                '}','}']
    cpp+=['}']
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='care-review-',dir=output) as temporary:
        folder=Path(temporary);source=folder/'sample.cpp';exe=folder/'sample'
        source.write_text('\n'.join(cpp)+'\n')
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(source),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout
    (output/'actual-cpp-samples.txt').write_text(result)
    frames={case['key']:[] for case in cases}
    metadata={'constants':{},'sequences':{case['key']:{'clip':case['clip'],'groom_variant':case['variant'],
        'sleep_stage':case['sleep'],'sheet_frames':case['sheet_frames'],'samples':[]} for case in cases}}
    for line in result.splitlines():
        fields=line.split();kind,key=fields[:2]
        if kind=='C':metadata['constants'][key]=int(fields[2]);continue
        if kind=='M':
            metadata['sequences'][key].update({'limit_ms':int(fields[2]),'sample_step_ms':int(fields[3])});continue
        if kind!='S':raise ValueError(f'Unexpected C++ sampler output: {line}')
        ms,frame,ground,local,hold,looping,holding,finished=map(int,fields[2:])
        frames[key].append((ms,frame,bool(ground)))
        metadata['sequences'][key]['samples'].append({'ms':ms,'frame':frame,'grounded':bool(ground),
            'local_ms':local,'hold_ms':hold,'looping':bool(looping),'holding':bool(holding),'finished':bool(finished)})
    for sequence in metadata['sequences'].values():
        samples=sequence['samples'];sequence['frame_order']=[]
        for sample in samples:
            if not sequence['frame_order'] or sequence['frame_order'][-1]!=sample['frame']:
                sequence['frame_order'].append(sample['frame'])
        holds=[sample['hold_ms'] for sample in samples if sample['hold_ms'] and not sample['finished']]
        sequence['minimum_authored_hold_ms']=min(holds) if holds else 0
    return frames,metadata


def runtime_image(path):
    # Same harmless alpha<=16 cutoff used when LoadCareSprites prepares GDI.
    pixels=np.array(Image.open(path).convert('RGBA'))
    pixels[pixels[:,:,3]<=16]=0
    return Image.fromarray(pixels)


def legacy_reference():
    image=runtime_image(ROOT/'assets/cat.png');rects=[]
    for index in range(6):
        col,row=index%4,index//4
        x0,x1=col*image.width//4,(col+1)*image.width//4
        y0,y1=row*image.height//2,(row+1)*image.height//2
        alpha=np.array(image.crop((x0,y0,x1,y1)))[:,:,3]>16
        yy,xx=np.where(alpha)
        left,top=max(x0,x0+int(xx.min())-2),max(y0,y0+int(yy.min())-2)
        right,bottom=min(x1,x0+int(xx.max())+3),min(y1,y0+int(yy.max())+3)
        rects.append([left,top,right-left,bottom-top])
    rect=rects[4]
    entry={'frames':[{'rect':rect,'anchor':[rect[2]/2,rect[3]]}],
           'reference_height':max(rect[3] for rect in rects)}
    return image,entry


def place(canvas, image, entry, index, center, bottom, markers=True, naive=False):
    frame=entry['frames'][index];x,y,w,h=frame['rect'];ax,ay=frame['anchor']
    if naive:
        cols,rows=entry['columns'],entry['rows'];col,row=index%cols,index//cols
        x,y=col*image.width//cols,row*image.height//rows
        w,h=(col+1)*image.width//cols-x,(row+1)*image.height//rows-y
        ax,ay=w/2,h
    factor=110/entry['reference_height']
    # This is DrawCare's destination equation at growth/body/DPI scale1.
    crop=image.crop((x,y,x+w,y+h));size=(max(1,round(w*factor)),max(1,round(h*factor)))
    resized=crop.resize(size,Image.Resampling.LANCZOS)
    px,py=round(center-ax*factor),round(bottom-ay*factor)
    canvas.alpha_composite(resized,(px,py))
    if markers:
        draw=ImageDraw.Draw(canvas);draw.line((center-4,bottom,center+4,bottom),fill=(255,220,70,255))
        draw.line((center,bottom-4,center,bottom+4),fill=(255,220,70,255))
        material=np.array(crop)[:,:,3]>128
        if material.any():
            yy,xx=np.where(material);head_y=py+float(yy.min())*factor
            upper=yy<yy.min()+(yy.max()-yy.min())*.4
            head_x=px+float(np.median(xx[upper]))*factor
            draw.line((head_x-3,head_y,head_x+3,head_y),fill=(100,225,255,255))
    return [px,py,size[0],size[1]]


def sequence_panel(image, entry, key, sample, size=(440,290)):
    ms,index,ground=sample
    canvas=Image.new('RGBA',size,BACKGROUND);draw=ImageDraw.Draw(canvas)
    floor=size[1]-30;center=size[0]/2
    draw.line((0,floor,size[0],floor),fill=(125,135,145,255))
    draw.text((8,6),f'{key}  t={ms/1000:.2f}s  source frame{index}',fill='white')
    bottom=floor
    if entry['clip']=='jump' and not ground:
        # Preview-only ballistic root, explicitly separate from live Box2D.
        t=(ms-220)/1000;bottom-=max(0,320*t-420*t*t)
    place(canvas,image,entry,index,center,bottom)
    return canvas


def export_summary(manifest, output, report, prefix):
    """Export a real-time timeline board with a reproducible source manifest."""
    entries={entry['clip']:entry for entry in json.loads(manifest.read_text())['clips']}
    sequences=report['timelines']['sequences']
    order=[key for key in ('groom','groom-v1','groom-v2','sleep','sleep-wake','sleep-rest',
                           'box','fish','punch','eat','treat','jump') if key in sequences]
    images={clip:runtime_image((manifest.parent/entry['source']).resolve()) for clip,entry in entries.items()}
    panel_width,panel_height=260,270;columns=4;header=36
    rows=(len(order)+columns-1)//columns
    # The compact board covers the full entry/care/box events. The separate
    # sleep-rest GIF retains the complete120s cycle without speeding it up.
    limit=max(sequence['limit_ms'] for key,sequence in sequences.items() if key!='sleep-rest')
    step=80;frames=[]
    for ms in range(0,limit+1,step):
        board=Image.new('RGB',(columns*panel_width,rows*panel_height+header),BACKGROUND[:3]);draw=ImageDraw.Draw(board)
        draw.text((8,4),'DEVELOPER PREVIEW - compiled C++ timing, Pillow drawing; not a Windows capture',fill='white')
        draw.text((8,19),f'Independent examples at real speed; host event scheduling is not shown. t={ms/1000:.2f}s',fill='#dadde2')
        for position,key in enumerate(order):
            sequence=sequences[key];samples=sequence['samples'];index=min(ms//sequence['sample_step_ms'],len(samples)-1)
            sample=samples[index];entry=entries[sequence['clip']]
            panel=sequence_panel(images[sequence['clip']],entry,key,
                (sample['ms'],sample['frame'],sample['grounded']),(panel_width,panel_height))
            label='finished: quiet hold' if sample['finished'] else 'rest loop' if sample['looping'] else 'one event'
            if key=='sleep-rest':label='full120s cycle in separate sleep-rest GIF'
            ImageDraw.Draw(panel).text((8,21),label,fill='#dadde2')
            board.paste(panel.convert('RGB'),((position%columns)*panel_width,(position//columns)*panel_height+header))
        frames.append(board)
    prefix=prefix.resolve();prefix.parent.mkdir(parents=True,exist_ok=True)
    gif=Path(str(prefix)+'.gif');png=Path(str(prefix)+'.png');proof=Path(str(prefix)+'.json')
    snapshot=min(30,len(frames)-1);frames[snapshot].save(png)
    # One palette keeps the preview from flashing as a different pose appears.
    palette=frames[snapshot].quantize(colors=256)
    quantized=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
    quantized[0].save(gif,save_all=True,append_images=quantized[1:],duration=step,loop=0,disposal=1,optimize=True)
    # A retained-frame GIF is smaller, but must decode without fur trails
    # or stale poses. Check EVERY exported frame against its fixed palette.
    with Image.open(gif) as decoded:
        if decoded.n_frames!=len(quantized):raise ValueError('Summary GIF lost timeline frames')
        for index,expected in enumerate(quantized):
            decoded.seek(index)
            if ImageChops.difference(decoded.convert('RGB'),expected.convert('RGB')).getbbox():
                raise ValueError(f'Summary GIF composition drift at frame{index}')
    exported=dict(report)
    exported['summary_artifacts']={'gif':str(gif.relative_to(ROOT)),'png':str(png.relative_to(ROOT)),
        'gif_sha256':digest(gif),'png_sha256':digest(png),'frame_count':len(frames),'step_ms':step,
        'duration_ms':len(frames)*step,'png_clock_ms':snapshot*step,'panel_order':order,
        'long_sleep_rest_preview':str((output/'sleep-rest-registered.gif').relative_to(ROOT))}
    proof.write_text(json.dumps(exported,indent=2)+'\n')
    print(json.dumps({'summary':str(prefix),'frames':len(frames),'duration_ms':len(frames)*step}))


def review(manifest, output):
    data=json.loads(manifest.read_text());entries=data['clips'];samples,timeline=sampled_frames(entries,output)
    report={'manifest_sha256':digest(manifest),'sampler_sha256':digest(ROOT/'src/cat_sprite_sequence.hpp'),
            'placement':'DrawCare reference=110/referenceHeight; rect drawn at center-anchor*reference',
            'sample_source':'actual compiled C++ cat::sprite_sequence::Sample with sheetFrames=descriptor count',
            'scope':'DEVELOPER PREVIEW: compiled C++ timelines, Pillow drawing; not a Windows capture or live Box2D simulation. Cream character0 at110 logical pixels, body/growth/DPI scale1.',
            'preview_tool_sha256':digest(Path(__file__)),
            'animation_header_sha256':digest(ROOT/'src/cat_animation.hpp'),
            'presentation_limits':'Raw authored keyframes; application pose crossfades, scheduler, cursor input and window/Box2D behavior are outside this developer preview.',
            'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'timelines':timeline,
            'clips':{}}
    neutral=Image.new('RGBA',((len(entries)+1)*190,170),BACKGROUND)
    old_image,old_entry=legacy_reference()
    ImageDraw.Draw(neutral).text((4,4),'Existing 2D sitting cat',fill='white')
    place(neutral,old_image,old_entry,0,92,145)
    report['legacy_reference_height']=old_entry['reference_height']
    for entry_index,entry in enumerate(entries):
        clip=entry['clip'];path=(manifest.parent/entry['source']).resolve()
        if digest(path)!=entry['sha256']:raise ValueError(f'{clip}: registered source hash drift')
        image=runtime_image(path);columns=entry['columns'];rows=entry['rows'];cells=[]
        labels,_=label(np.array(image)[:,:,3]>64);areas=np.bincount(labels.ravel())
        major=set((np.argsort(areas[1:])[-len(entry['frames']):]+1).tolist())
        for index,frame in enumerate(entry['frames']):
            x,y,w,h=frame['rect'];members=set(np.unique(labels[y:y+h,x:x+w]).tolist())&major
            if len(members)!=1:raise ValueError(f'{clip} frame{index}: source ROI includes adjacent cat or misses its cat')
        sheet=Image.new('RGBA',(columns*220,rows*170),BACKGROUND);draw=ImageDraw.Draw(sheet)
        for index,frame in enumerate(entry['frames']):
            col,row=index%columns,index//columns;center=col*220+105;floor=row*170+145
            draw.line((col*220,floor,(col+1)*220,floor),fill=(125,135,145,255))
            draw.text((col*220+5,row*170+5),f'{clip} {index}',fill='white')
            cells.append(place(sheet,image,entry,index,center,floor))
        sheet.convert('RGB').save(output/(clip+'-110px.png'))
        sheet.resize((sheet.width*4,sheet.height*4),Image.Resampling.NEAREST).convert('RGB').save(output/(clip+'-4x.png'))
        ImageDraw.Draw(neutral).text(((entry_index+1)*190+4,4),f'{clip} ref{entry["reference_height"]}',fill='white')
        place(neutral,image,entry,15 if clip=='jump' else 0,(entry_index+1)*190+92,145)
        frames=[];comparison=[]
        for ms,index,ground in samples[clip]:
            if index>=len(entry['frames']):raise ValueError(f'{clip}: C++ returned invalid frame{index}')
            baseline=260 if clip=='jump' else 180
            canvas=Image.new('RGBA',(440,baseline+30),BACKGROUND);d=ImageDraw.Draw(canvas)
            d.line((0,baseline,440,baseline),fill=(125,135,145,255));d.text((8,6),f'{clip}  {ms}ms  source frame{index}',fill='white')
            bottom=baseline
            if clip=='jump' and not ground:
                # Explicit preview-only trajectory. This tests registration;
                # the Windows application uses actual Box2D root positions.
                t=(ms-220)/1000;bottom-=max(0,320*t-420*t*t)
            place(canvas,image,entry,index,220,bottom)
            frames.append(canvas.convert('RGB'))
            if clip in ('groom','eat','treat'):
                cmp=Image.new('RGBA',(500,190),BACKGROUND);dc=ImageDraw.Draw(cmp)
                dc.text((5,5),'Uniform grid / cell-bottom anchor',fill='white')
                dc.text((255,5),'Registered source / cat support',fill='white')
                dc.line((0,165,500,165),fill=(125,135,145,255))
                place(cmp,image,entry,index,120,165,naive=True)
                place(cmp,image,entry,index,370,165)
                comparison.append(cmp.convert('RGB'))
        frames[0].save(output/(clip+'-registered.gif'),save_all=True,append_images=frames[1:],duration=80,loop=0,disposal=2)
        if comparison:comparison[0].save(output/(clip+'-registration-comparison.gif'),save_all=True,append_images=comparison[1:],duration=80,loop=0,disposal=2)
        report['clips'][clip]={'sha256':digest(path),'size':list(image.size),'frames':len(entry['frames']),
                               'reference_height':entry['reference_height'],'sample_count':len(frames),
                               'adjacent_major_silhouette_pixels':0,'destination_rects':cells}
    # Alternate grooming families and a full two-minute resting cycle use
    # the same compiled sampler. Their GIF delays retain real elapsed time.
    by_clip={entry['clip']:entry for entry in entries}
    for key in ('groom-v1','groom-v2','sleep-wake','sleep-rest'):
        if key not in samples:continue
        clip=timeline['sequences'][key]['clip'];entry=by_clip[clip]
        image=runtime_image((manifest.parent/entry['source']).resolve())
        frames=[sequence_panel(image,entry,key,sample).convert('RGB') for sample in samples[key]]
        frames[0].save(output/(key+'-registered.gif'),save_all=True,append_images=frames[1:],
            duration=timeline['sequences'][key]['sample_step_ms'],loop=0,disposal=2)
    neutral.convert('RGB').save(output/'neutral-size-comparison.png')
    combined=Image.new('RGB',(2000,1900),BACKGROUND[:3]);draw=ImageDraw.Draw(combined)
    for index,entry in enumerate(entries):
        clip=entry['clip'];panel=Image.open(output/(clip+'-110px.png'));panel.thumbnail((990,440))
        x,y=(index%2)*1000,(index//2)*475;combined.paste(panel,(x,y+20));draw.text((x,y),clip,fill='white')
    combined.save(output/'all-contacts.jpg')
    (output/'review.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'manifest':str(manifest),'output':str(output),'clips':len(entries),
                      'sampler_sha256':report['sampler_sha256']}))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=ROOT/'build/processed-care')
    parser.add_argument('--manifest',type=Path,default=ROOT/'build/processed-care/manifest.json')
    parser.add_argument('--out',type=Path,default=ROOT/'build/previews/care-sprites')
    parser.add_argument('--register',action='store_true')
    parser.add_argument('--preview-prefix',type=Path,help='Optional GIF/PNG/JSON developer-preview export prefix')
    args=parser.parse_args()
    args.source_dir=args.source_dir.resolve();args.manifest=args.manifest.resolve();args.out=args.out.resolve()
    if args.register:register(args.source_dir,args.manifest)
    report=review(args.manifest,args.out)
    if args.preview_prefix:export_summary(args.manifest,args.out,report,args.preview_prefix)


if __name__=='__main__':main()
