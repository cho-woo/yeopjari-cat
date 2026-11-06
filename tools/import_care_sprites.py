#!/usr/bin/env python3
"""Validate/import original transparent PNG action sheets without editing pixels.

Manifest schema1: {"schema":1,"clips":[{"clip":"groom","character":0,
"source":"groom.png","columns":4,"rows":4,"reference_height":360,
"anchor":[160,300]}]}. Instead of equal-grid rectangles, supply `frames` with
{rect:[x,y,width,height],anchor:[localX,localY]} for every frame. One common
reference_height controls the clip; individual poses are never height-normalized.
Explicit rectangles should exclude captions. The importer never guesses or
removes white/black/checkerboard backgrounds and never generates missing art.

  python3 tools/import_care_sprites.py --manifest /path/to/manifest.json
  python3 tools/import_care_sprites.py --check

PNG files are copied byte-for-byte into assets/care-sprites. A canonical manifest
there makes later --check invocations reproducible. No manifest and empty generated
files is a supported default, using the application's existing artwork. Explicit
missing manifests, opaque images and generated-file drift fail closed.
"""
from __future__ import annotations

import argparse
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
MAX_BYTES=32*1024*1024
MAX_PIXELS=32*1024*1024
MAX_SIDE=16384
HEADER='src/care_sprites.generated.hpp'
RC='src/care_sprites.generated.rc'
CANONICAL='assets/care-sprites/manifest.json'

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


def load_entries(manifest_path):
    if manifest_path is None:return []
    require(manifest_path.is_file(),f'Missing manifest: {manifest_path}')
    require(manifest_path.stat().st_size<=1024*1024,'Manifest exceeds1MiB')
    try:manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object)
    except (OSError,UnicodeError,ValueError) as error:raise ImportError('Cannot read manifest JSON') from error
    require(isinstance(manifest,dict) and set(manifest)=={'schema','clips'} and type(manifest['schema']) is int and manifest['schema']==1,
            'Manifest requires schema1 and clips')
    require(isinstance(manifest['clips'],list) and len(manifest['clips'])<=80,'clips must be a list with at most80 entries')
    entries=[];seen=set()
    for index,entry in enumerate(manifest['clips']):
        label=f'clips[{index}]'
        required={'clip','character','source','columns','rows','reference_height'}
        require(isinstance(entry,dict) and required<=set(entry) and set(entry)<=required|{'frames','anchor','sha256'},f'{label}: unsupported or missing fields')
        name=entry['clip'];require(isinstance(name,str) and name in LAYOUT,f'{label}: unknown clip')
        clip_index,enum,columns,rows=LAYOUT[name];character=integer(entry['character'],label+'.character',0,9)
        require((name,character) not in seen,f'{label}: duplicate character/clip')
        seen.add((name,character))
        permitted={(columns,rows)}|({(4,2)} if name=='sleep' else set())
        require(type(entry['columns']) is int and type(entry['rows']) is int and (entry['columns'],entry['rows']) in permitted,
                f'{label}: {name} grid must be one of {sorted(permitted)}')
        columns,rows=entry['columns'],entry['rows']
        require(isinstance(entry['source'],str) and entry['source'] and '\0' not in entry['source'],f'{label}: PNG source path required')
        source=(manifest_path.parent/entry['source']).resolve()
        require(source.suffix.lower()=='.png' and source.is_file(),f'{label}: missing original PNG: {source}')
        require(source.stat().st_size<=MAX_BYTES,f'{label}: PNG exceeds32MiB')
        data=source.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if 'sha256' in entry:require(entry['sha256']==digest,f'{label}: original PNG checksum mismatch')
        width,height,alpha=png_alpha(data)
        require(width>=columns and height>=rows,f'{label}: image is smaller than its frame grid')
        reference=number(entry['reference_height'],label+'.reference_height',.000001,MAX_SIDE)
        raw_frames=entry.get('frames')
        require(raw_frames is None or isinstance(raw_frames,list) and len(raw_frames)==columns*rows,f'{label}: wrong frame count')
        frames=[]
        for frame_index in range(columns*rows):
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
            frames.append({'rect':[x,y,fw,fh],'anchor':fixed_anchor})
        filename=f'c{character}-{name}.png'
        entries.append({'clip':name,'enum':enum,'character':character,'resource':113+character*8+clip_index,
                        'columns':columns,'rows':rows,'reference_height':reference,'width':width,'height':height,
                        'frames':frames,'filename':filename,'sha256':digest,'data':data})
    return sorted(entries,key=lambda value:(value['character'],LAYOUT[value['clip']][0]))


def numeric(value):
    return repr(value) if type(value) is float else str(value)


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
        for entry in entries:
            header+='    Descriptor{cat::sprite_sequence::Clip::'+entry['enum']+','+','.join(str(entry[k]) for k in ['character','resource','columns','rows'])+','
            header+=f'{len(entry["frames"])},{entry["width"]},{entry["height"]},{numeric(entry["reference_height"])},{{{{\n'
            for frame in entry['frames']:
                header+='        Frame{{'+','.join(map(str,frame['rect']))+'},{'+','.join(map(numeric,frame['anchor']))+'}},\n'
            header+='    }},"'+entry['sha256']+'","assets/care-sprites/'+entry['filename']+'"},\n'
            rc+=f'{entry["resource"]} RCDATA "../assets/care-sprites/{entry["filename"]}"\n'
        header+='}};\n'
    header+='}\n'
    return header.encode(),rc.encode()


def prepare_import(root,manifest_path=None,check=False):
    root=Path(root).resolve();default=root/CANONICAL
    selected=Path(manifest_path).resolve() if manifest_path is not None else default if default.is_file() else None
    entries=load_entries(selected);header,rc=generated(entries)
    outputs={root/HEADER:header,root/RC:rc}
    for entry in entries:outputs[root/'assets/care-sprites'/entry['filename']]=entry['data']
    if selected is not None:
        canonical={'schema':1,'clips':[{key:entry[key] for key in ('clip','character','columns','rows','reference_height','frames','sha256')}|
                    {'source':entry['filename']} for entry in entries]}
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
            'resource_ids':[entry['resource'] for entry in entries]}


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
