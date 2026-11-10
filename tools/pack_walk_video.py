#!/usr/bin/env python3
"""Pack retained original video poses with fixed per-action registration.

packing.json is the editable source: native video frame IDs, common crop/root,
reference height and intro/loop/outro exposures. No new poses are synthesized.
All decoding, matting and appearance authoring happen offline.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/natural-gait'
RESOURCES = {'walk': 400, 'run': 401, 'eat': 118}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source(path, role):
    return {'role': role, 'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path)}

def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')

def pack(directory, clip, character):
    folder = directory / clip['rgba'][character]
    crop = clip['crop']
    scale = clip['scale']
    cell = (round((crop[2]-crop[0])*scale), round((crop[3]-crop[1])*scale))
    columns = clip.get('columns', 8)
    indices = clip['indices']
    rows = math.ceil(len(indices)/columns)
    root = [(clip['anchor'][0]-crop[0])*scale, (clip['anchor'][1]-crop[1])*scale]
    if clip.get('mirror'): root[0] = cell[0]-root[0]
    atlas = Image.new('RGBA', (cell[0]*columns, cell[1]*rows))
    records = []
    for ordinal, original in enumerate(indices):
        path = folder / f'{original:03}.png'
        with Image.open(path) as image:
            if image.mode != 'RGBA' or image.size != tuple(clip['source_canvas']):
                raise ValueError(f'Wrong native source canvas: {path}')
            bounds = image.getchannel('A').getbbox()
            if bounds and (bounds[0]<crop[0] or bounds[1]<crop[1] or bounds[2]>crop[2] or bounds[3]>crop[3]):
                raise ValueError(f'Crop clips source alpha at frame {original}: {bounds} vs {crop}')
            frame = image.crop(crop).resize(cell, Image.Resampling.LANCZOS)
            if clip.get('mirror'): frame=frame.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        x,y = ordinal%columns*cell[0], ordinal//columns*cell[1]
        atlas.paste(frame,(x,y))
        records.append({'source_frame_index':original,'source_png_sha256':sha(path),
                        'rect':[x,y,*cell],'anchor':root})
    return atlas, records, columns, rows

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,default=DIRECTORY)
    args=parser.parse_args()
    directory=args.directory.resolve()
    config_path=directory/'packing.json'
    config=json.loads(config_path.read_text(encoding='utf-8'))
    entries,evidence,proofs,videos=[],[],[],{}
    sources=[source(Path(__file__).resolve(),'importer'), source(config_path,'packing'),
             source(directory/'contact-reference.json','contacts'),
             source(ROOT/'tools/matte_walk_video.py','matting_importer'),
             source(ROOT/'tools/video_tuxedo/transfer.py','appearance_importer')]
    for clip in config['clips']:
        name=clip['clip']; original_alpha=None
        role='input_video' if name=='walk' else 'input_video_'+name
        sources.append(source(directory/clip['video'],role))
        for character in ('cream','tuxedo'):
            atlas,records,columns,rows=pack(directory,clip,character)
            alpha=atlas.getchannel('A').tobytes()
            if original_alpha is not None and original_alpha!=alpha:
                raise ValueError('Appearance transfer changed the original silhouette')
            original_alpha=alpha
            path=directory/f'{character}-video-{name}.png';atlas.save(path)
            evidence.append({'clip':name,'character':0 if character=='cream' else 8,
                             'base_resource_id':RESOURCES[name], 'source':path.name,'sha256':sha(path),
                             'image_width':atlas.width,'image_height':atlas.height})
            write_json(directory/f'{character}-{name}-frame-sources.json',records)
            if character=='cream':
                entry={'clip':name,'character':0,'source':path.name,'columns':columns,'rows':rows,
                       'frame_count':len(records),'reference_height':clip['reference_height']*clip['scale'],
                       'frames':[{k:r[k] for k in ('rect','anchor')} for r in records], 'sha256':sha(path),
                       'stride_pixels':clip.get('stride_pixels',0),'standing_frame':clip.get('standing_frame',0),
                       'sequence':clip['sequence']}
                if clip.get('movement_delay_ms') is not None:
                    entry['movement_delay_ms']=clip['movement_delay_ms']
                entries.append(entry)
        cycle_frames=len({s['frame'] for s in clip['sequence']['loop']})
        proofs.append({'clip':name,'rendered_frames':len(clip['indices']),'cycle_frames':cycle_frames,
                       'standing_frame':clip.get('standing_frame',0),'stride_pixels':clip.get('stride_pixels',0),
                       'reference_height':clip['reference_height']*clip['scale']})
        videos[name]={'source_fps':clip['source_fps'],'source_frame_count':clip['source_frame_count'],
                      'source_canvas':clip['source_canvas'],'source_role':role,
                      'selected_frame_indices':clip['indices'],'crop':clip['crop'],'global_scale':clip['scale'],
                      'mirror':clip.get('mirror',False)}
    manifest=directory/'manifest.json';write_json(manifest,{'schema':2,'clips':entries})
    walk=next(c for c in config['clips'] if c['clip']=='walk')
    # Retain the original walking proof alias for existing downloadable captures.
    walk_video=dict(videos['walk']);walk_video['selected_frame_indices']={'walk':walk['indices']}
    walk_video['nominal_walk_speed_logical_px_s']=walk['stride_pixels']*walk['source_fps']/len(walk['sequence']['loop'])
    write_json(directory/'provenance.json',{'schema':1,'method':'authored_video_extraction',
        'precomputed_only':True,'manifest_sha256':sha(manifest),'sources':sources,'atlases':evidence,
        'videos':videos,'video':walk_video,'appearance_method':'authored_tuxedo_material_fields_over_unchanged_video_geometry',
        'pose_interpolation':False,'clips':proofs})
    print(json.dumps({'status':'packed','clips':proofs,'pose_interpolation':False}))

if __name__=='__main__': main()
