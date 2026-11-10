#!/usr/bin/env python3
"""Technical video foreground extraction. Preserves decoded RGB; only writes alpha.

Authoring dependencies: numpy, opencv-python-headless, onnxruntime, Pillow.
Use U2NETP saliency as a seed, color graph-cut at source resolution for the actual
outline, and a subpixel matte. No generated drawings or per-frame scale changes.
Run mode protects white fur; Eat mode retains the metal bowl and scattered kibble.
"""
from __future__ import annotations
import argparse, hashlib, json, time
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort
from PIL import Image, ImageDraw

def model_session(path):
    options=ort.SessionOptions(); options.intra_op_num_threads=2; options.inter_op_num_threads=1
    return ort.InferenceSession(str(path),options,providers=['CPUExecutionProvider'])

def probability(rgb, session):
    image=cv2.resize(rgb,(320,320),interpolation=cv2.INTER_AREA).astype(np.float32)
    image=image/max(1.,float(image.max()))
    image=(image-np.array([.485,.456,.406],np.float32))/np.array([.229,.224,.225],np.float32)
    output=session.run(None,{session.get_inputs()[0].name:image.transpose(2,0,1)[None]})[0][0,0]
    output=(output-output.min())/max(1e-8,float(output.max()-output.min()))
    return cv2.resize(output,(rgb.shape[1],rgb.shape[0]),interpolation=cv2.INTER_CUBIC).clip(0,1)

def largest(mask):
    count,labels,stats,_=cv2.connectedComponentsWithStats(mask.astype(np.uint8),8)
    if count<=1:raise RuntimeError('No foreground component')
    chosen=1+int(np.argmax(stats[1:,cv2.CC_STAT_AREA]));return (labels==chosen).astype(np.uint8)

def backdrop_seeds(rgb):
    """Flood only source-backdrop colours connected to the video border.

    The pink/blue scrolling blobs can fool saliency into being foreground.
    Their flat colour families are observable at the border; a dark drawn
    contour keeps identically coloured skin/fur inside the cat disconnected.
    """
    height,width=rgb.shape[:2]
    border=np.concatenate([rgb[:8].reshape(-1,3),rgb[-8:].reshape(-1,3),
        rgb[:,:8].reshape(-1,3),rgb[:,-8:].reshape(-1,3)]).astype(np.float32)
    cv2.setRNGSeed(1729)
    _,_,centres=cv2.kmeans(border,8,None,(cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_MAX_ITER,30,.1),3,cv2.KMEANS_PP_CENTERS)
    dist=np.sqrt(np.min(np.sum((rgb.astype(np.float32)[:,:,None,:]-centres[None,None,:,:])**2,axis=3),axis=2))
    candidate=(dist<10).astype(np.uint8)
    count,components,_,_=cv2.connectedComponentsWithStats(candidate,8)
    ids=np.unique(np.concatenate([components[0],components[-1],components[:,0],components[:,-1]]))
    ids=ids[ids!=0];known=np.isin(components,ids).astype(np.uint8)
    # Include compressed/antialiased backdrop edge pixels without crossing
    # the kitten's much darker outline or changing any decoded source RGB.
    nearby=cv2.dilate(known,np.ones((3,3),np.uint8))>0
    return (known.astype(bool)|(nearby&(dist<25)))

def matte(rgb, probability, iterations=4, mode="walk"):
    labels=np.where(probability>.5,cv2.GC_PR_FGD,cv2.GC_PR_BGD).astype(np.uint8)
    labels[probability<.015]=cv2.GC_BGD
    labels[probability>.985]=cv2.GC_FGD
    backdrop=backdrop_seeds(rgb)
    if mode=="run":
        # A genuinely white fur highlight is indistinguishable from a white
        # backdrop by colour alone. Keep the semantic foreground interior.
        backdrop &= probability<.75
    elif mode=="eat":
        # The metal bowl has the same neutral family as the studio backdrop.
        # Its confident semantic interior must survive colour refinement.
        backdrop &= probability<.985
    labels[backdrop]=cv2.GC_BGD
    if mode=="eat":
        warm=(rgb[:,:,0].astype(np.int16)-rgb[:,:,1]>18)&(rgb[:,:,1].astype(np.int16)-rgb[:,:,2]>12)
        labels[warm]=cv2.GC_FGD
    # Borders contain only the provided source's backdrop, never a cat pixel.
    labels[:2]=labels[-2:]=cv2.GC_BGD; labels[:,:2]=labels[:,-2:]=cv2.GC_BGD
    fg=np.zeros((1,65),np.float64);bg=np.zeros((1,65),np.float64)
    cv2.grabCut(cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR),labels,None,bg,fg,iterations,cv2.GC_INIT_WITH_MASK)
    foreground=((labels==cv2.GC_FGD)|(labels==cv2.GC_PR_FGD)).astype(np.uint8)
    if mode=="eat":
        # Bowl and individual kibble crumbs are legitimately disconnected.
        count,components,stats,centres=cv2.connectedComponentsWithStats(foreground,8)
        area=stats[:,cv2.CC_STAT_AREA]
        # Tiny detached items are food crumbs near bowl/floor, not UI/title
        # pixels present in the supplied video's initial splash frame.
        keep=np.where((area>=2500)|((area>=6)&(centres[:,1]>=rgb.shape[0]*.55)))[0]
        keep=keep[keep!=0]
        hard=np.isin(components,keep).astype(np.uint8)
    else:hard=largest(foreground)
    alpha=cv2.GaussianBlur(hard.astype(np.float32),(0,0),.42)
    alpha[hard.astype(bool)&(cv2.distanceTransform(hard,cv2.DIST_L2,5)>1.1)]=1
    alpha=np.clip(alpha*255+.5,0,255).astype(np.uint8)
    alpha[alpha<=16]=0
    return alpha

def proof(rgb, alpha, out, label):
    image=Image.fromarray(np.dstack([rgb,alpha]),'RGBA')
    board=Image.new('RGB',(rgb.shape[1]*2,rgb.shape[0]+30),'#eeeeee');draw=ImageDraw.Draw(board)
    for column,color in enumerate(['#666b73','#121820']):
        panel=Image.new('RGBA',image.size,color);panel.alpha_composite(image)
        board.paste(panel.convert('RGB'),(column*image.width,30))
        draw.text((column*image.width+8,8),label+' / '+color,fill='#17212c')
    board.save(out)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--frames',type=Path,required=True)
    parser.add_argument('--model',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--only',help='Comma-separated 1-based frame numbers')
    parser.add_argument('--mode',choices=['walk','run','eat'],default='walk')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    debug=args.output/'diagnostics';debug.mkdir(exist_ok=True)
    paths=sorted(args.frames.glob('*.png'));selected={int(x) for x in args.only.split(',')} if args.only else None
    session=model_session(args.model);records=[];started=time.monotonic()
    for index,path in enumerate(paths,1):
        if selected and index not in selected:continue
        rgb=np.array(Image.open(path).convert('RGB'));p=probability(rgb,session);a=matte(rgb,p,mode=args.mode)
        destination=args.output/f'{index:04}.png';Image.fromarray(np.dstack([rgb,a]),'RGBA').save(destination)
        Image.fromarray((p*255).astype(np.uint8)).save(debug/f'{index:04}-probability.png')
        if selected or index in [1,25,61,109]:proof(rgb,a,debug/f'{index:04}-proof.png',path.name)
        ys,xs=np.where(a>128);records.append({'frame':index,'source':path.name,'rgba':destination.name,'bbox':[int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1)],'opaque_pixels':int((a>128).sum())})
        print('matted',index,flush=True)
    report={'schema':1,'method':'u2netp-seeded full-resolution color graphcut, alpha-only extraction','model_sha256':hashlib.sha256(args.model.read_bytes()).hexdigest(),'source_rgb_preserved':True,'mode':args.mode,'per_frame_scaling':False,'elapsed_seconds':time.monotonic()-started,'frames':records}
    (args.output/'matting-report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
