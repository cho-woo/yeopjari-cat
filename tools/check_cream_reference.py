#!/usr/bin/env python3
"""Check the retained user-selected eight-pose Cream design with stdlib only."""
from __future__ import annotations
import hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=Path('assets/cream-design-reference.json')

def check(root=ROOT):
    root=Path(root);reference=json.loads((root/MANIFEST).read_text())
    if reference.get('schema')!=1 or reference.get('source')!='assets/cat.png':raise ValueError('Invalid Cream reference binding')
    raw=(root/reference['source']).read_bytes()
    if not raw.startswith(b'\x89PNG\r\n\x1a\n') or len(raw)<33:raise ValueError('Invalid Cream reference PNG')
    if hashlib.sha256(raw).hexdigest()!=reference['sha256']:raise ValueError('User-selected Cream design changed')
    width,height=struct.unpack_from('>II',raw,16)
    if [width,height]!=reference['canvas'] or raw[24:26]!=bytes([8,6]):raise ValueError('Cream reference canvas/format changed')
    frames=reference.get('frames',[])
    if len(frames)!=8 or [f['frame'] for f in frames]!=list(range(8)):raise ValueError('Eight original Cream poses required')
    for frame in frames:
        x,y,w,h=frame['rect']
        if not (w>0 and h>0 and x>=0 and y>=0 and x+w<=width and y+h<=height):raise ValueError('Invalid Cream reference registration')
    return {'status':'passed','source':reference['source'],'sha256':reference['sha256'],'frames':len(frames),'dependencies':'Python standard library only'}

if __name__=='__main__':print(json.dumps(check()))
