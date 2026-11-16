"""Register complete Dropshot seated/pounce/seated takes; Box2D owns the ball.

The generated magenta guide ball is removed by pigment, never painted over.
All source frames, source cadence and the fixed 1024/367 identity scale remain.
"""
from pathlib import Path
import hashlib,json,subprocess,sys
import cv2,numpy as np
from PIL import Image
import imageio_ffmpeg
ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'assets/video-actions';V=D/'selected/ball-v40';SIDE=288
sys.path.insert(0,str(ROOT/'tools'))
from pack_video_actions import foreground,eye_material,snow_material,EyeIdentityTracker

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def encode(frames,path):
    cmd=[imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{SIDE}x{SIDE*2}','-r','24','-i','-','-an','-c:v','libx264','-preset','slow','-crf','16','-pix_fmt','yuv420p','-g','24','-bf','0','-movflags','+faststart',str(path)]
    p=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    for im in frames:
        a=np.array(im.resize((SIDE,SIDE),Image.Resampling.LANCZOS));rgb=a[:,:,:3].copy();alpha=a[:,:,3]
        extended=cv2.dilate(rgb,np.ones((5,5),np.uint8));rgb[alpha==0]=extended[alpha==0]
        p.stdin.write(np.concatenate((rgb,np.repeat(alpha[:,:,None],3,axis=2)),axis=0).tobytes())
    p.stdin.close();assert p.wait()==0

def matte(rgb):
    im=foreground(rgb);a=np.array(im);r,g,b=rgb.transpose(2,0,1).astype(float)
    # Include the guide's dark red-purple rim as well as its bright body.
    # A blue/red ratio of .65 left a narrow crescent attached to a toe.
    magenta=(r>4)&(b>4)&(g<np.minimum(r,b)*.90)&(b>r*.38)
    n,labels,stats,_=cv2.connectedComponentsWithStats(magenta.astype('uint8'),8)
    # Select only the separate guide prop. Ear interiors and paw pads share
    # pink pigments and must never be included in a global colour cut.
    eligible=[i for i in range(1,n) if stats[i,cv2.CC_STAT_AREA]>50 and
        stats[i,cv2.CC_STAT_LEFT]+stats[i,cv2.CC_STAT_WIDTH]/2>310 and
        (stats[i,cv2.CC_STAT_TOP]+stats[i,cv2.CC_STAT_HEIGHT]/2>280 or stats[i,cv2.CC_STAT_LEFT]>350)]
    seeds=(labels==max(eligible,key=lambda i:stats[i,cv2.CC_STAT_AREA])).astype('uint8') if eligible else np.zeros(labels.shape,'uint8')
    guide=cv2.dilate(cv2.morphologyEx(seeds,cv2.MORPH_CLOSE,np.ones((5,5),np.uint8)),
        cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3)))>0
    a[guide]=0
    return Image.fromarray(a),guide

def restore_occluded_head(raw,frames,guides,coat):
    # The generated guide passes in front of the muzzle. Recover ONLY those
    # hidden pixels from a clean head in this very take, registered by tracked
    # facial features. No invented face, texture synthesis or body repaint.
    donor=48 if coat=='tuxedo' else 50
    ref=np.array(frames[donor]);mask=(ref[:,:,3]>200).astype('uint8')*255
    yy,xx=np.indices(mask.shape);mask[(yy>305)|(xx<190)|(xx>330)]=0
    gray=cv2.cvtColor(raw[donor],cv2.COLOR_RGB2GRAY)
    corners=cv2.goodFeaturesToTrack(gray,maxCorners=100,qualityLevel=.02,minDistance=4,mask=mask)
    records=[]
    for i in range(45,88):
        target=cv2.cvtColor(raw[i],cv2.COLOR_RGB2GRAY)
        tracked,ok,err=cv2.calcOpticalFlowPyrLK(gray,target,corners,None,winSize=(25,25),maxLevel=3)
        back,backok,_=cv2.calcOpticalFlowPyrLK(target,gray,tracked,None,winSize=(25,25),maxLevel=3)
        good=(ok[:,0]>0)&(backok[:,0]>0)&(np.linalg.norm(back[:,0]-corners[:,0],axis=1)<1.5)&(err[:,0]<30)
        if good.sum()<8:continue
        matrix,inliers=cv2.estimateAffinePartial2D(corners[good,0],tracked[good,0],method=cv2.RANSAC,ransacReprojThreshold=2)
        if matrix is None or inliers.sum()<8:continue
        # A head-only donor cannot create a new paw beneath the contact.
        donor_pixels=ref.copy();donor_pixels[310:]=0
        registered=cv2.warpAffine(donor_pixels,matrix,(480,480),flags=cv2.INTER_LINEAR)
        current=np.array(frames[i]);fill=guides[i]&(registered[:,:,3]>200)
        current[fill]=registered[fill];frames[i]=Image.fromarray(current)
        records.append(dict(frame=i,donor=donor,affine=matrix.tolist(),restored_pixels=int(fill.sum())))
    return records

# Manually reviewed near front toe, in 480px source coordinates. Each sample
# is constrained to visible fur, so a nose/guide-ball point cannot hit a ball.
KEYS={
 'cream':[(0,260,335),(40,274,335),(46,277,334),(48,276,334),(50,285,310),(52,306,318),(54,294,336),(58,291,336),(70,291,336),(74,293,333),(78,322,330),(82,302,338),(88,271,335),(96,264,337),(123,260,335)],
 'tuxedo':[(0,260,335),(40,273,335),(44,274,335),(46,282,310),(48,303,305),(50,309,319),(52,306,334),(54,301,336),(64,297,335),(70,315,326),(74,323,333),(78,314,334),(82,292,337),(88,268,336),(96,261,337),(123,260,335)]}
records=[];paws={};restoration={}
outdir=D/'runtime/ball-v40';outdir.mkdir(parents=True,exist_ok=True)
for c in ('cream','tuxedo'):
    source=D/'sources/ball-v40'/f'{c}-seated-pounce.mp4';cap=cv2.VideoCapture(str(source));fps=cap.get(cv2.CAP_PROP_FPS);raw=[]
    while True:
        ok,b=cap.read()
        if not ok:break
        raw.append(cv2.cvtColor(b,cv2.COLOR_BGR2RGB))
    cap.release();assert len(raw)==124 and fps==24 and raw[0].shape[:2]==(480,480)
    matted=[matte(x) for x in raw];frames=[x[0] for x in matted]
    restoration[c]=restore_occluded_head(raw,frames,[x[1] for x in matted],c)
    for i,im in enumerate(frames):
        a=np.array(im);n,labels,stats,_=cv2.connectedComponentsWithStats((a[:,:,3]>0).astype('uint8'),8)
        largest=1+int(np.argmax(stats[1:,cv2.CC_STAT_AREA]));a[labels!=largest]=0
        frames[i]=Image.fromarray(a)
    points=[];keys=np.array(KEYS[c]);factor=110/(367*480/1024)
    for i,im in enumerate(frames):
        tx,ty=(np.interp(i,keys[:,0],keys[:,k]) for k in (1,2));yy,xx=np.where(np.array(im)[:,:,3]>180)
        candidates=(yy>295)&(xx>230)&(xx<340)
        xx,yy=xx[candidates],yy[candidates];j=np.argmin((xx-tx)**2+(yy-ty)**2)
        points.append([(float(xx[j])-240)*factor,(340-float(yy[j]))*factor])
    paws[c]=points
    for coat,images in [(c,frames)]:
        path=outdir/f'{coat}-pounce.mp4';encode(images,path)
        records.append(dict(character=0 if coat=='cream' else 8,resource=2110 if coat=='cream' else 2111,path=path.relative_to(ROOT).as_posix(),sha256=sha(path),source=source.relative_to(ROOT).as_posix(),source_sha256=sha(source),source_frames=124,source_fps=24,selected_frames=list(range(124)),duration_ms=5167,width=SIDE,height=SIDE,anchor=[240*SIDE/480,340*SIDE/480],reference_height=367*SIDE/1024))
    if c=='cream':
        snow=[];tracker=EyeIdentityTracker();eyes=[]
        for i,im in enumerate(frames):
            rect=(0,0,480,480);eye,mask=eye_material(im,rect,'roll');eye,mask=tracker.apply(eye,mask,{'rect':rect,'anchor':[240,340]},i)
            painted,checks=snow_material(im,mask,strict_skin=True);assert checks['alpha_exact'] and checks['skin_rgb_exact']
            snow.append(painted);eyes.append(eye)
        path=outdir/'snowdrop-pounce.mp4';encode(snow,path);r=dict(records[-1]);r.update(character=10,resource=2112,path=path.relative_to(ROOT).as_posix(),sha256=sha(path),material='approved white coat and anatomical gold/blue iris')
        records.append(r);(V/'snowdrop-eyes.json').write_text(json.dumps(eyes,indent=2)+'\n',encoding='utf-8',newline='\n')
        for i in (0,48,52,74,96,123):snow[i].save(V/f'snowdrop-{i:03}.png')
    for i in (0,48,52,74,96,123):frames[i].save(V/f'{c}-{i:03}.png')
registration=dict(schema=1,canonical_reference_height=367,fixed_source_root=[240,340],source_size=[480,480],full_source_cadence=True,guide_ball='magenta pigment removed; independent visible Box2D ball, no scripted ball positions',occlusion_restoration=restoration,paw_keys=KEYS,paw_points_logical=paws,assets=records)
(V/'registration.json').write_text(json.dumps(registration,indent=2)+'\n',encoding='utf-8',newline='\n')
h=['// Generated by tools/pack_ball_pounce.py.','#pragma once','#include "cat_deformation.hpp"','#include <array>','#include <cstdint>','namespace cat::ball_movie {','struct Asset {unsigned character;int resource;unsigned width,height,frames;std::uint64_t durationMs;double anchorX,anchorY,referenceHeight;};','inline constexpr std::array<Asset,3> kAssets{{']
for r in records:h.append('    Asset{'+','.join(map(str,[r['character'],r['resource'],SIDE,SIDE,124,5167,*r['anchor'],r['reference_height']]))+'},')
h+=['}};','inline const Asset* Find(unsigned coat){for(const auto& a:kAssets)if(a.character==coat)return &a;return nullptr;}','inline unsigned Frame(std::uint64_t ms){return static_cast<unsigned>(std::min<std::uint64_t>(123,ms*24/1000));}']
for c,points in paws.items():
    h.append('inline constexpr std::array<cat::deform::Point,124> k'+c.title()+'{{')
    for p in points:h.append('    cat::deform::Point{'+','.join(f'{v:.5f}' for v in p)+'},')
    h.append('}};')
h+=['inline cat::deform::Point Paw(unsigned coat,unsigned frame){return (coat==8?kTuxedo:kCream)[std::min(123u,frame)];}','inline unsigned StandOffFrame(unsigned coat){return coat==8?50:52;}','inline unsigned ContactStart(unsigned coat){return coat==8?1875:2000;}','inline unsigned ContactEnd(unsigned coat){return coat==8?2250:2375;}','}']
(ROOT/'src/ball_pounce.generated.hpp').write_text('\n'.join(h)+'\n',encoding='utf-8',newline='\n')
(ROOT/'src/ball_pounce.generated.rc').write_text('// Generated complete ball pounce movies.\n'+'\n'.join(str(r['resource'])+' RCDATA "../'+r['path']+'"' for r in records)+'\n',encoding='utf-8',newline='\n')
print('Ball pounce movie bytes:',sum((ROOT/r['path']).stat().st_size for r in records))
