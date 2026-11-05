#!/usr/bin/env python3
"""Review the actual 2D joint walk against the v2.0.7 four-picture walk.

Production C++ Rig/Cycle rasterize source fur and feet. Pillow performs final
placement; this portable review is not Windows GDI+ or live window execution.
"""
from pathlib import Path
import argparse,os,subprocess,sys,ctypes,math,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build/articulated-walk')
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
os.chdir(ROOT)
sys.path.insert(0,str(ROOT/'tools'))
from render_motion_preview import atlas
from render_walk_review import save_gif
from PIL import Image,ImageDraw,ImageChops
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'src'),str(ROOT/'tools/articulated_walk_probe.cpp'),'-o',str(out/'probe.so')],check=True)
lib=ctypes.CDLL(str((out/'probe.so').resolve()));p=ctypes.c_void_p;d=ctypes.c_double;i=ctypes.c_int
for n,args,res in [('create',[p,i,i],p),('destroy',[p],None),('step',[p,d,d,d,d],i),('pixels',[p],p),('width',[p],i),('height',[p],i),('reach_error',[p],d),('joint',[p,i,ctypes.POINTER(d)],None)]:
 f=getattr(lib,'articulated_'+n);f.argtypes=args;f.restype=res
colors=['#d34444','#db9d24','#1b9a67','#507bcc'];samples=[];errors=[];characters=[];ground_errors=[];raster_count=0
for char,file in [('Cream','cat.png'),('Silver','cat-silver.png')]:
 frames,ref=atlas(file);im=frames[1].convert('RGBa');raw=im.tobytes('raw','BGRa');mem=ctypes.create_string_buffer(raw,len(raw));scale=110/max(a.height for a in frames[:6]);r=lib.articulated_create(mem,im.width,im.height);characters.append((char,frames,im,mem,scale,r))

def audit(im,r,scale,body):
 global raster_count
 w,h=lib.articulated_width(r),lib.articulated_height(r)
 raw=ctypes.string_at(lib.articulated_pixels(r),w*h*4)
 image=Image.frombytes('RGBA',(w,h),raw,'raw','BGRA');alpha=image.getchannel('A')
 for channel in ('R','G','B'):
  if ImageChops.subtract(image.getchannel(channel),alpha).getbbox():raise RuntimeError('Invalid premultiplied alpha')
 source=im.tobytes('raw','BGRa');padding=(w-im.width)//2
 for y in range(int(h*.68)):
  if raw[(y*w+padding)*4:(y*w+padding+im.width)*4]!=source[y*im.width*4:(y+1)*im.width*4]:raise RuntimeError('Head/back source pixels changed')
 bounds=alpha.point(lambda x:255 if x>153 else 0).getbbox()
 if not bounds or bounds[0]<=0 or bounds[2]>=w:raise RuntimeError('Output clipped')
 error=(bounds[3]-(h-2))*scale*body[1];ground_errors.append(error)
 if error < -1.2 or error > 1.2:raise RuntimeError(f'Painted paw lost support: {error}')
 if lib.articulated_reach_error(r)>1e-8:raise RuntimeError('Unreachable actual gait foot')
 raster_count+=1


def render(char,frames,im,r,scale,time,body=(1,1),zoom=1,diagnostic=False,old=False):
 ground=160*zoom;board=Image.new('RGBA',(260*zoom,200*zoom),'#edf0f4');draw=ImageDraw.Draw(board)
 for world in range(-100,400,10):
  x=(world-time*22%10)*zoom;draw.line((x,ground,x,ground+4*zoom),fill='#9ba8b7')
 draw.line((0,ground,260*zoom,ground),fill='#64758a')
 if old:
  frame=int((time*22/14)%1*4);img=frames[frame].convert('RGBA');w,h=round(img.width*scale*body[0]*zoom),round(img.height*scale*body[1]*zoom);img=img.resize((w,h),Image.Resampling.BICUBIC);board.alpha_composite(img,(130*zoom-w//2,ground-h))
 else:
  w,h=lib.articulated_width(r),lib.articulated_height(r);raw=ctypes.string_at(lib.articulated_pixels(r),w*h*4);img=Image.frombytes('RGBa',(w,h),raw,'raw','BGRa').convert('RGBA');sw,sh=round(w*scale*body[0]*zoom),round(h*scale*body[1]*zoom);img=img.resize((sw,sh),Image.Resampling.BICUBIC);origin=(130*zoom-sw/2,ground-sh+2*scale*body[1]*zoom);board.alpha_composite(img,(round(origin[0]),round(origin[1])))
  if diagnostic:
   pad=(w-im.width)/2
   for leg,color in enumerate(colors):
    j=(d*12)();lib.articulated_joint(r,leg,j)
    coords=[(origin[0]+(j[a]+pad)*scale*body[0]*zoom,origin[1]+j[a+1]*scale*body[1]*zoom)for a in range(0,10,2)]
    draw.line(coords[:4],fill=color,width=1)
    x,y=coords[4];draw.ellipse((x-3,y-3,x+3,y+3),outline=color,width=1)
 draw.text((4*zoom,5*zoom),char+(' v2.0.7 4 frames' if old else ' IK18 .72 2.6')+f' t={time:.2f}',fill='#203344')
 return board.convert('RGB')

normal=[];diagnostics=[];still=[]
for t in range(100):
 time=t*.04;board=Image.new('RGB',(520,400));detail=Image.new('RGB',(1040,800))
 for row,(char,frames,im,mem,scale,r) in enumerate(characters):
  if not lib.articulated_step(r,.88 if t else 0,.04,im.width*scale,im.height*scale):raise RuntimeError('step')
  errors.append(lib.articulated_reach_error(r));audit(im,r,scale,(1,1));board.paste(render(char,frames,im,r,scale,time,old=True),(0,row*200));board.paste(render(char,frames,im,r,scale,time),(260,row*200));detail.paste(render(char,frames,im,r,scale,time,zoom=2,old=True),(0,row*400));detail.paste(render(char,frames,im,r,scale,time,zoom=2,diagnostic=True),(520,row*400))
 normal.append(board);diagnostics.append(detail)
 if t in(0,4,8,12,16,20,24,28):still.append(detail)
save_gif(normal,out/'candidate.gif',40);save_gif(diagnostics,out/'diagnostics.gif',40)
sheet=Image.new('RGB',(4160,1600),'#edf0f4')
for n,img in enumerate(still):sheet.paste(img,((n%4)*1040,(n//4)*800))
sheet.save(out/'candidate.png')
# Conservative nutrition shapes and the same true planted target controller.
shapes=[]
for char,frames,im,mem,scale,unused in characters:
 for body in ((.78,.96),(1,1),(1.26,.98)):
  r=lib.articulated_create(mem,im.width,im.height)
  for t in range(96):
   lib.articulated_step(r,.88,.04,im.width*scale*body[0],im.height*scale*body[1]);err=lib.articulated_reach_error(r);errors.append(err);audit(im,r,scale,body)
   if t in (0,6,12,18):shapes.append(render(char,frames,im,r,scale,t*.04,body,zoom=2,diagnostic=True))
  lib.articulated_destroy(r)
shape=Image.new('RGB',(2080,2400),'#edf0f4')
for n,img in enumerate(shapes):shape.paste(img,((n%4)*520,(n//4)*400))
shape.save(out/'body-extremes.png')
for _,_,_,_,_,r in characters:lib.articulated_destroy(r)
proof={
 'scope':'Cream/Silver ordinary grounded walking only; face/fur source retained',
 'comparison':'Left v2.0.7 original four whole drawings at14px distance/cycle; right production two-bone Rig and true18px contact Cycle',
 'renderer':'Production C++ raster and Cycle; Pillow placement, not Windows GDI+ or live Box2D/windows',
 'max_reach_error_bodypx':max(errors),'painted_ground_error_range_bodypx':[min(ground_errors),max(ground_errors)],
 'source':'Source frame1 for original near-limb painted texture; far limbs use perspective-scaled same fur',
 'actual_parameters':[18,.72,2.6],'foot_coordinates_scaled':False,'raster_samples':raster_count,
 'body_shapes':[[.78,.96],[1,1],[1.26,.98]],'speed_px_s':22,'frame_ms':40,
 'all_upper68percent_source_pixels_unchanged':True,'premultiplied_alpha_valid':True,'unreachable_feet':0,
 'source_hashes':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ['assets/cat.png','assets/cat-silver.png','src/cat_articulated_walk.hpp','src/cat_gait.hpp','tools/articulated_walk_probe.cpp','tools/render_joint_walk_review.py']},
 'gif_sha256':hashlib.sha256((out/'candidate.gif').read_bytes()).hexdigest(),
 'limits':'The original artwork is stylized and partly occluded. Torso occlusion and rigid skinning still require image review; numerical contacts do not prove biological realism.'}
(out/'review.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({'output':str(out),'max_reach_error_bodypx':max(errors),'painted_ground_error_range_bodypx':proof['painted_ground_error_range_bodypx'],'raster_samples':raster_count}))
