#!/usr/bin/env python3
"""Validate/import original transparent PNG action sheets without editing pixels.

Manifest schema1: {"schema":1,"clips":[{"clip":"groom","character":0,
"source":"groom.png","columns":4,"rows":4,"reference_height":360,
"anchor":[160,300]}]}. Instead of equal-grid rectangles, supply `frames` with
{rect:[x,y,width,height],anchor:[localX,localY]} for every frame. One common
reference_height controls the clip; individual poses are never height-normalized.
Explicit rectangles should exclude captions. The importer never guesses or
removes white/black/checkerboard backgrounds and never generates missing art.

Schema2 adds independent frame_count (1..256), arbitrary bounded grids and an
explicit sequence: intro/loop/outro lists of {frame,ms}, finish (hold/complete),
optional duration_ms and events [{step,offset_ms,type}]. Event step addresses
the concatenated intro, loop and outro lists. Walk/run additionally require
stride_pixels: logical travel per complete cycle, independent of frame count.
Optional standing_frame selects the planted pose. Walk/run step ms are distance
weights, not an independent wall-time animation clock.

  python3 tools/import_care_sprites.py --manifest /path/to/manifest.json
  python3 tools/import_care_sprites.py --check

PNG files are copied byte-for-byte into assets/care-sprites. A canonical manifest
there makes later --check invocations reproducible. No manifest and empty generated
files is a supported default, using the application's existing artwork. Explicit
missing manifests, opaque images and generated-file drift fail closed.

Default imports also select the precomputed derived-motion Walk/Run override
when present. Its separate source registration/provenance retain the original
eight drawings; the original care manifest and PNG are never replaced by the
derived atlas. An explicit --manifest retains the ordinary source-import behavior.
"""
from __future__ import annotations

import argparse
from collections import OrderedDict
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
CLIPS=(('groom','Groom',4,4),('sleep','Sleep',4,3),('box','Box',4,2),('fish','Fish',4,2),
       ('punch','Punch',4,3),('eat','Eat',3,4),('jump','Jump',4,4),('treat','Treat',6,4))
LAYOUT={name:(index,enum,columns,rows) for index,(name,enum,columns,rows) in enumerate(CLIPS)}
LAYOUT.update({'walk':(9,'Walk',4,2),'run':(10,'Run',4,2)})
MAX_FRAMES=256
MAX_STEPS=512
MAX_CUES=64
MAX_DURATION_MS=24*60*60*1000
MAX_BYTES=32*1024*1024
MAX_PIXELS=32*1024*1024
MAX_RESIDENT_PIXELS=48*1024*1024
MAX_SIDE=16384
HEADER='src/care_sprites.generated.hpp'
RC='src/care_sprites.generated.rc'
CANONICAL='assets/care-sprites/manifest.json'
DERIVED='assets/derived-motion/manifest.json'
NATURAL='assets/natural-gait/manifest.json'

class ImportError(ValueError):
    pass


def require(condition,message):
    if not condition:raise ImportError(message)


def integer(value,label,low,high):
    require(type(value) is int and low<=value<=high,f'{label}: integer {low}..{high} required')
    return value


def number(value,label,low,high):
    require(type(value) in (int,float) and low<=value<=high and (type(value) is int or math.isfinite(value)),
            f'{label}: finite number {low}..{high} required')
    return value


def unique_object(pairs):
    result={}
    for key,value in pairs:
        require(key not in result,f'Duplicate JSON key: {key}')
        result[key]=value
    return result


def png_alpha(data):
    """Decode RGBA8 alpha only. PNG filters operate independently per channel."""
    require(len(data)<=MAX_BYTES and data.startswith(b'\x89PNG\r\n\x1a\n'),'Expected a PNG no larger than32MiB')
    position=8;ihdr=None;idat=[];ended=False;seen_idat=False;idat_closed=False;seen_palette=False
    while position<len(data):
        require(position+12<=len(data),'Truncated PNG chunk')
        size=struct.unpack_from('>I',data,position)[0];kind=data[position+4:position+8];end=position+8+size
        require(end+4<=len(data),'Truncated PNG chunk payload')
        require(all(65<=c<=90 or 97<=c<=122 for c in kind) and not(kind[2]&32),"Invalid PNG chunk type")
        payload=data[position+8:end]
        require(zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',data,end)[0],'PNG CRC mismatch')
        if ihdr is None:require(kind==b'IHDR','PNG IHDR must be first')
        if kind==b'IHDR':
            require(ihdr is None and size==13,'Duplicate or invalid PNG IHDR')
            ihdr=struct.unpack('>IIBBBBB',payload);width,height,depth,color,compression,filtering,interlace=ihdr
            require(0<width<=MAX_SIDE and 0<height<=MAX_SIDE and width*height<=MAX_PIXELS,'PNG dimensions exceed import limits')
            require((depth,color,compression,filtering,interlace)==(8,6,0,0,0),
                    'Original PNG must be non-interlaced8-bit RGBA with real transparency')
        elif kind==b'IDAT':
            require(not ended and not idat_closed,'PNG IDAT chunks must be consecutive');idat.append(payload);seen_idat=True
        elif kind==b'IEND':
            require(size==0 and seen_idat and end+4==len(data),'Invalid PNG end')
            ended=True;position=end+4;break
        elif kind==b'PLTE':
            require(not seen_palette and not seen_idat and 3<=size<=768 and size%3==0,'Invalid PNG palette');seen_palette=True
        elif kind==b'tRNS':raise ImportError('RGBA PNG must not contain a separate transparency table')
        elif kind in (b'acTL',b'fcTL',b'fdAT'):
            raise ImportError('Animated PNG is not a static action sheet')
        elif not(kind[0]&32):
            raise ImportError('Unsupported critical PNG chunk')
        if seen_idat and kind!=b'IDAT':idat_closed=True
        position=end+4
    require(ended and ihdr is not None,'Incomplete PNG')
    width,height=ihdr[:2];stride=width*4;expected=(stride+1)*height
    decoder=zlib.decompressobj()
    try:raw=decoder.decompress(b''.join(idat),expected+1)
    except zlib.error as error:raise ImportError('Invalid PNG compression') from error
    require(len(raw)==expected and decoder.eof and not decoder.unconsumed_tail and not decoder.unused_data,
            'PNG decompressed size mismatch')
    alpha=bytearray(width*height);previous=bytearray(width)
    for y in range(height):
        start=y*(stride+1);kind=raw[start];row=bytearray(raw[start+4:start+stride+1:4])
        require(kind<=4,'Invalid PNG row filter')
        if kind:
            for x in range(width):
                left=row[x-1] if x else 0;up=previous[x];upper_left=previous[x-1] if x else 0
                if kind==1:predict=left
                elif kind==2:predict=up
                elif kind==3:predict=(left+up)//2
                else:
                    estimate=left+up-upper_left;distances=(abs(estimate-left),abs(estimate-up),abs(estimate-upper_left))
                    predict=left if distances[0]<=distances[1] and distances[0]<=distances[2] else up if distances[1]<=distances[2] else upper_left
                row[x]=(row[x]+predict)&255
        alpha[y*width:(y+1)*width]=row;previous=row
    require(min(alpha)<=16 and max(alpha)>16,'PNG must contain actual transparent background and visible artwork; no background is guessed')
    return width,height,alpha


def anchor(value,width,height,label):
    require(isinstance(value,list) and len(value)==2,f'{label}: [x,y] required')
    return [number(value[0],label+'.x',0,width),number(value[1],label+'.y',0,height)]


def sequence(value,frame_count,label):
    fields={'intro','loop','outro','finish','duration_ms','events'}
    require(isinstance(value,dict) and set(value)<=fields,f'{label}: unsupported sequence fields')
    steps=[];ranges=[];canonical={}
    for section in ('intro','loop','outro'):
        raw=value.get(section,[])
        require(isinstance(raw,list),f'{label}.{section}: timed step list required')
        first=len(steps);canonical[section]=[]
        for index,step in enumerate(raw):
            name=f'{label}.{section}[{index}]'
            require(isinstance(step,dict) and set(step)=={'frame','ms'},f'{name}: frame and ms required')
            normalized={'frame':integer(step['frame'],name+'.frame',0,frame_count-1),
                        'ms':integer(step['ms'],name+'.ms',1,60000)}
            steps.append(normalized);canonical[section].append(normalized)
        ranges.append((first,len(raw)))
    require(1<=len(steps)<=MAX_STEPS,f'{label}: 1..{MAX_STEPS} timed steps required')
    finish=value.get('finish','complete')
    require(finish in ('hold','complete'),f'{label}.finish: hold or complete required')
    duration=integer(value.get('duration_ms',0),label+'.duration_ms',0,MAX_DURATION_MS)
    boundaries=sum(step['ms'] for name in ('intro','outro') for step in canonical[name])
    require(duration==0 or duration>=boundaries,f'{label}: duration_ms truncates the entrance or ending')
    raw_events=value.get('events',[])
    require(isinstance(raw_events,list) and len(raw_events)<=MAX_CUES,f'{label}: at most {MAX_CUES} events allowed')
    event_bits={'contact':1,'takeoff':2,'landing':4,'marker':8}
    cues=[];events=[];seen=set()
    for index,cue in enumerate(raw_events):
        name=f'{label}.events[{index}]'
        require(isinstance(cue,dict) and set(cue)=={'step','offset_ms','type'},f'{name}: step, offset_ms and type required')
        step=integer(cue['step'],name+'.step',0,len(steps)-1)
        offset=integer(cue['offset_ms'],name+'.offset_ms',0,steps[step]['ms']-1)
        require(isinstance(cue['type'],str) and cue['type'] in event_bits,f'{name}: unsupported event type')
        key=(step,offset,cue['type']);require(key not in seen,f'{name}: duplicate event')
        seen.add(key);cues.append((step,offset,event_bits[cue['type']]))
        events.append({'step':step,'offset_ms':offset,'type':cue['type']})
    canonical.update({'finish':finish,'duration_ms':duration,'events':events})
    return {'steps':steps,'ranges':ranges,'finish':finish,'duration_ms':duration,'cues':cues,'canonical':canonical}


def load_entries(manifest_path):
    if manifest_path is None:return []
    require(manifest_path.is_file(),f'Missing manifest: {manifest_path}')
    require(manifest_path.stat().st_size<=1024*1024,'Manifest exceeds1MiB')
    try:manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object)
    except (OSError,UnicodeError,ValueError) as error:raise ImportError('Cannot read manifest JSON') from error
    require(isinstance(manifest,dict) and set(manifest)=={'schema','clips'} and type(manifest['schema']) is int and manifest['schema'] in (1,2),
            'Manifest requires schema1 or schema2 and clips')
    schema=manifest['schema']
    require(isinstance(manifest['clips'],list) and len(manifest['clips'])<=100,'clips must be a list with at most100 entries')
    entries=[];seen=set();resident_pixels=0
    # Keep alpha validation bounded while decoding a repeated/shared sheet only
    # once. The cache stores alpha, not a second RGBA copy of every source.
    decoded=OrderedDict();cached_pixels=0
    for index,entry in enumerate(manifest['clips']):
        label=f'clips[{index}]'
        required={'clip','character','source','columns','rows','reference_height'}
        optional={'frames','anchor','sha256'}|({'frame_count','sequence','stride_pixels','standing_frame','movement_delay_ms'} if schema==2 else set())
        require(isinstance(entry,dict) and required<=set(entry) and set(entry)<=required|optional,f'{label}: unsupported or missing fields')
        name=entry['clip'];require(isinstance(name,str) and name in LAYOUT,f'{label}: unknown clip')
        require(schema==2 or name not in ('walk','run'),f'{label}: locomotion requires schema2 and a sequence')
        clip_index,enum,columns,rows=LAYOUT[name];character=integer(entry['character'],label+'.character',0,9)
        require((name,character) not in seen,f'{label}: duplicate character/clip')
        seen.add((name,character))
        permitted={(columns,rows)}|({(4,2)} if name=='sleep' else set())
        if schema==1:
            require(type(entry['columns']) is int and type(entry['rows']) is int and (entry['columns'],entry['rows']) in permitted,
                    f'{label}: {name} grid must be one of {sorted(permitted)}')
        columns=integer(entry['columns'],label+'.columns',1,MAX_FRAMES)
        rows=integer(entry['rows'],label+'.rows',1,MAX_FRAMES)
        require(columns*rows<=MAX_FRAMES,f'{label}: grid capacity exceeds {MAX_FRAMES}')
        require(isinstance(entry['source'],str) and entry['source'] and '\0' not in entry['source'],f'{label}: PNG source path required')
        source=(manifest_path.parent/entry['source']).resolve()
        require(source.suffix.lower()=='.png' and source.is_file(),f'{label}: missing original PNG: {source}')
        require(source.stat().st_size<=MAX_BYTES,f'{label}: PNG exceeds32MiB')
        data=source.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if 'sha256' in entry:require(entry['sha256']==digest,f'{label}: original PNG checksum mismatch')
        if digest in decoded:
            width,height,alpha=decoded.pop(digest);decoded[digest]=(width,height,alpha)
        else:
            width,height,alpha=png_alpha(data)
            while decoded and cached_pixels+width*height>MAX_PIXELS:
                _,old=decoded.popitem(last=False);cached_pixels-=old[0]*old[1]
            decoded[digest]=(width,height,alpha);cached_pixels+=width*height
        require(width>=columns and height>=rows,f'{label}: image is smaller than its frame grid')
        reference=number(entry['reference_height'],label+'.reference_height',.000001,MAX_SIDE)
        raw_frames=entry.get('frames')
        require(raw_frames is None or isinstance(raw_frames,list),f'{label}: frames must be a list')
        count=integer(entry.get('frame_count',len(raw_frames) if raw_frames is not None else columns*rows),
                      label+'.frame_count',1,min(MAX_FRAMES,columns*rows))
        require(raw_frames is None or len(raw_frames)==count,f'{label}: wrong frame count')
        require(schema==2 or count==columns*rows,f'{label}: schema1 frame count must match its grid')
        program=sequence(entry['sequence'],count,label+'.sequence') if 'sequence' in entry else None
        require(program is not None or ((columns,rows) in permitted and count==columns*rows and name not in ('walk','run')),
                f'{label}: custom frame count or locomotion requires an explicit sequence')
        standing=integer(entry.get('standing_frame',0),label+'.standing_frame',0,count-1)
        stride=number(entry.get('stride_pixels',0),label+'.stride_pixels',0,4096)
        require(name not in ('walk','run') or stride>0,f'{label}: locomotion stride_pixels must be positive')
        movement_delay=integer(entry.get('movement_delay_ms',0),label+'.movement_delay_ms',0,MAX_DURATION_MS)
        intro_duration=sum(step['ms'] for step in program['canonical']['intro']) if program is not None else 0
        require(movement_delay<=intro_duration,f'{label}: movement_delay_ms exceeds the entrance duration')
        frames=[]
        for frame_index in range(count):
            frame_label=f'{label}.frames[{frame_index}]'
            if raw_frames is None:
                col,row=frame_index%columns,frame_index//columns
                x,y=col*width//columns,row*height//rows
                rect=[x,y,(col+1)*width//columns-x,(row+1)*height//rows-y]
                raw_anchor=entry.get('anchor',[rect[2]/2,rect[3]])
            else:
                value=raw_frames[frame_index]
                require(isinstance(value,dict) and 'rect' in value and set(value)<={'rect','anchor'},f'{frame_label}: rect/anchor required')
                rect=value['rect'];raw_anchor=value.get('anchor',entry.get('anchor'))
            require(isinstance(rect,list) and len(rect)==4,f'{frame_label}: [x,y,width,height] required')
            x=integer(rect[0],frame_label+'.x',0,width-1);y=integer(rect[1],frame_label+'.y',0,height-1)
            fw=integer(rect[2],frame_label+'.width',1,width-x);fh=integer(rect[3],frame_label+'.height',1,height-y)
            fixed_anchor=anchor(raw_anchor,fw,fh,frame_label+'.anchor')
            minimum=255;maximum=0
            for scanline in range(y,y+fh):
                values=alpha[scanline*width+x:scanline*width+x+fw];minimum=min(minimum,min(values));maximum=max(maximum,max(values))
            require(minimum<=16 and maximum>16,f'{frame_label}: frame must contain transparent padding and visible artwork')
            resident_pixels+=fw*fh
            require(resident_pixels<=MAX_RESIDENT_PIXELS,f'{label}: decoded frame storage exceeds192MiB resident budget')
            frames.append({'rect':[x,y,fw,fh],'anchor':fixed_anchor})
        filename=f'c{character}-{name}.png'
        # 301..314 remain reserved by the retired-audio compatibility checks.
        resource=400+character*2+(name=='run') if name in ('walk','run') else 113+character*8+clip_index
        entries.append({'clip':name,'enum':enum,'character':character,'resource':resource,'schema':schema,
                        'columns':columns,'rows':rows,'reference_height':reference,'width':width,'height':height,
                        'frames':frames,'filename':filename,'sha256':digest,'data':data,'program':program,'stride_pixels':stride,
                        'standing_frame':standing,'movement_delay_ms':movement_delay})
    entries.sort(key=lambda value:(value['character'],LAYOUT[value['clip']][0]))
    for entry in entries:
        if entry['clip']=='run':
            walking=next((value for value in entries if value['clip']=='walk' and value['character']==entry['character']),None)
            if walking and walking['sha256']==entry['sha256']:
                entry['resource']=walking['resource'];entry['filename']=walking['filename'];entry['data']=walking['data']
    return entries


def numeric(value):
    return repr(value) if type(value) is float else str(value)


def natural_locomotion(root,originals):
    """Select independently authored/video action poses without replacing source art.

    Unlike the archived optical-flow bake, this registration owns its measured
    stride, reference height, frame count and root anchors. Its manifest is the
    activation point: an authoring project folder alone changes nothing.
    """
    root=Path(root).resolve();manifest=root/NATURAL
    if not manifest.is_file():return originals,0
    directory=manifest.parent
    provenance=directory/'provenance.json'
    require(provenance.is_file(),'Missing natural gait provenance')
    require(provenance.stat().st_size<=1024*1024,'Natural gait provenance exceeds1MiB')
    try:report=json.loads(provenance.read_text(encoding='utf-8'),object_pairs_hook=unique_object)
    except (OSError,UnicodeError,ValueError) as error:raise ImportError('Cannot read natural gait provenance') from error
    require(isinstance(report,dict) and report.get('schema')==1 and
            report.get('method') in ('skeletal_3d_bake','skeletal_2d_bake','authored_pose_bake','authored_video_extraction') and
            report.get('precomputed_only') is True,'Invalid natural gait provenance')
    def verify(path,expected,label):
        require(path.is_file() and isinstance(expected,str),f'Natural gait {label} drift or missing file')
        digest=hashlib.sha256()
        with path.open('rb') as source:
            for block in iter(lambda:source.read(1024*1024),b''):digest.update(block)
        require(digest.hexdigest()==expected,f'Natural gait {label} drift or missing file')
    def bounded_path(base,relative,label):
        require(isinstance(relative,str) and relative and not Path(relative).is_absolute(),
                f'Natural gait {label} path must be relative')
        path=(base/relative).resolve()
        require(path.is_relative_to(root),f'Natural gait {label} must stay inside the project')
        return path
    verify(manifest,report.get('manifest_sha256'),'manifest')
    sources=report.get('sources')
    require(isinstance(sources,list) and 3<=len(sources)<=32,'Natural gait authoring sources required')
    roles=set()
    for source in sources:
        require(isinstance(source,dict) and isinstance(source.get('role'),str) and
                source['role'] not in roles,'Natural gait duplicate or invalid source role')
        roles.add(source['role'])
        path=bounded_path(root,source.get('path'),'source')
        verify(path,source.get('sha256'),source['role'])
    video_method=report.get('method')=='authored_video_extraction'
    require(({'importer','contacts'} if video_method else {'generator','project','contacts'})<=roles and
            (not video_method or any(role=='input_video' or role.startswith('input_video_') for role in roles)),
            'Natural gait authoring/video, importer and contacts sources required')
    atlases=report.get('atlases')
    require(isinstance(atlases,list) and 1<=len(atlases)<=20,'Natural gait atlas evidence required')
    registered={}
    for atlas in atlases:
        require(isinstance(atlas,dict),'Natural gait atlas record required')
        path=bounded_path(directory,atlas.get('source'),'atlas')
        require(path.is_relative_to(directory) and path.suffix.lower()=='.png',
                'Natural gait atlas must be a PNG inside its asset directory')
        require(path not in registered,'Natural gait duplicate atlas evidence')
        verify(path,atlas.get('sha256'),'atlas')
        registered[path]=atlas
    replacements=load_entries(manifest)
    allowed=('walk','run','eat') if video_method else ('walk','run')
    require(1<=len(replacements)<=len(allowed) and all(entry['character']==0 and entry['clip'] in allowed for entry in replacements),
            'Natural action override contains an unsupported character or clip')
    raw=json.loads(manifest.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object)
    paths={(entry['clip'],entry['character']):bounded_path(directory,entry['source'],'clip atlas')
           for entry in raw['clips']}
    evidence=report.get('clips')
    require(isinstance(evidence,list) and len(evidence)==len(replacements),'Natural gait clip evidence required')
    for entry in replacements:
        require(any(base['clip']==entry['clip'] and base['character']==0 for base in originals),
                'Natural gait cannot replace an absent original clip')
        path=paths[(entry['clip'],entry['character'])]
        require(path in registered,'Natural gait clip has no atlas evidence')
        atlas=registered[path]
        require(entry['sha256']==atlas.get('sha256') and
                entry['width']==atlas.get('image_width') and entry['height']==atlas.get('image_height'),
                'Natural gait atlas registration mismatch')
        proofs=[proof for proof in evidence if isinstance(proof,dict) and proof.get('clip')==entry['clip']]
        require(len(proofs)==1,'Natural gait duplicate or absent clip evidence')
        proof=proofs[0];program=entry['program']
        require(program is not None and (entry['clip'] not in ('walk','run') or program['ranges'][1][1]>=2),
                'Natural action needs a program and locomotion needs a walking cycle')
        first,count=program['ranges'][1]
        cycle_frames=len({step['frame'] for step in program['steps'][first:first+count]})
        require(len(entry['frames'])==proof.get('rendered_frames') and
                cycle_frames==proof.get('cycle_frames') and
                entry['standing_frame']==proof.get('standing_frame') and
                entry['stride_pixels']==proof.get('stride_pixels') and
                entry['reference_height']==proof.get('reference_height'),
                'Natural gait clip registration mismatch')
        if video_method:
            videos=report.get('videos')
            if isinstance(videos,dict):
                video=videos.get(entry['clip'])
                require(isinstance(video,dict),'Natural action per-clip video metadata required')
                selected=video.get('selected_frame_indices')
                source_role=video.get('source_role')
            else:
                video=report.get('video');require(isinstance(video,dict),'Natural gait video metadata required')
                selections=video.get('selected_frame_indices');require(isinstance(selections,dict),'Natural video selections required')
                selected=selections.get(entry['clip'])
                source_role=video.get('source_role','input_video')
            require(isinstance(source_role,str) and source_role in roles and
                    (source_role=='input_video' or source_role.startswith('input_video_')),
                    'Natural video source role is not bound to an input video')
            fps=number(video.get('source_fps'),'Natural video source_fps',.000001,240)
            source_count=integer(video.get('source_frame_count'),'Natural video source_frame_count',1,1000000)
            require(isinstance(selected,list) and len(selected)==len(entry['frames']) and
                    all(type(index) is int and 0<=index<source_count for index in selected),
                    'Natural video source frame selection mismatch')
            entry['nominal_fps']=fps
        entry['asset_path']=path.relative_to(root).as_posix()
    replaced={(entry['clip'],entry['character']) for entry in replacements}
    result=[entry for entry in originals if (entry['clip'],entry['character']) not in replaced]+replacements
    # Previously shared Walk/Run resource400 must separate when only one clip
    # is replaced. Keep the remaining clip's actual source/rectangles intact.
    walk=next((entry for entry in result if entry['character']==0 and entry['clip']=='walk'),None)
    run=next((entry for entry in result if entry['character']==0 and entry['clip']=='run'),None)
    if walk is not None and run is not None and walk['sha256']!=run['sha256'] and walk['resource']==run['resource']:
        result=[dict(entry,resource=401) if entry is run else entry for entry in result]
    result.sort(key=lambda value:(value['character'],LAYOUT[value['clip']][0]))
    require(sum(frame['rect'][2]*frame['rect'][3] for entry in result for frame in entry['frames'])<=MAX_RESIDENT_PIXELS,
            'Natural gait frame storage exceeds192MiB resident budget')
    return result,len(replacements)


def runtime_locomotion(root,originals):
    # A new authored pose family takes precedence over the archived flow bake.
    manifest=Path(root)/NATURAL
    if manifest.is_file():
        try:raw=json.loads(manifest.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object)
        except (OSError,UnicodeError,ValueError) as error:raise ImportError('Cannot read natural gait manifest') from error
        require(isinstance(raw,dict) and isinstance(raw.get('clips'),list),'Natural gait clips required')
        keys={(entry.get('clip'),entry.get('character')) for entry in raw['clips'] if isinstance(entry,dict)}
        # A complete new family has no dependency on archived flow files. A
        # Walk-only replacement keeps the existing derived Run exactly.
        base=originals if {('walk',0),('run',0)}<=keys else derived_locomotion(root,originals)[0]
        return natural_locomotion(root,base)
    return derived_locomotion(root,originals)


def derived_locomotion(root,originals):
    """Select a checked derived resource without canonicalizing over originals."""
    directory=root/'assets/derived-motion';manifest=directory/'manifest.json'
    if not directory.exists():return originals,0
    require(manifest.is_file(),f'Missing derived locomotion manifest: {manifest}')
    provenance=directory/'provenance.json'
    require(provenance.is_file(),'Missing derived locomotion provenance')
    try:report=json.loads(provenance.read_text(encoding='utf-8'),object_pairs_hook=unique_object)
    except (OSError,UnicodeError,ValueError) as error:raise ImportError('Cannot read derived provenance') from error
    require(report.get('schema')==1 and report.get('precomputed_only') is True and
            report.get('whole_sprite_alpha_blend') is False,'Invalid derived motion provenance')
    def verify(path,expected,label):
        require(path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==expected,
                f'Derived {label} drift or missing file')
    verify(manifest,report.get('derived_manifest_sha256'),'manifest')
    for key in ('generator','source_registration','original_source'):
        relative=report.get(key)
        require(isinstance(relative,str) and not Path(relative).is_absolute(),f'Derived {key} path')
        path=(root/relative).resolve()
        require(path.is_relative_to(root.resolve()),f'Derived {key} must stay inside the project')
        verify(path,report.get(key+'_sha256'),key)
    atlas=report.get('atlas',{})
    require(isinstance(atlas,dict) and isinstance(atlas.get('source'),str) and
            Path(atlas['source']).name==atlas['source'],'Derived atlas filename')
    verify(directory/atlas['source'],atlas.get('sha256'),'atlas')
    replacements=load_entries(manifest)
    require(len(replacements)==2 and {(e['clip'],e['character']) for e in replacements}==
            {('walk',0),('run',0)},'Derived override must contain only Cream Walk and Run')
    evidence=report.get('clips',[])
    for entry in replacements:
        base=next((e for e in originals if e['clip']==entry['clip'] and e['character']==0),None)
        proof=next((e for e in evidence if e.get('clip')==entry['clip']),{})
        require(base is not None and len(base['frames'])==proof.get('source_frame_count')==
                proof.get('original_drawings')==8,'Derived motion must retain eight source drawings')
        require(len(entry['frames'])==proof.get('derived_frame_count')==64 and
                entry['sha256']==atlas.get('sha256') and base['sha256']==report.get('original_source_sha256'),
                'Derived frame count or source binding')
        require(entry['reference_height']==base['reference_height'] and entry['stride_pixels']==base['stride_pixels'],
                'Derived motion must preserve scale and distance-driven stride')
        require(all(f['anchor']==base['frames'][0]['anchor'] for f in entry['frames']),
                'Derived motion must preserve fixed foot registration')
        entry['asset_path']=(Path('assets/derived-motion')/atlas['source']).as_posix()
    replaced={(entry['clip'],entry['character']) for entry in replacements}
    result=[entry for entry in originals if (entry['clip'],entry['character']) not in replaced]+replacements
    result.sort(key=lambda value:(value['character'],LAYOUT[value['clip']][0]))
    require(sum(frame['rect'][2]*frame['rect'][3] for entry in result for frame in entry['frames'])<=MAX_RESIDENT_PIXELS,
            'Derived frame storage exceeds192MiB resident budget')
    return result,len(replacements)


def generated_program(program):
    if program is None:return 'cat::sprite_sequence::Program{}'
    scope='cat::sprite_sequence::'
    result=scope+'Program{'+str(len(program['steps']))+',{{\n'
    for step in program['steps']:
        result+='        '+scope+'Step{'+str(step['frame'])+','+str(step['ms'])+'},\n'
    result+='    }},'+','.join(scope+'Range{'+str(first)+','+str(count)+'}' for first,count in program['ranges'])
    result+=','+scope+'Finish::'+('Hold' if program['finish']=='hold' else 'Complete')
    result+=','+str(program['duration_ms'])+','+str(len(program['cues']))+',{{'
    result+=','.join(scope+'Cue{'+','.join(map(str,cue))+'}' for cue in program['cues'])
    return result+'}}}'


def generated(entries):
    header='// Generated by tools/import_care_sprites.py. Do not edit by hand.\n'
    rc='// Generated by tools/import_care_sprites.py. Do not edit by hand.\n'
    if not entries:
        header+='// No imported PNGs: the application keeps its existing action artwork.\n'
        rc+='// No imported PNGs: no additional bitmap resources are embedded.\n'
    header+='#pragma once\n#include "cat_sprite_assets.hpp"\nnamespace cat::sprite_assets {\n'
    if not entries:header+='inline constexpr std::array<Descriptor,0> kCareSprites{};\n'
    else:
        header+=f'inline constexpr std::array<Descriptor,{len(entries)}> kCareSprites{{{{\n'
        resources={}
        for entry in entries:
            header+='    Descriptor{cat::sprite_sequence::Clip::'+entry['enum']+','+','.join(str(entry[k]) for k in ['character','resource','columns','rows'])+','
            header+=f'{len(entry["frames"])},{entry["width"]},{entry["height"]},{numeric(entry["reference_height"])},{{{{\n'
            for frame in entry['frames']:
                header+='        Frame{{'+','.join(map(str,frame['rect']))+'},{'+','.join(map(numeric,frame['anchor']))+'}},\n'
            asset_path=entry.get('asset_path','assets/care-sprites/'+entry['filename'])
            header+='    }},"'+entry['sha256']+'","'+asset_path+'"'
            if entry['program'] is not None or entry['stride_pixels']:
                header+=','+generated_program(entry['program'])+','+numeric(entry['stride_pixels'])+','+str(entry['standing_frame'])
                if entry.get('nominal_fps',0) or entry.get('movement_delay_ms',0):header+=','+numeric(entry.get('nominal_fps',0))
                if entry.get('movement_delay_ms',0):header+=','+str(entry['movement_delay_ms'])
            header+='},\n'
            if entry['resource'] in resources:
                require(resources[entry['resource']]==entry['sha256'],'Conflicting PNGs share a resource identifier')
            else:
                resources[entry['resource']]=entry['sha256']
                rc+=f'{entry["resource"]} RCDATA "../{asset_path}"\n'
        header+='}};\n'
    header+='}\n'
    return header.encode(),rc.encode()


def prepare_import(root,manifest_path=None,check=False):
    root=Path(root).resolve();default=root/CANONICAL
    selected=Path(manifest_path).resolve() if manifest_path is not None else default if default.is_file() else None
    originals=load_entries(selected)
    entries,overrides=runtime_locomotion(root,originals) if manifest_path is None else (originals,0)
    natural=overrides if manifest_path is None and (root/NATURAL).is_file() else 0
    derived=overrides if not natural else 0
    header,rc=generated(entries)
    outputs={root/HEADER:header,root/RC:rc}
    for entry in originals:outputs[root/'assets/care-sprites'/entry['filename']]=entry['data']
    if selected is not None:
        schema=2 if any(entry['schema']==2 for entry in originals) else 1
        clips=[]
        for entry in originals:
            value={key:entry[key] for key in ('clip','character','columns','rows','reference_height','frames','sha256')}
            value['source']=entry['filename']
            if schema==2:
                value['frame_count']=len(entry['frames'])
                if entry['program'] is not None:value['sequence']=entry['program']['canonical']
                if entry['stride_pixels']:
                    value['stride_pixels']=entry['stride_pixels']
                    value['standing_frame']=entry['standing_frame']
                if entry.get('movement_delay_ms',0):value['movement_delay_ms']=entry['movement_delay_ms']
            clips.append(value)
        canonical={'schema':schema,'clips':clips}
        outputs[default]=(json.dumps(canonical,ensure_ascii=False,indent=2)+'\n').encode()
    # Complete preflight before touching any generated or copied file.
    if check:
        for path,data in outputs.items():require(path.is_file() and path.read_bytes()==data,f'Generated asset drift or missing file: {path.relative_to(root)}')
    else:
        ordered=sorted(outputs,key=lambda path:(path==root/HEADER,path==root/RC,str(path)))
        for path in ordered:
            data=outputs[path]
            if path.is_file() and path.read_bytes()==data:continue
            path.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.'+path.name+'.',delete=False) as temp:
                temporary=Path(temp.name)
                try:temp.write(data)
                except BaseException:temporary.unlink(missing_ok=True);raise
            try:os.replace(temporary,path)
            finally:temporary.unlink(missing_ok=True)
    return {'status':'verified' if check else 'imported','clips':len(entries),'original_png_bytes_preserved':True,
            'derived_locomotion_clips':derived,
            'natural_locomotion_clips':natural,
            'resource_ids':[entry['resource'] for entry in entries],
            'unique_sheets':len({entry['sha256'] for entry in entries}),
            'actual_frames':sum(len(entry['frames']) for entry in entries),
            'decoded_frame_bytes':sum(frame['rect'][2]*frame['rect'][3]*4 for entry in entries for frame in entry['frames'])}


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--manifest',type=Path,help='Original PNG manifest; defaults to assets/care-sprites/manifest.json when present')
    parser.add_argument('--check',action='store_true',help='Read-only validation of PNGs, metadata and generated-file drift')
    parser.add_argument('--root',type=Path,default=ROOT,help=argparse.SUPPRESS)
    args=parser.parse_args()
    try:result=prepare_import(args.root,args.manifest,args.check)
    except (ImportError,OSError) as error:print(f'Care sprite import failed: {error}',file=sys.stderr);return 1
    print(json.dumps(result,ensure_ascii=False));return 0

if __name__=='__main__':raise SystemExit(main())
