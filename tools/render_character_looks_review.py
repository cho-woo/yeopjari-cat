#!/usr/bin/env python3
"""Offline source-art contact boards, explicitly separate from Windows output."""
import io,json,math,subprocess
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from bake_character_looks import frame_rects
from import_styled_sprites import runtime_care_entries
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/previews/v2.0.26/appearance';OUT.mkdir(parents=True,exist_ok=True)
FONT=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',12)
BG='#d8d4d1';INK='#343a43';care={e['resource']:e for e in runtime_care_entries(ROOT)}
manifest=json.loads((ROOT/'assets/styled-sprites/manifest.json').read_text());entries={e['base_resource_id']:e for e in manifest['atlases'] if e['character']==8}

def sources(base):
 e=entries[base];return [Image.open(ROOT/e['base_source']).convert('RGBA'),Image.open(ROOT/'assets/styled-sprites'/e['source']).convert('RGBA'),Image.open(ROOT/f'assets/styled-sprites/c10-r{base}.png').convert('RGBA')]

def pose(im,base,index):
 rect=frame_rects(base,im.size,care)[index];x,y,w,h=rect;crop=im.crop((x,y,x+w,y+h))
 if base in care:
  ref=care[base]['reference_height'];anchor=care[base]['frames'][index]['anchor']
 else:ref=440 if base==107 else 367;anchor=[w/2,h]
 return crop,ref,anchor

def draw_pose(board,im,base,index,cx,bottom,height=110):
 crop,ref,anchor=pose(im,base,index);factor=height/ref
 crop=crop.resize((max(1,round(crop.width*factor)),max(1,round(crop.height*factor))),Image.Resampling.LANCZOS)
 board.alpha_composite(crop,(round(cx-anchor[0]*factor),round(bottom-anchor[1]*factor)))

samples=[('Idle',101,4),('Walk',400,5),('Run',401,4),('Jump source',400,29),('Groom',113,6),('Fish',116,2),('Eating',118,40),('Treat',120,10),('Sleep',101,6)]
board=Image.new('RGBA',(9*180,3*165+50),BG);d=ImageDraw.Draw(board);d.text((12,10),'OFFLINE source art: 110 logical px common standing reference | not Windows capture',font=FONT,fill=INK)
for col,(name,base,frame) in enumerate(samples):
 ims=sources(base)
 for row,im in enumerate(ims):
  draw_pose(board,im,base,frame,col*180+90,50+row*165+132);d.text((col*180+4,50+row*165+140),f'{["Cream","Soft Tux","OddEye"][row]} / {name}',font=FONT,fill=INK)
board.convert('RGB').save(OUT/'three-cats-110px-source.png')
# Every retained drawing is shown. A single sheet-level scale is used, never
# silhouette-normalizing one frame; props cannot shrink the character.
for base,e in entries.items():
 ims=sources(base);count=len(frame_rects(base,ims[0].size,care));cols=min(8,count);rows=math.ceil(count/cols)
 panel=Image.new('RGBA',(cols*180,50+rows*3*165),BG);d=ImageDraw.Draw(panel);d.text((10,10),f'OFFLINE ALL SOURCE FRAMES / resource {base} / {count} drawings / fixed 110px reference',font=FONT,fill=INK)
 for row,im in enumerate(ims):
  for index in range(count):
   x=(index%cols)*180;y=50+(row*rows+index//cols)*165;draw_pose(panel,im,base,index,x+90,y+132);d.text((x+4,y+140),f'{["Cream","Soft Tux","OddEye"][row]} f{index}',font=FONT,fill=INK)
 panel.convert('RGB').save(OUT/f'all-frames-r{base}.png')
# Before/after covers every source atlas with one identical representative pose.
base_list=list(entries);compare=Image.new('RGBA',(4*190,60+math.ceil(len(base_list)/4)*2*165),BG);d=ImageDraw.Draw(compare);d.text((12,10),'OFFLINE v2.0.25 Tux / softer v2.0.26 Tux; identical source scale and poses',font=FONT,fill=INK)
for i,base in enumerate(base_list):
 old=Image.open(io.BytesIO(subprocess.check_output(['git','show','HEAD:assets/styled-sprites/'+entries[base]['source']],cwd=ROOT))).convert('RGBA');new=sources(base)[1]
 for j,im in enumerate([old,new]):
  x=i%4*190;y=60+(i//4*2+j)*165;frame=4 if base==101 else 0;draw_pose(compare,im,base,frame,x+95,y+130);d.text((x+4,y+140),f'r{base} {"before" if j==0 else "after"}',font=FONT,fill=INK)
compare.convert('RGB').save(OUT/'tuxedo-full-actions-before-after.png')
print(OUT)
