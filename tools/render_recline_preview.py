#!/usr/bin/env python3
"""Read-only actual-art preview of production native-head registration/eyelids.

C++ owns landmarks, eye patches, masking and registered raster. Pillow only
places opaque endpoint art and composites output on a developer contact sheet.
This portable preview is not Win32/GDI+ execution or a desktop physics test.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw
from render_motion_preview import atlas
from render_expression_preview import companion_crops

ROOT=Path(__file__).resolve().parents[1]
NAMES=['Cream','Silver','Black','Calico','Siamese','Munchkin','Dog','Rabbit','Tuxedo','British']

class Preview:
 def __init__(self, output):
  self.output=output;output.mkdir(parents=True,exist_ok=True)
  path=output/'recline_preview.so'
  subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'tools/recline_preview.cpp'),'-o',str(path)],check=True)
  self.lib=ctypes.CDLL(str(path));p=ctypes.c_void_p;i=ctypes.c_int;d=ctypes.POINTER(ctypes.c_double)
  self.lib.recline_preview.argtypes=[p,i,i,p,i,i,i,i,d];self.lib.recline_preview.restype=i
  self.lib.recline_preview_close.argtypes=[p,i,i,p,i,i,p,i,i,ctypes.c_double];self.lib.recline_preview_close.restype=i
  self.lib.recline_preview_landmarks.argtypes=[i,i,d]
  self.lib.recline_preview_blink.argtypes=[i,i,ctypes.c_double];self.lib.recline_preview_blink.restype=ctypes.c_double
  self.lib.recline_preview_pose.argtypes=[ctypes.c_uint,d]
  self.lib.recline_preview_skin.argtypes=[p,i,i,p,i,ctypes.c_uint,i];self.lib.recline_preview_skin.restype=i
  self.lib.recline_preview_benchmark.argtypes=[p,i,i,p,i,i,i,d,p,i,i,i];self.lib.recline_preview_benchmark.restype=ctypes.c_double
  old=[atlas('cat.png')[0],atlas('cat-silver.png')[0]];lying=atlas('cat-recline.png',2,4)[0]
  sit=companion_crops(0);side=companion_crops(7);back=atlas('companions-back.png',4,8)[0];lids=companion_crops(5)
  self.art=[];self.lids=[]
  for c in range(10):
   scale=110/max(im.height for im in old[c][:6]) if c<2 else 110/sit[c-2].height
   self.art.append({4:(old[c][4],scale) if c<2 else(sit[c-2],scale),24:(lying[c*2],.25) if c<2 else(side[c-2],scale),25:(lying[c*2+1],.25) if c<2 else(back[c-2],.44)})
   self.lids.append(old[c][5] if c<2 else lids[c-2])
 def landmarks(self,c,f):
  values=(ctypes.c_double*6)();self.lib.recline_preview_landmarks(c,f,values);return list(values)
 def source(self,c,f,sleep=False,elapsed=None,resting=False):
  im,scale=self.art[c][f]
  if elapsed is not None:
   raw=ctypes.create_string_buffer(im.convert('RGBa').tobytes('raw','BGRa'));dest=ctypes.create_string_buffer(im.width*im.height*4)
   if self.lib.recline_preview_skin(raw,im.width,im.height,dest,f,elapsed,resting)!=1:raise RuntimeError('production local joint warp failed')
   im=Image.frombytes('RGBa',im.size,dest.raw,'raw','BGRa').convert('RGBA')
  if not sleep:return im,scale
  raw=ctypes.create_string_buffer(im.convert('RGBa').tobytes('raw','BGRa'));dest=ctypes.create_string_buffer(len(raw)-1)
  lid=self.lids[c];closed=ctypes.create_string_buffer(lid.convert('RGBa').tobytes('raw','BGRa'))
  if not self.lib.recline_preview_close(raw,im.width,im.height,dest,c,f,closed,lid.width,lid.height,float(sleep)):raise RuntimeError('production eyelid failed')
  return Image.frombytes('RGBa',im.size,dest.raw,'raw','BGRa').convert('RGBA'),scale
 def image(self,c,a,b,t,zoom=2,sleep=False,elapsed=None):
  canvas=(240*zoom,160*zoom);origin=(120*zoom,140*zoom)
  def endpoint(frame):
   source,scale=self.source(c,frame,sleep,elapsed,sleep);scale*=zoom
   im=source.convert('RGBa').transform(canvas,Image.Transform.AFFINE,(1/scale,0,source.width/2-origin[0]/scale,0,1/scale,source.height-origin[1]/scale),Image.Resampling.BICUBIC)
   return im
  if b<0 or t<=0:result=endpoint(a)
  elif t>=1:result=endpoint(b)
  else:
   first,fs=self.art[c][a];second,ss=self.art[c][b]
   layers=[]
   for frame in (a,b):
    source,scale=self.source(c,frame,max(float(sleep),self.lib.recline_preview_blink(a,b,t)),elapsed,sleep)
    raw=ctypes.create_string_buffer(source.convert('RGBa').tobytes('raw','BGRa'));dest=ctypes.create_string_buffer(canvas[0]*canvas[1]*4)
    geometry=(ctypes.c_double*12)(source.width*scale*zoom,source.height*scale*zoom,a,first.width*fs*zoom,first.height*fs*zoom,b,second.width*ss*zoom,second.height*ss*zoom,t,*origin,0)
    result=self.lib.recline_preview(raw,source.width,source.height,dest,*canvas,c,frame,geometry)
    if result!=1:raise RuntimeError(f'production registration failed: character={c} source={frame} {a}>{b} code={result}')
    layers.append(Image.frombytes('RGBa',canvas,dest.raw,'raw','BGRa'))
   # Same integer premultiplied weights as DrawStyledAnimation.
   weight=int(t*255+.5)
   mixed=bytes((x*(255-weight)+y*weight+127)//255 for x,y in zip(layers[0].tobytes(),layers[1].tobytes()))
   result=Image.frombytes('RGBa',canvas,mixed)
  display=Image.new('RGBA',canvas,'#edf0f4');display.alpha_composite(result.convert('RGBA'));ImageDraw.Draw(display).line((0,origin[1],canvas[0],origin[1]),fill='#718396',width=zoom)
  return display
 def contact(self,zoom=2):
  cases=[(4,24,.5,False),(24,25,.5,False),(25,-1,0,False),(24,-1,0,True),(25,-1,0,True)]
  sheet=Image.new('RGBA',(240*zoom*5,180*zoom*10),'#edf0f4');pen=ImageDraw.Draw(sheet)
  for c,name in enumerate(NAMES):
   for col,(a,b,t,sleep) in enumerate(cases):
    im=self.image(c,a,b,t,zoom,sleep,1600 if sleep else 260 if a==4 else 820 if b==25 else 1900);x=col*240*zoom;y=c*180*zoom
    sheet.alpha_composite(im,(x,y+16*zoom));pen.text((x+6,y+6),f'{name} {a}>{b} t={t} sleep={sleep}',fill='#172633')
  sheet.convert('RGB').save(self.output/'all-species.png')
 def benchmarks(self):
  result={}
  for c in range(10):
   a,sa=self.art[c][24];b,sb=self.art[c][25];lid=self.lids[c]
   inputs=[ctypes.create_string_buffer(im.convert('RGBa').tobytes('raw','BGRa')) for im in (a,b,lid)]
   geometry=(ctypes.c_double*4)(a.width*sa,a.height*sa,b.width*sb,b.height*sb)
   value=self.lib.recline_preview_benchmark(inputs[0],a.width,a.height,inputs[1],b.width,b.height,c,geometry,inputs[2],lid.width,lid.height,40)
   if value<0:raise RuntimeError('production benchmark raster failed')
   result[NAMES[c]]=round(value,3)
  (self.output/'timing-ms.json').write_text(json.dumps({'platform':'Linux g++ -O2; 420x320; two native-source rasters, mesh, eyelids and premultiplied mix; excludes GDI+ and local joint preparation','ms_per_transition_frame':result},indent=2)+'\n')
 def animations(self):
  for c in (0,2,6,7):
   images=[]
   for t in range(0,4400,40):
    values=(ctypes.c_double*3)();self.lib.recline_preview_pose(t,values)
    images.append(self.image(c,int(values[0]),int(values[1]),values[2],2,elapsed=t))
   images[0].save(self.output/f'{NAMES[c].lower()}-belly.gif',save_all=True,append_images=images[1:],duration=40,disposal=2)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=ROOT/'build/previews/recline-register');parser.add_argument('--animations',action='store_true');parser.add_argument('--benchmark',action='store_true');args=parser.parse_args()
 preview=Preview(args.output.resolve());preview.contact()
 if args.animations:preview.animations()
 if args.benchmark:preview.benchmarks()
 files=['src/cat_recline_transition.hpp','src/cat_animation.hpp','src/cat_species.hpp','src/cat_species_belly.hpp','tools/recline_preview.cpp','assets/cat.png','assets/cat-silver.png','assets/cat-recline.png','assets/companion-poses.png','assets/companions-back.png']
 (preview.output/'provenance.json').write_text(json.dumps({'renderer':'production C++ head registration/eyelid + Pillow endpoint display; not Windows/GDI+','sources':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}},indent=2)+'\n')
 print(preview.output/'all-species.png')
if __name__=='__main__':main()
