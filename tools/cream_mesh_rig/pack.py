#!/usr/bin/env python3
"""Pack already-rendered rig poses without inventing or deforming artwork.

python tools/cream_mesh_rig/pack.py build/cream-mesh-rig
The loop annotation entry excludes the separately rendered neutral stance.
The review manifest appends that stance without inserting it into the loop.
"""
from __future__ import annotations
import argparse,copy,json,math,hashlib
from pathlib import Path
from PIL import Image

FEET=('near_front','far_front','near_rear','far_rear')

def sample_sole(image,point):
    x,y=point['x'],point['y'];a=image.getchannel('A');samples=[]
    for yy in range(max(0,math.floor(y)-3),min(image.height,math.ceil(y)+4)):
        for xx in range(max(0,math.floor(x)-3),min(image.width,math.ceil(x)+4)):
            if (xx-x)**2+(yy-y)**2<=9.01:samples.append((a.getpixel((xx,yy)),xx,yy))
    solid=[v for v in samples if v[0]>128]
    return {'max_alpha_within_3_source_px':max((v[0] for v in samples),default=0),
            'solid_support':bool(solid),'nearest_solid_distance_source_px':min((math.hypot(xx-x,yy-y) for _,xx,yy in solid),default=None),
            'solid_bottom_y_source_px':max((yy for _,_,yy in solid),default=None)}

def pack(folder):
    path=folder/'annotations.json';doc=json.loads(path.read_text());count=len(doc['annotations']);standing=json.loads((folder/'standing-annotation.json').read_text())['annotation'];standing['frame']=count
    files=[folder/'frames'/f'frame-{i:04d}.png' for i in range(count)]+[folder/'standing.png']
    images=[Image.open(p).convert('RGBA') for p in files];assert all(im.size==(313,313) for im in images)
    columns=math.ceil(math.sqrt(len(images)));rows=math.ceil(len(images)/columns);atlas=Image.new('RGBA',(columns*313,rows*313));frames=[];coverage=[]
    for i,im in enumerate(images):
        x=(i%columns)*313;y=(i//columns)*313;atlas.paste(im,(x,y))
        annotation=(doc['annotations']+[standing])[i]
        frames.append({'rect':[x,y,313,313],'anchor':annotation.get('root_anchor',[161,275])})
        coverage.append({'frame':i,'feet':{name:sample_sole(im,annotation['feet'][name]) for name in FEET}})
    atlas.save(folder/'atlas.png',compress_level=9)
    # entry is deliberately only the periodic loop, for calibrator inspection.
    entry=doc['entry'];entry.update(source='atlas.png',columns=columns,rows=rows,frame_count=count,frames=frames[:count]);doc['auxiliary_annotations']=[standing]
    path.write_text(json.dumps(doc,indent=2)+'\n')
    full=copy.deepcopy(entry);full.update(frame_count=len(images),frames=frames,standing_frame=count)
    full['sequence']['duration_ms']=round(doc['motion_contract']['stride_logical_px']/doc['motion_contract']['speed_logical_px_per_second']*1000)
    (folder/'manifest.json').write_text(json.dumps({'schema':2,'clips':[full]},indent=2)+'\n')
    failed=[{'frame':i,'foot':foot,**coverage[i]['feet'][foot]} for i,ann in enumerate(doc['annotations']+[standing]) for foot in FEET if ann['feet'][foot]['planted'] and not coverage[i]['feet'][foot]['solid_support']]
    report={'schema':1,'loop_pose_count':count,'auxiliary_standing_pose_count':1,'canvas':[313,313],'anchor':[161,275],'reference_height':221,'atlas_size':list(atlas.size),'png_sha256':hashlib.sha256((folder/'atlas.png').read_bytes()).hexdigest(),'rendered_sole_alpha_check':{'threshold':128,'radius_source_px':3,'planted_failures':failed,'frames':coverage},'visual_approval':'FAILED / unapproved diagnostic handoff; numeric success is not visual approval','independently_painted_walk_frames':0,'rig_rendered_loop_poses':count}
    (folder/'bake-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'atlas':str(folder/'atlas.png'),'manifest':str(folder/'manifest.json'),'annotations':str(path),'loop_poses':count,'standing_poses':1,'planted_alpha_failures':len(failed)}))
    return bool(failed)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path);args=parser.parse_args();raise SystemExit(pack(args.folder))
