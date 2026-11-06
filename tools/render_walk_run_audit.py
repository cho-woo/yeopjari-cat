#!/usr/bin/env python3
"""Read-only, pinned-source audit of the walk and run dispatchers.

Production C++ Evaluate/Locomotion and the selected Rig render each pose.
Pillow supplies final placement; this is not live Windows/GDI+/Box2D output.
Pinterest references are not downloaded or inferred by this tool.
"""
from pathlib import Path
import argparse,ctypes,hashlib,json,math,subprocess
from PIL import Image,ImageDraw,ImageChops
from render_motion_preview import atlas
from render_walk_review import save_gif
ROOT=Path(__file__).resolve().parents[1]

BRIDGE=r'''
#include "cat_animation.hpp"
#include "cat_articulated_walk.hpp"
#include "cat_locomotion_rig.hpp"
struct Review { cat::animation::Locomotion gait;cat::articulated::Rig joint;cat::walk::Rig legacy;int w=0,h=0;bool active=false;double reach=0; };
extern "C" {
void* audit_create(const std::uint32_t* pixels,int w,int h,int character) {
 auto* r=new Review;r->w=w;r->h=h;
 if(!r->joint.Prepare(pixels,w,h,w*4)||!r->legacy.Prepare(pixels,w,h,w*4,cat::walk::ProfileFor(static_cast<cat::Character>(character)))){delete r;return nullptr;}
 return r;
}
void audit_destroy(void* p){delete static_cast<Review*>(p);}
int audit_step(void* p,int running,double distance,double dt,double width,double height,double* o){
 auto& r=*static_cast<Review*>(p);const auto motion=running?cat::animation::Motion::Zoomies:cat::animation::Motion::Walk;
 o[35]=0;
 if(!r.gait.Advance(motion,distance,dt))return 0;
 const auto pose=cat::animation::Evaluate(motion,0,500000,1,r.gait);
 r.active=pose.jointWalk;
 const auto feet=r.active?pose.jointFeet:pose.walkFeet;
 o[0]=r.active?pose.jointPhase:pose.walkPhase;o[1]=r.active?18:pose.walkStride;
 o[2]=r.active?.72:pose.walkDuty;o[3]=r.active?2.6:pose.walkLift;
#ifdef AUDIT_HAS_JOINT_DYNAMICS
 if(r.active) {
#ifdef AUDIT_HAS_ACTUAL_MODE
  const auto settings=cat::walk_dynamics::ParametersFor(r.gait.JointMode());
#else
  const auto settings=cat::walk_dynamics::ParametersFor(running?cat::walk_dynamics::Mode::Gallop:cat::walk_dynamics::Mode::Walk);
#endif
  o[1]=settings.stride;o[2]=settings.duty;o[3]=settings.lift;
  o[16]=pose.jointDynamics.torsoX;o[17]=pose.jointDynamics.torsoY;o[18]=pose.jointDynamics.pitchDegrees;o[19]=pose.jointDynamics.airborneWeight;
#ifdef AUDIT_HAS_SPINE
  o[21]=pose.jointDynamics.spineStretch;o[22]=pose.jointDynamics.tailDegrees;
#endif
 }
#endif
 for(int i=0;i<4;++i){o[4+i*3]=feet[i].x;o[5+i*3]=feet[i].y;o[6+i*3]=feet[i].stance;o[23+i]=static_cast<double>(feet[i].contact);}
 bool okay=false;
#ifdef AUDIT_HAS_JOINT_DYNAMICS
 if(r.active)okay=r.joint.Raster(feet,width,height,pose.jointDynamics);
 else okay=r.legacy.Raster(pose.walkPhase,pose.walkStride,width,height,pose.walkDuty,pose.walkLift,&feet);
#else
 okay=r.active?r.joint.Raster(feet,width,height):r.legacy.Raster(pose.walkPhase,pose.walkStride,width,height,pose.walkDuty,pose.walkLift,&feet);
#endif
 o[20]=r.active?r.joint.ReachError():0;
 if(r.active && okay)for(unsigned i=0;i<4;++i){const auto& leg=r.joint.Legs()[i];o[27+i*2]=leg.paw.x*width/r.w;o[28+i*2]=leg.paw.y*height/r.h;if(leg.clamped)o[35]+=1u<<i;}
 return okay;
}
const std::uint32_t* audit_pixels(void* p){auto&r=*static_cast<Review*>(p);return r.active?r.joint.Pixels():r.legacy.Pixels();}
int audit_width(void* p){auto&r=*static_cast<Review*>(p);return r.active?r.joint.Width():r.legacy.Width();}
int audit_height(void* p){auto&r=*static_cast<Review*>(p);return r.active?r.joint.Height():r.legacy.Height();}
int audit_padding(void* p){auto&r=*static_cast<Review*>(p);return r.active?r.joint.Height()-r.h:0;}
double audit_offset(void* p){auto&r=*static_cast<Review*>(p);return r.active?0:r.legacy.BodyOffset();}
int audit_mode(void* p){
#ifdef AUDIT_HAS_ACTUAL_MODE
 return static_cast<int>(static_cast<Review*>(p)->gait.JointMode());
#else
 (void)p;return -1;
#endif
}
void audit_leg_errors(void* p,double width,double height,double* o){
 auto& r=*static_cast<Review*>(p);
 for(unsigned i=0;i<4;++i){o[i]=0;if(r.active){const auto& leg=r.joint.Legs()[i];o[i]=std::hypot((leg.paw.x-leg.targetPaw.x)*width/r.w,(leg.paw.y-leg.targetPaw.y)*height/r.h);}}
}
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source-ref',default='HEAD',help='Git ref or WORKTREE')
    ap.add_argument('--audit-transitions',action='store_true',help='Audit actual solved-paw world anchors across Walk/Run configuration changes')
    ap.add_argument('--body-width',type=float,default=1.0)
    ap.add_argument('--body-height',type=float,default=1.0)
    ap.add_argument('--walk-speed',type=float,default=22.0,help='Actual logical body speed for walking, including faster window/toy approaches')
    ap.add_argument('--audit-only',action='store_true')
    ap.add_argument('--frames',type=int,default=90)
    ap.add_argument('--compare-before',type=Path,help='Optional output directory from an earlier pinned run')
    ap.add_argument('--output',type=Path,default=ROOT/'build/previews/walk-run-audit')
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if args.frames<22:ap.error('--frames must be at least22')
    if not (.5<=args.body_width<=2 and .5<=args.body_height<=2):ap.error('Body dimensions out of review range')
    if not math.isfinite(args.walk_speed) or not (0<args.walk_speed<=85):ap.error('--walk-speed must be greater than0 and at most85 bodypx/s')
    working=args.source_ref=='WORKTREE'
    sha=subprocess.check_output(['git','rev-parse','HEAD' if working else args.source_ref],cwd=ROOT,text=True).strip()
    inc=out/'source';inc.mkdir(exist_ok=True);hashes={}
    names=[str(path.relative_to(ROOT)) for path in (ROOT/'src').glob('*.hpp')] if working else subprocess.check_output(['git','ls-tree','-r','--name-only',sha,'src'],cwd=ROOT,text=True).splitlines()
    for name in names:
        if name.endswith('.hpp'):
            data=(ROOT/name).read_bytes() if working else subprocess.check_output(['git','show',sha+':'+name],cwd=ROOT)
            (inc/Path(name).name).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
    (out/'bridge.cpp').write_text(BRIDGE)
    flags=['-DAUDIT_HAS_JOINT_DYNAMICS'] if 'jointDynamics' in (inc/'cat_animation.hpp').read_text() else []
    if 'JointMode()' in (inc/'cat_animation.hpp').read_text():flags.append('-DAUDIT_HAS_ACTUAL_MODE')
    if (inc/'cat_walk_dynamics.hpp').exists() and 'spineStretch' in (inc/'cat_walk_dynamics.hpp').read_text():flags.append('-DAUDIT_HAS_SPINE')
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',*flags,'-I'+str(inc),str(out/'bridge.cpp'),'-o',str(out/'audit.so')],check=True)
    lib=ctypes.CDLL(str(out/'audit.so'));p=ctypes.c_void_p;d=ctypes.c_double;i=ctypes.c_int
    for name,types,result in [('create',[p,i,i,i],p),('destroy',[p],None),('step',[p,i,d,d,d,d,ctypes.POINTER(d)],i),('pixels',[p],p),('width',[p],i),('height',[p],i),('padding',[p],i),('offset',[p],d),('mode',[p],i),('leg_errors',[p,d,d,ctypes.POINTER(d)],None)]:
        f=getattr(lib,'audit_'+name);f.argtypes=types;f.restype=result
    cases=[]
    for character,file in enumerate(('cat.png','cat-silver.png')):
        frames,_=atlas(file);source=frames[1];scale=110/max(im.height for im in frames[:6]);raw=source.convert('RGBa').tobytes('raw','BGRa');memory=ctypes.create_string_buffer(raw,len(raw))
        for gait,speed in ((0,args.walk_speed),(1,165.)):
            instance=lib.audit_create(memory,source.width,source.height,character)
            if not instance:raise RuntimeError('Prepare')
            for _ in range(180):
                values=(d*36)()
                if not lib.audit_step(instance,gait,speed/60,1/60,source.width*scale*args.body_width,source.height*scale*args.body_height,values):raise RuntimeError('Warmup')
            cases.append((character,gait,speed,source,scale,memory,instance))
    raster_audit={}
    def panel(case,time,zoom=1):
        character,gait,speed,source,scale,memory,instance=case
        w,h=lib.audit_width(instance),lib.audit_height(instance)
        data=ctypes.string_at(lib.audit_pixels(instance),w*h*4)
        image=Image.frombytes('RGBa',(w,h),data,'raw','BGRa').convert('RGBA')
        if zoom==1:
            premul=Image.frombytes('RGBA',(w,h),data,'raw','BGRA');alpha=premul.getchannel('A')
            bounds=alpha.point(lambda x:255 if x>153 else 0).getbbox()
            invalid=any(ImageChops.subtract(premul.getchannel(c),alpha).getbbox() for c in ('R','G','B'))
            raster_audit[(character,gait,time)]={'alpha_bounds':bounds,'touches_top_edge':bool(bounds and bounds[1]==0),'touches_bottom_edge':bool(bounds and bounds[3]>=h),'painted_ground_bodypx':None if not bounds else (bounds[3]-(source.height-2))*scale*args.body_height+lib.audit_offset(instance),'invalid_premult_alpha':invalid}

        image=image.resize((round(w*scale*args.body_width*zoom),round(h*scale*args.body_height*zoom)),Image.Resampling.BICUBIC)
        canvas=Image.new('RGBA',(260*zoom,200*zoom),'#edf0f4');draw=ImageDraw.Draw(canvas);ground=160*zoom
        for x in range(-100,400,10):
            pos=(x-time*speed%10)*zoom;draw.line((pos,ground,pos,ground+4*zoom),fill='#9ba8b7')
        draw.line((0,ground,260*zoom,ground),fill='#64758a')
        x=130*zoom-image.width/2;y=ground-image.height+((2+lib.audit_padding(instance))*scale*args.body_height+lib.audit_offset(instance))*zoom
        placed=image.transform(canvas.size,Image.Transform.AFFINE,(1,0,-x,0,1,-y),Image.Resampling.BICUBIC)
        canvas.alpha_composite(placed)
        draw.text((4*zoom,4*zoom),('Cream','Silver')[character]+' '+('WALK','RUN')[gait]+f' {speed:g} px/s t={time:.2f}s',fill='#203344')
        return canvas.convert('RGB')
    movies=[];details={};samples=[]
    try:
        for frame in range(args.frames):
            time=frame*.04;board=Image.new('RGB',(520,400));large=Image.new('RGB',(1560,1200))
            for case in cases:
                character,gait,speed,source,scale,memory,instance=case;values=(d*36)()
                if not lib.audit_step(instance,gait,0 if frame==0 else speed*.04,0 if frame==0 else .04,source.width*scale*args.body_width,source.height*scale*args.body_height,values):raise RuntimeError('Raster')
                board.paste(panel(case,time),(gait*260,character*200));view=None if args.audit_only else panel(case,time,3)
                if view is not None:large.paste(view,(gait*780,character*600))
                samples.append({'character':character,'running':bool(gait),'actual_joint_mode':lib.audit_mode(instance),'t':time,'phase':values[0],'stride':values[1],'duty':values[2],'lift':values[3],'body_offset':lib.audit_offset(instance),'feet':list(values)[4:16],'torso_x':values[16],'torso_y':values[17],'pitch_degrees':values[18],'airborne_weight':values[19],'reach_error':values[20],'spine_stretch':values[21],'tail_degrees':values[22],'clamped_leg_mask':int(values[35]),'contact_ids':list(values)[23:27],'solved_paws_bodypx':list(values)[27:35],**raster_audit[(character,gait,time)]})
                details[(character,gait,frame)]=view
            movies.append(board)
            if frame==0 and not args.audit_only:large.save(out/'walk-run-3x.png')
        if not args.audit_only:save_gif(movies,out/'walk-run.gif',40)
        if args.compare_before and not args.audit_only:
            old_path=args.compare_before/'walk-run.gif'
            if not old_path.exists():old_path=args.compare_before/'walk-run-before.gif'
            old=Image.open(old_path);combined=[]
            for frame,image in enumerate(movies):
                old.seek(frame%old.n_frames)
                board=Image.new('RGB',(1040,424),'#edf0f4');draw=ImageDraw.Draw(board)
                board.paste(old.convert('RGB'),(0,24));board.paste(image,(520,24))
                draw.text((5,5),'PINNED BASELINE',fill='#203344');draw.text((525,5),'CURRENT CANDIDATE',fill='#203344')
                combined.append(board)
            save_gif(combined,out/'before-candidate.gif',40)

        for gait,name,indexes in ([] if args.audit_only else [(0,'walk',[0,3,6,9,12,15,18,21]),(1,'run',[0,1,2,3,4,5,6,7])]):
            for character,label in ((0,'cream'),(1,'silver')):
                sheet=Image.new('RGB',(4*440,2*160),'#edf0f4')
                for pos,frame in enumerate(indexes):
                    crop=details[(character,gait,frame)].crop((145,335,585,495))
                    ImageDraw.Draw(crop).text((4,4),f'{name} {frame*.04:.2f}s',fill='#203344')
                    sheet.paste(crop,((pos%4)*440,(pos//4)*160))
                sheet.save(out/f'{label}-{name}-feet.png')
        transition=[]
        if args.audit_transitions:
            for character,file in enumerate(('cat.png','cat-silver.png')):
                frame_list,_=atlas(file);source=frame_list[1];scale=110/max(im.height for im in frame_list[:6]);raw=source.convert('RGBa').tobytes('raw','BGRa');memory=ctypes.create_string_buffer(raw,len(raw));instance=lib.audit_create(memory,source.width,source.height,character)
                width=source.width*scale*args.body_width;height=source.height*scale*args.body_height
                root=0.;previous=None;max_drift=0.;contacts=0;reach=0.;checked=0;worst_reach=None;worst_drift=None
                stance_target_error=0.;swing_target_error=0.;clamp_stance_samples=0;clamp_swing_samples=0
                try:
                    for step in range(480):
                        if step<90:running,speed=0,22.
                        elif step<150:running,speed=1,22+(165-22)*(step-90)/60
                        elif step<300:running,speed=1,165.
                        elif step<360:running,speed=0,165-(165-22)*(step-300)/60
                        else:running,speed=0,22.
                        distance=speed/60;root+=distance;values=(d*36)()
                        if not lib.audit_step(instance,running,distance,1/60,width,height,values):raise RuntimeError('Transition raster rejected actual motion')
                        leg_errors=(d*4)();lib.audit_leg_errors(instance,width,height,leg_errors)
                        for leg,error in enumerate(leg_errors):
                            if values[6+leg*3]:
                                stance_target_error=max(stance_target_error,error)
                                clamp_stance_samples+=bool(int(values[35])&(1<<leg))
                            else:
                                swing_target_error=max(swing_target_error,error)
                                clamp_swing_samples+=bool(int(values[35])&(1<<leg))
                        if values[20]>reach:
                            reach=values[20];worst_reach={'step':step,'running':bool(running),'actual_joint_mode':lib.audit_mode(instance),'speed':speed,'phase':values[0],'clamped_leg_mask':int(values[35]),'feet':list(values)[4:16],'torso_y':values[17],'pitch_degrees':values[18]}

                        world=[(root+values[27+leg*2],values[28+leg*2],values[6+leg*3],values[23+leg]) for leg in range(4)]
                        if previous:
                            for leg in range(4):
                                current,old=world[leg],previous[leg]
                                if current[2] and old[2] and current[3]==old[3]:
                                    drift=max(abs(current[0]-old[0]),abs(current[1]-old[1]));checked+=1
                                    if drift>max_drift:
                                        max_drift=drift;worst_drift={'step':step,'leg':leg,'running':bool(running),'actual_joint_mode':lib.audit_mode(instance),'speed':speed,'phase':values[0],'old_paw':old,'current_paw':current,'clamped_leg_mask':int(values[35])}
                                if current[3]!=old[3]:contacts+=1
                        previous=world
                    transition.append({'character':character,'body_width':args.body_width,'body_height':args.body_height,'steps':480,'stance_segments_checked':checked,'contacts':contacts,'max_solved_world_stance_drift_bodypx':max_drift,'max_reach_error_bodypx':reach,'max_stance_target_error_bodypx':stance_target_error,'max_swing_target_error_bodypx':swing_target_error,'clamped_stance_samples':clamp_stance_samples,'clamped_swing_samples':clamp_swing_samples,'worst_reach':worst_reach,'worst_stance_drift':worst_drift})
                finally:lib.audit_destroy(instance)
        (out/'transitions.json').write_text(json.dumps(transition,indent=2)+'\n')
        manifest={'source_commit':sha,'working_tree':working,'body_width':args.body_width,'body_height':args.body_height,'source_hashes':hashes,'transition_checks':transition,'scope':'Cream/Silver actual Evaluate Walk and Zoomies dispatcher with optional joint dynamics','limits':'C++ actual evaluators and rigs, constant relative travel replay, Pillow placement. Not live Win32/GDI+/Box2D. Pinterest refs not viewed or downloaded.','samples':samples,'tool_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        (out/'audit.json').write_text(json.dumps(manifest,indent=2)+'\n')
    finally:
        for case in cases:lib.audit_destroy(case[-1])
    print(out)
if __name__=='__main__':main()
