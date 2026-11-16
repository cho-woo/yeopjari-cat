"""Bake retained Dropshot furniture scenes into small RGB/alpha packed H264.
No source frame is removed or retimed. The1024/367 canonical size convention is
shared with the other authored movies; scene roots are fixed, never alpha-fit.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
import cv2,numpy as np
from PIL import Image
import imageio_ffmpeg
ROOT=Path(__file__).resolve().parents[1];D=ROOT/'assets/furniture-v39';SIDE=288
sys.path.insert(0,str(ROOT/'tools'));from pack_video_actions import eye_material,snow_material
parser=argparse.ArgumentParser();parser.add_argument('--snow-eyes-only',action='store_true');args=parser.parse_args()

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def matte(rgb):
 r,g,b=rgb.transpose(2,0,1).astype(float)
 opaque=~(g>np.maximum(r,b)*1.22+12)
 count,labels,stats,_=cv2.connectedComponentsWithStats(opaque.astype('uint8'),8)
 keep=[i for i in range(1,count) if stats[i,cv2.CC_STAT_AREA]>=8]
 mask=np.isin(labels,keep).astype(np.float32)
 alpha=cv2.GaussianBlur(mask,(0,0),.36);alpha[cv2.distanceTransform(mask.astype('uint8'),cv2.DIST_L2,5)>1.2]=1
 alpha=np.rint(alpha*255).clip(0,255).astype('uint8');alpha[alpha<12]=0
 color=rgb.copy();edge=(alpha>0)&(alpha<245);color[:,:,1][edge]=np.minimum(color[:,:,1],np.maximum(color[:,:,0],color[:,:,2]))[edge];color[alpha==0]=0
 return Image.fromarray(np.dstack((color,alpha)))
def encode(frames,path,fps,*,preserve_iris_chroma=False):
 args=[imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{SIDE}x{SIDE*2}','-r',str(fps),'-i','-','-an','-c:v','libx264','-preset','slow','-crf','22','-pix_fmt','yuv420p','-g','24','-bf','0','-movflags','+faststart',str(path)]
 # Keep the small blue iris from becoming neutral grey under 4:2:0
 # quantization. Preserve the MF-compatible format, dimensions and clock.
 if preserve_iris_chroma:args[-1:-1]=['-x264-params','chroma-qp-offset=-6']
 proc=subprocess.Popen(args,stdin=subprocess.PIPE)
 for im in frames:
  rgba=np.array(im.resize((SIDE,SIDE),Image.Resampling.LANCZOS));rgb=rgba[:,:,:3].copy();alpha=rgba[:,:,3]
  extended=cv2.dilate(rgb,np.ones((5,5),np.uint8));rgb[alpha==0]=extended[alpha==0]
  packed=np.concatenate((rgb,np.repeat(alpha[:,:,None],3,axis=2)),axis=0)
  proc.stdin.write(packed.tobytes())
 proc.stdin.close();assert proc.wait()==0

jobs=json.loads((D/'selected/video-generations.json').read_text())
records=json.loads((D/'manifest.json').read_text())['assets'] if args.snow_eyes_only else []
review=D/'selected/review';review.mkdir(exist_ok=True)
for job in jobs:
 kind,coat=job['kind'],job['coat']
 if args.snow_eyes_only and coat!='cream':continue
 p=D/'sources'/f'{coat}-{kind}.mp4';video=cv2.VideoCapture(str(p));fps=video.get(cv2.CAP_PROP_FPS);source=[]
 while True:
  ok,bgr=video.read()
  if not ok:break
  source.append(cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB))
 video.release();height,width=source[0].shape[:2]
 frames=[matte(x) for x in source];assert len(frames)==job['source']['frames']
 output=D/'runtime'/f'{coat}-{kind}.mp4'
 if not args.snow_eyes_only:encode(frames,output,fps)
 kindid={'tower':1,'scratch':2,'box':3}[kind]
 record={'kind':kind,'kind_id':kindid,'character':0 if coat=='cream' else 8,'resource':2100+(0 if coat=='cream' else 3)+kindid,'path':output.relative_to(ROOT).as_posix(),'sha256':sha(output),'source':p.relative_to(ROOT).as_posix(),'source_sha256':sha(p),'source_dimensions':[width,height],'source_fps':fps,'source_frames':len(frames),'selected_frames':list(range(len(frames))),'duration_ms':round(len(frames)*1000/fps),'width':SIDE,'height':SIDE,'packed_alpha':'RGB top / grey alpha bottom; no audio','anchor':[300*SIDE/1024,880*SIDE/1024],'reference_height':367*SIDE/1024,'fixed_scene_floor':880,'fixed_scene_origin_x':300}
 if not args.snow_eyes_only:records.append(record)
 else:
  existing=next(r for r in records if r['character']==0 and r['kind_id']==kindid)
  assert existing==record,'Eye-only correction cannot change Cream source, packing or registration'
 if coat=='cream':
  snow=[];eye_records=[]
  for i,im in enumerate(frames):
   # These upright prop scenes have no inverted head roll. Transporting eye
   # IDs by nearest position confused a moving/climbing head with a roll and
   # swapped the identical first/last face. Use source geometry instead:
   # paired eyes retain gold screen-left / blue screen-right; the detector
   # labels a single profile eye from the actual nose direction.
   rect=(0,0,width,height);eyes,mask=eye_material(im,rect,'roll')
   rgb=np.asarray(im)[:,:,:3].astype(float);first=source[0].astype(float)
   # Furniture is cool-coloured and static; retain its exact RGB. Only the
   # cat's warm coat and annotated iris pigments receive Snowdrop material.
   warm=(rgb[:,:,0]-rgb[:,:,1]>4)&(rgb[:,:,0]-rgb[:,:,2]>8)
   inprop=np.indices((height,width))[1]>=475*width/1024
   sameprop=np.max(np.abs(rgb-first),axis=2)<34
   props=(~warm)|(inprop&sameprop);props[mask>0]=False
   painted,checks=snow_material(im,mask,props,strict_skin=True);assert checks['alpha_exact'] and checks['skin_rgb_exact']
   # The retained source has only a thin, dark iris rim. At288px, 4:2:0
   # averages that blue into the black pupil unless its material value is
   # preserved before encoding. Change only the annotated blue iris pixels;
   # pupil, highlights, alpha, fur, skin and furniture stay untouched.
   selected=mask==2
   rgba=np.array(painted);hsv=np.array(painted.convert('RGB').convert('HSV'))
   hsv[:,:,1][selected]=np.maximum(hsv[:,:,1][selected],170)
   hsv[:,:,2][selected]=np.maximum(hsv[:,:,2][selected],140)
   iris=np.array(Image.fromarray(hsv,'HSV').convert('RGB'))
   rgba[:,:,:3][selected]=iris[selected];painted=Image.fromarray(rgba)
   snow.append(painted);eye_records.append(eyes)
  snowpath=D/'runtime'/f'snowdrop-{kind}.mp4';encode(snow,snowpath,fps,preserve_iris_chroma=True)
  r=dict(record);r.update(character=10,resource=2106+kindid,path=snowpath.relative_to(ROOT).as_posix(),sha256=sha(snowpath),material='approved Snowdrop white fur + upright source gold/blue iris identity; furniture RGB protected',chroma_qp_offset=-6,blue_iris_min_saturation=170,blue_iris_min_value=140)
  if args.snow_eyes_only:records=[old for old in records if not(old['character']==10 and old['kind_id']==kindid)]
  records.append(r)
  (review/f'snowdrop-{kind}-eyes.json').write_text(json.dumps(eye_records,indent=2)+'\n',encoding='utf-8',newline='\n')
  for i in (0,len(frames)//3,len(frames)//2,len(frames)*2//3,len(frames)-1):snow[i].save(review/f'snowdrop-{kind}-{i:03}.png')
 if not args.snow_eyes_only:
  for i in (0,len(frames)//3,len(frames)//2,len(frames)*2//3,len(frames)-1):frames[i].save(review/f'{coat}-{kind}-{i:03}.png')
 print(kind,coat,len(frames),'frames',fps,'FPS',output.stat().st_size,'bytes',flush=True)
records.sort(key=lambda r:(r['character'],r['kind_id']))
manifest={'schema':1,'method':'Dropshot complete source / fixed canonical scene / packed video alpha','canonical_reference_height':367,'assets':records}
(D/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
header=['// Generated by tools/pack_furniture_movies.py.','#pragma once','#include <array>','#include <cstdint>','namespace cat::furniture {','enum class Kind : unsigned { None,Tower,Scratcher,Box };','struct Asset {Kind kind;unsigned character;int resource;unsigned width,height,frames;std::uint64_t durationMs;double anchorX,anchorY,referenceHeight;};','inline constexpr std::array<Asset,9> kAssets{{']
rc=['// Generated furniture RGB/alpha movies.']
for r in records:
 header.append('    Asset{Kind::'+{'tower':'Tower','scratch':'Scratcher','box':'Box'}[r['kind']]+','+','.join(map(str,[r['character'],r['resource'],SIDE,SIDE,r['source_frames'],r['duration_ms'],*r['anchor'],r['reference_height']]))+'},')
 rc.append(str(r['resource'])+' RCDATA "../'+r['path']+'"')
header+=['}};','inline const Asset* Find(Kind kind,unsigned character) {for(const auto& a:kAssets)if(a.kind==kind&&a.character==character)return &a;return nullptr;}','}']
(ROOT/'src/furniture_assets.generated.hpp').write_text('\n'.join(header)+'\n',encoding='utf-8',newline='\n');(ROOT/'src/furniture_assets.generated.rc').write_text('\n'.join(rc)+'\n',encoding='utf-8',newline='\n')
print('Packed furniture bytes:',sum((ROOT/r['path']).stat().st_size for r in records))
