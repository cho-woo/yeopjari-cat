#!/usr/bin/env python3
"""Transfer authored Tuxedo appearance fields through the unchanged user video.

Image-gen key paintings author appearance only. Dense correspondence is computed
between original cream video frames; it transports Lab material offsets, never
cat geometry or alpha. Every output uses the exact original frame/matte canvas.
This is offline asset authoring, not runtime processing or frame interpolation.
"""
from __future__ import annotations
import argparse, hashlib, json, math, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build/flow-deps'))
import cv2
KEYS={50:'exec-2e6ffdb3-5a8a-4505-a022-e8039a4c757c.png',55:'exec-9477b84d-3607-4cfa-832b-1379e9ecddc8.png',60:'exec-5915bae7-080a-41fd-8b23-e2e05ac04223.png',65:'exec-16f41ff8-7701-4a50-a279-9ef0bc50f11e.png'}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def lab(rgb):return cv2.cvtColor(rgb.astype(np.float32)/255,cv2.COLOR_RGB2LAB)
def rgb(l):return np.clip(cv2.cvtColor(l.astype(np.float32),cv2.COLOR_LAB2RGB)*255,0,255).astype(np.uint8)
def nearest_fill(a,valid):
    if not valid.any():raise ValueError('Empty material-field support')
    inds=distance_transform_edt(~valid,return_distances=False,return_indices=True)
    return a[tuple(inds)]
def load_rgba(p):return np.asarray(Image.open(p).convert('RGBA')).copy()
def flow_gray(image):
    a=image[:,:,3].astype(np.float32)/255
    gray=cv2.cvtColor(image[:,:,:3],cv2.COLOR_RGB2GRAY).astype(np.float32)
    return np.clip(gray*a+128*(1-a),0,255).astype(np.uint8)
def get_rgba(base,index):return load_rgba(base/f'{index+1:04d}.png')
def align_key(source,target):
    h,w=source.shape[:2]
    target=np.asarray(Image.fromarray(target).resize((w,h),Image.Resampling.LANCZOS)).copy()
    s=cv2.GaussianBlur(source[:,:,3].astype(np.float32)/255,(9,9),2)
    t=cv2.GaussianBlur(target[:,:,3].astype(np.float32)/255,(9,9),2)
    warp=np.eye(2,3,dtype=np.float32)
    score,warp=cv2.findTransformECC(s,t,warp,cv2.MOTION_AFFINE,
        (cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_COUNT,200,1e-6),None,5)
    mapped=cv2.warpAffine(target,warp,(w,h),flags=cv2.INTER_LINEAR|cv2.WARP_INVERSE_MAP,borderMode=cv2.BORDER_CONSTANT)
    return mapped,{'alpha_ecc':float(score),'appearance_to_source_inverse_map':warp.tolist()}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--key-dir',type=Path,default=ROOT/'build/user-walk-video/tuxedo/authored-keys');ap.add_argument('--eye-trace',type=Path,default=ROOT/'build/user-walk-video/eye-trace.json');ap.add_argument('--matte-dir',type=Path,default=ROOT/'build/user-walk-video/matting/corrected');ap.add_argument('--start',type=int,default=50);ap.add_argument('--end',type=int,default=70);ap.add_argument('--output',type=Path,default=ROOT/'build/user-walk-video/tuxedo');ap.add_argument('--config',type=Path,help='Per-clip keys, temporal correspondence, protected props and review frames.');args=ap.parse_args()
    config=json.loads(args.config.read_text()) if args.config else {}
    cv2.setNumThreads(2)
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'rgba').mkdir(exist_ok=True);(args.output/'keys').mkdir(exist_ok=True)
    indices=config.get('indices',list(range(args.start,args.end)))
    source={i:get_rgba(args.matte_dir,i) for i in indices}
    keydefs=config.get('keys',[{'source_frame':i,'time':i,'image':f'{i+1:04d}.png'} for i in KEYS])
    period=config.get('period',20);temporal_sigma=config.get('temporal_sigma',4.5)
    prepared=[];proofkeys=[]
    for key in keydefs:
        i=key['source_frame'];key_time=key.get('time',i)
        s=get_rgba(args.matte_dir,i);path=args.key_dir/key.get('image',f'{i+1:04d}.png')
        target,registration=align_key(s,load_rgba(path))
        valid=(s[:,:,3]>230)&(target[:,:,3]>200)
        if config.get('exclude_iris_from_material',False):
            painted_hsv=cv2.cvtColor(target[:,:,:3],cv2.COLOR_RGB2HSV)
            # Irises are restored from the exact original eye geometry below.
            # Do not transport a key painting's iris into fur during a turn.
            painted_iris=(painted_hsv[:,:,0]>=20)&(painted_hsv[:,:,0]<=50)&(painted_hsv[:,:,1]>50)&(painted_hsv[:,:,2]>45)
            valid&=~painted_iris
        field=lab(target[:,:,:3])-lab(s[:,:,:3]);field=nearest_fill(field,valid)
        field=cv2.GaussianBlur(field,(5,5),.9)
        # No geometry from this generated painting is used below.
        Image.fromarray(target).save(args.output/'keys'/f'aligned-{i:03d}.png')
        prepared.append((key_time,s,field,flow_gray(s)))
        proofkeys.append({'source_frame_index':i,'appearance_time':key_time,'authored_image':str(path),'authored_sha256':sha(path),**registration})
    dis=cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM);dis.setFinestScale(0);dis.setPatchSize(8);dis.setPatchStride(3)
    h,w=next(iter(source.values())).shape[:2];xx,yy=np.meshgrid(np.arange(w,dtype=np.float32),np.arange(h,dtype=np.float32))
    eyes={r['frame']:(r['eye_x'],r['eye_y']) for r in json.loads(args.eye_trace.read_text())} if args.eye_trace.exists() and not config.get('ignore_eye_trace',False) else {}
    reports=[]
    for i,s in source.items():
        gray=flow_gray(s);slab=lab(s[:,:,:3]);acc=np.zeros_like(slab);total=np.zeros((h,w),np.float32)
        for k,ks,field,kgray in prepared:
            # Timing chooses the closest appearance reference. Nonperiodic
            # actions use linear time; aliases can reuse a material reference
            # at another time without repeating or altering a motion frame.
            phase_distance=abs((i-k+period/2)%period-period/2) if period else abs(i-k)
            temporal=math.exp(-(phase_distance/temporal_sigma)**2/2)
            flow=dis.calc(gray,kgray,None)
            mapx=xx+flow[:,:,0];mapy=yy+flow[:,:,1]
            delta=cv2.remap(field,mapx,mapy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE)
            k_rgb=cv2.remap(ks[:,:,:3],mapx,mapy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE)
            k_alpha=cv2.remap(ks[:,:,3],mapx,mapy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT).astype(np.float32)/255
            error=np.mean(abs(k_rgb.astype(np.float32)-s[:,:,:3].astype(np.float32)),axis=2)
            confidence=np.exp(-error/32)*k_alpha
            weight=temporal*np.maximum(.04,confidence)
            acc+=delta*weight[:,:,None];total+=weight
        result=rgb(slab+acc/np.maximum(total[:,:,None],1e-6))
        # Retain original true pink skin and its detail. This is photometric
        # skin detection, not a geometric fur pattern or a posed limb mask.
        sr=s[:,:,:3].astype(np.float32);r,g,b=sr[:,:,0],sr[:,:,1],sr[:,:,2]
        pink=np.clip(((r-g)-1.6*(g-b)-18)/25,0,1)*np.clip((r-110)/60,0,1)
        # Keep blush modest if the transferred material is black.
        destlight=np.max(result,axis=2);pink*=np.clip((destlight-60)/110,0,1)
        result=np.clip(result*(1-pink[:,:,None])+sr*pink[:,:,None],0,255).astype(np.uint8)
        # Preserve exact source eye geometry. The author's desired olive/gold
        # iris is a hue change on the original brown iris, not a new eye.
        if i in eyes:
            ex,ey=eyes[i];roi=((xx-ex)/38)**2+((yy-ey)/57)**2<1
            hsv=cv2.cvtColor(s[:,:,:3],cv2.COLOR_RGB2HSV)
            iris=roi&(hsv[:,:,0]>=4)&(hsv[:,:,0]<=29)&(hsv[:,:,1]>100)&(hsv[:,:,2]>40)&(hsv[:,:,2]<225)
            eyehsv=hsv.copy();eyehsv[:,:,0][iris]=29;eyehsv[:,:,1][iris]=np.minimum(150,eyehsv[:,:,1][iris]);eyehsv[:,:,2][iris]=(eyehsv[:,:,2][iris]*.92).astype(np.uint8);eye_rgb=cv2.cvtColor(eyehsv,cv2.COLOR_HSV2RGB)
            result[iris]=eye_rgb[iris]
        elif config.get('eye_rois'):
            # Recolor only the original brown iris pixels, including both eyes
            # in front views. Closed eyelids are too thin to be iris components.
            hsv=cv2.cvtColor(s[:,:,:3],cv2.COLOR_RGB2HSV);roi=np.zeros((h,w),bool)
            for x0,y0,x1,y1 in config['eye_rois']:roi[y0:y1,x0:x1]=True
            candidates=(roi&(s[:,:,3]>180)&(hsv[:,:,0]>=4)&(hsv[:,:,0]<=29)&(hsv[:,:,1]>120)&(hsv[:,:,2]>40)&(hsv[:,:,2]<225)).astype(np.uint8)
            count,labels,stats,_=cv2.connectedComponentsWithStats(candidates)
            iris=np.zeros((h,w),bool)
            for c in range(1,count):
                x,y,cw,ch,area=stats[c]
                component=labels==c
                # Ginger fur can share the iris hue, but has no deep brown
                # iris core. This prevents olive spots on crown/ear markings.
                core=(s[:,:,0]<150)&(s[:,:,1]<110)&(s[:,:,2]<75)
                if 30<=area<=6000 and ch>=7 and .12<=cw/ch<=2.8 and np.count_nonzero(component&core)>=12:iris|=component
            eyehsv=hsv.copy();eyehsv[:,:,0][iris]=29;eyehsv[:,:,1][iris]=np.minimum(150,eyehsv[:,:,1][iris]);eyehsv[:,:,2][iris]=(eyehsv[:,:,2][iris]*.92).astype(np.uint8)
            eye_rgb=cv2.cvtColor(eyehsv,cv2.COLOR_HSV2RGB);result[iris]=eye_rgb[iris]
        protected=np.zeros((h,w),bool)
        if config.get('prop_mask_dir'):
            # Masks may be grayscale L or RGBA; the supplied matte prop mask is L.
            protected|=np.asarray(Image.open(Path(config['prop_mask_dir'])/f'{i+1:04d}.png').convert('L'))>0
        for x0,y0,x1,y1 in config.get('preserve_rectangles',[]):protected[y0:y1,x0:x1]=True
        result[protected]=s[:,:,:3][protected]
        rgba=np.dstack([result,s[:,:,3]])
        rgba[rgba[:,:,3]==0,:3]=0
        out=args.output/'rgba'/f'{i+1:04d}.png';Image.fromarray(rgba).save(out)
        assert np.array_equal(rgba[:,:,3],s[:,:,3])
        assert np.array_equal(rgba[:,:,:3][protected&(s[:,:,3]>0)],s[:,:,:3][protected&(s[:,:,3]>0)])
        reports.append({'source_frame_index':i,'source_alpha_sha256':hashlib.sha256(s[:,:,3].tobytes()).hexdigest(),'output_alpha_sha256':hashlib.sha256(rgba[:,:,3].tobytes()).hexdigest(),'protected_prop_rgb_pixel_count':int((protected&(s[:,:,3]>0)).sum()),'output_sha256':sha(out)})
        print(i,flush=True)
    (args.output/'transfer-proof.json').write_text(json.dumps({'schema':1,'method':'authored_material_fields_over_original_video_geometry','geometry_source':'unchanged input video; optical flow transports appearance fields only','alpha_source':str(args.matte_dir),'pose_interpolation':False,'runtime_image_processing':False,'temporal_period':period,'temporal_sigma':temporal_sigma,'config':config,'selected_source_indices':list(source),'key_paintings':proofkeys,'frames':reports},indent=2)+'\n')
    # Actual110px review. One fixed source reference, no per-frame fitting.
    board=Image.new('RGB',(1000,480),'#d8dfe4');draw=ImageDraw.Draw(board)
    for col,i in enumerate(config.get('preview_keys',[50,55,60,65])):
        for row,im in enumerate([source[i],load_rgba(args.output/'rgba'/f'{i+1:04d}.png')]):
            ref=config.get('reference_height',417)
            img=Image.fromarray(im).resize((round(w*110/ref),round(h*110/ref)),Image.Resampling.LANCZOS)
            board.paste(img,(col*250+20,row*230+50),img)
            draw.text((col*250+15,row*230+10),f'{"Original" if row==0 else "Tuxedo"} / source {i}',fill='#26313a')
    board.save(args.output/'contact-110.png')

if __name__=='__main__':main()
