package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import java.util.ArrayDeque;
import java.util.Random;

/** One scene in dp, used by Home, preview, and both cats. Only resize and explicit dragging relocate roots. */
final class PetScene {
    static float clamp(float value,float lo,float hi){return Math.max(lo,Math.min(Math.max(lo,hi),value));}
    static float smooth(float v){v=clamp(v,0,1);return v*v*(3-2*v);}
    static final class Step {
        final String name;
        float x=Float.NaN,y=Float.NaN;
        int direction,bridge;
        boolean platform;
        boolean travel, continuous;
        Step(String name){this.name=name;}
    }
    final class Actor {
        final int coat;
        final PlayObject object=new PlayObject();
        final ArrayDeque<Step> plan=new ArrayDeque<>();
        MotionLibrary.Clip idle,clip,previousPose;
        MotionClock clock;
        Step step;
        String action="idle",owner="",queued,loadingAction;
        float x,startX,offsetY,lift,startY,speed,fade;
        float nextGroundMin=Float.NaN;
        float fingerX,fingerY,attackX,attackY,jumpHeight,jumpDx,stopTravel;
        float carryLift,carryMs,landingDy,fingerMiss=Float.POSITIVE_INFINITY;
        int facing=1,idleFacing=1,previousFacing=1,previousFrame,idleFrame,revision;
        boolean controlled,loading,gaitExit,sideReady,fingerDown,fingerArmed;
        double age,restMs,fingerAim;
        long completed,landings;
        int ballHits;
        String finishedOwner="";
        Actor(int coat,float x){
            this.coat=coat;this.x=x;restMs=500+random.nextInt(901);
            idle=library.neutral(coat);idleFrame=0;
            library.preload(coat,"run",null);
        }
        boolean busy(){return loading||clock!=null||!plan.isEmpty()||!owner.isEmpty();}
        float ground(){return floor+offsetY;}
        float footY(){return ground()-lift-carryLift*smooth(carryMs/320);}
        float margin(){return size*.68f;}
        int toward(float dest){return dest<x?-1:1;}
        float safeX(float dest){return clamp(dest,margin(),width-margin());}
        float safeY(float dest){return clamp(dest,Math.min(height*.8f,size+12),height-12);}
        void pose(String name){plan.add(new Step(name));}
        void facePose(String name,int dir){Step s=new Step(name);s.direction=dir;plan.add(s);}
        void route(String gait,float px,float py,boolean continuous){
            Step s=new Step(gait);s.travel=true;s.x=safeX(px);s.y=safeY(py);s.continuous=continuous;plan.add(s);
        }
        void remember(){
            if(clip!=null&&clock!=null){idle=clip;idleFrame=clock.frame();idleFacing=facing;}
        }
        // Keep the small working set for the current interaction even if another coat
        // evicts an entry from the shared LRU. Late replies cannot retain an old story.
        java.util.HashMap<String,MotionLibrary.Clip> upcoming=new java.util.HashMap<>();
        void reserve(String name){
            if(upcoming.containsKey(name))return;
            java.util.HashMap<String,MotionLibrary.Clip> story=upcoming;
            story.put(name,null);
            library.preload(coat,name,value->{
                if(!closed && upcoming==story){
                    if(value==null)story.remove(name);else story.put(name,value);
                }
            });
        }
        void releaseClips(){if(!upcoming.isEmpty())upcoming=new java.util.HashMap<>();}
        void obtain(String name,MotionLibrary.Ready ready){
            MotionLibrary.Clip resident=upcoming.get(name);
            if(resident!=null)ready.accept(resident);else library.request(coat,name,ready);
        }
        void armFinger(){cancelSocial();cancelStory();fingerArmed=true;library.preload(coat,"run",null);library.preload(coat,"cursor-pounce",null);}
        void cancelStory(){
            remember();releaseClips();revision++;loading=false;loadingAction=null;clock=null;clip=null;
            plan.clear();queued=null;owner="";action="idle";object.kind="";fingerDown=false;
            // User input may interrupt in midair. Keep the visible baseline and ease back on the next clock.
            if(lift!=0){carryLift=lift+carryLift*smooth(carryMs/320);carryMs=320;lift=0;}
            sideReady=false;speed=0;step=null;
        }
        float freeDestination(float wanted){
            float dest=safeX(wanted);
            Actor other=actors[0]==this?actors[1]:actors[0];
            if(other!=null&&Math.abs(other.ground()-ground())<size*.8f){
                float gap=size*1.04f;
                float near=other.x;
                if(other.step!=null&&other.step.travel&&Math.abs(other.step.y-ground())<size*.8f){
                    near=other.x>x?Math.min(near,other.step.x):Math.max(near,other.step.x);
                }
                if(other.x>x)dest=Math.min(dest,Math.max(x,near-gap));
                if(other.x<x)dest=Math.max(dest,Math.min(x,near+gap));
            }
            return dest;
        }
        float roam(float factor){
            float right=freeDestination(x+size*factor),left=freeDestination(x-size*factor);
            if(right-x<size*.65f)return left;
            if(x-left<size*.65f)return right;
            return random.nextBoolean()?right:left;
        }
        void request(String next){
            if(busy()){queued=next;return;}
            start(next);
        }
        void start(String next){
            owner=next;float ground=ground();
            switch(next){
                case "walk": case "stalk":
                    route("walk",roam(next.equals("walk")?2.5f:1.5f),ground,false);break;
                case "run": case "turn": case "brake": case "slide": case "stumble":{
                    float dest=roam(4.5f);int dir=toward(dest);
                    boolean turn=next.equals("turn")||(next.equals("run")&&random.nextInt(3)==0);
                    String stop=next.equals("run")||turn?new String[]{"brake","brake","slide","stumble"}[random.nextInt(4)]:next;
                    route("run",dest-dir*Math.min(size*.35f,Math.abs(dest-x)*.18f),ground,true);
                    if(turn){facePose("turn",dir);route("run",x,ground,true);dir=-dir;}
                    facePose(stop,dir);break;
                }
                case "perch":{
                    float dest=roam(1.55f),platform=safeY(ground-size*.55f);
                    object.place("ledge",dest,platform);
                    platformJump(dest,platform);pose("balance");pose("look-down");platformJump(x,ground);break;
                }
                case "climb": case "hang":
                    route("walk",width-size*.48f,ground,false);
                    plan.getLast().x=width-48*size/110;
                    facePose(next,1);break;
                case "ball": case "ball-pounce":{
                    int dir=width-x>x?1:-1;
                    object.place("ball",safeX(x+dir*size*(1.25f+random.nextFloat()*.65f)),ground);
                    appendBall(next);appendBall(next.equals("ball")?"ball-pounce":"ball");break;
                }
                case "cushion": case "catnip":
                    object.place(next,x,ground);
                    pose(next.equals("cushion")?"knead":"roll");
                    if(next.equals("cushion"))pose("sleep");break;
                case "tower":case "box":case "scratch":{
                    int dir=x>width*.5f?-1:1;
                    float root=clamp(x,dir<0?size*1.95f:size*.84f,dir>0?width-size*1.95f:width-size*.84f);
                    float base=safeY(Math.max(ground,size*2.34f));
                    if(Math.abs(x-root)+Math.abs(ground-base)>1)route("walk",root,base,false);
                    facePose(next,dir);break;
                }
                case "scratch-fur":pose("groom");break;
                case "sleep-talk":pose("loaf");pose("sleep-talk");pose("yawn");break;
                default:pose(next);
            }
            nextStep();
        }
        void platformJump(float dest,float base){
            Step s=new Step("jump");s.direction=toward(dest);s.x=dest;s.y=base;s.platform=true;plan.add(s);
        }
        boolean sidePose(String name){
            return name.equals("jump")||name.equals("cursor-pounce")||name.equals("punch")||
                name.equals("reach")||name.equals("climb")||name.equals("ball");
        }
        float standoff(String kind){
            boolean pounce=kind.equals("ball-pounce");
            float px=pounce?(coat==8?44.12f:42.20f):(coat==8?33.23f:34.375f);
            float py=pounce?(coat==8?13.43f:14.07f):3.4375f;
            return (px+.7f*(float)Math.sqrt(196-(py-12)*(py-12)))*size/110;
        }
        void appendBall(String kind){
            Step s=new Step("ball-approach");s.direction=kind.equals("ball")?1:2;plan.add(s);pose(kind);
        }
        void nextStep(){
            Step next=plan.poll();
            boolean exchange=owner.equals("pair-paw") && socialActive && socialPhase<3;
            if(next==null && sideReady && !exchange){next=new Step("walk");next.bridge=-1;next.direction=facing;}
            if(next==null){
                finishedOwner=owner;completed++;ballHits+=object.hits;object.hits=0;object.kind="";
                owner="";action="idle";step=null;if(!exchange){sideReady=false;releaseClips();}
                restMs=900+random.nextInt(1301);return;
            }
            if(next.name.equals("ball-approach")){
                String strike=next.direction==1?"ball":"ball-pounce";
                int dir=toward(object.x);float gap=standoff(strike);
                float dest=object.x-dir*gap;
                if(Math.abs(safeX(dest)-dest)>1){dir=-dir;dest=object.x-dir*gap;}
                plan.peek().direction=dir;
                Step gait=new Step("run");gait.travel=true;gait.x=safeX(dest);gait.y=ground();next=gait;
            }
            if(next.travel && Math.hypot(next.x-x,next.y-ground())<1){nextStep();return;}
            if(exchange && sideReady && !next.travel && sidePose(next.name) && next.direction!=0 && next.direction!=facing){
                plan.addFirst(next);next=new Step("turn");next.direction=facing;
            }
            if(next.bridge==0 && !next.travel && sidePose(next.name) && !sideReady && !owner.equals("finger")){
                plan.addFirst(next);Step enter=new Step("walk");enter.bridge=1;
                enter.direction=next.direction==0?facing:next.direction;next=enter;
            }else if(next.bridge==0 && !next.travel && !sidePose(next.name) && sideReady &&
                     !next.name.equals("turn") && !next.name.equals("slide") && !next.name.equals("brake") && !next.name.equals("stumble")){
                plan.addFirst(next);next=new Step("walk");next.bridge=-1;next.direction=facing;
            }
            step=next;
            String name=next.name;int token=++revision;loading=true;loadingAction=name;
            String asset=owner.equals("finger") && step.bridge<0 && name.equals("walk")?MotionLibrary.WALK_RECOVERY:name;
            obtain(asset,value->{
                if(closed||revision!=token)return;
                loading=false;loadingAction=null;
                if(value==null){android.util.Log.e("YeopjariCat","Cannot play "+name);cancelStory();restMs=4000;return;}
                previousPose=idle;previousFrame=idleFrame;previousFacing=idleFacing;fade=0;
                clip=value;clock=value.clock();action=name;age=0;startX=x;startY=ground();gaitExit=false;
                if(step.direction!=0)facing=step.direction;
                if(owner.equals("finger")&&!step.travel&&!name.equals("turn")&&step.bridge==0){
                    attackX=fingerX;attackY=fingerY;
                }
                if(step.bridge<0)clock.exitLoop();
                if(step.travel){
                    if(Math.abs(step.x-x)>1)facing=toward(step.x);
                    if(sideReady)clock.beginLoop();
                    speed=sideReady?size*.55f:0;sideReady=false;
                } else if(name.equals("turn")){
                    speed=0;
                } else if(isJump()){
                    boolean finger=owner.equals("finger");
                    landingDy=step.platform?step.y-startY:0;
                    float pad=size*.42f;
                    jumpDx=step.platform?step.x-x:finger?safeX(fingerX-facing*pad)-x:name.equals("cursor-pounce")?safeX(x+facing*size*.75f)-x:0;
                    jumpHeight=clamp(finger?ground()-fingerY-size*.48f:size*(name.equals("jump")?.95f:.70f),size*.16f,Math.max(size*.16f,ground()-size-4));
                    if(finger && value.paws.length>24){
                        // Source frame 24 is an extended forepaw during flight. Solve the arc at that exposure.
                        float phase=(24.5f-15)/25,at=smooth(phase);
                        float goal=fingerX-facing*value.paws[24][0]*size/110;
                        jumpDx=safeX(x+clamp((goal-x)/at,-size*.95f,size*.95f))-x;
                        jumpHeight=clamp((ground()-fingerY-value.paws[24][1]*size/110)/(4*phase*(1-phase)),
                            size*.08f,Math.max(size*.08f,ground()-size-4));
                    }
                } else if(isStop()){
                    stopTravel=safeX(x+facing*size*(action.equals("slide")?.45f:.24f))-x;
                }
                if(name.equals("ball")||name.equals("ball-pounce"))object.beginStroke();
                // Stage the immediately following pose before it owns the renderer.
                Step ahead=plan.peek();
                boolean finger=owner.equals("finger");
                if(ahead!=null && !ahead.name.equals("ball-approach") && !(finger && name.equals("cursor-pounce")))
                    library.preload(coat,ahead.name,null);
                if(finger){
                    if(name.equals("run"))reserve("cursor-pounce");
                    if(name.equals("punch") || name.equals("reach"))reserve(MotionLibrary.WALK_RECOVERY);
                }
                invalidate.run();
            });
        }
        boolean isJump(){return action.equals("jump")||action.equals("cursor-pounce");}
        boolean isStop(){return action.equals("slide")||action.equals("stumble")||action.equals("brake");}
        void endStep(){
            boolean attack=action.equals("cursor-pounce")||action.equals("punch")||action.equals("reach");
            remember();
            if(step.bridge!=0)sideReady=step.bridge>0;
            else if(step.travel)sideReady=step.continuous;
            else sideReady=sidePose(action);
            if(action.equals("turn")){facing=-facing;sideReady=true;}
            if(isJump()){lift=0;landings++;}
            if(action.equals("climb"))lift=0;
            clock=null;clip=null;action="idle";step=null;
            if(owner.equals("finger") && attack &&
                    (fingerDown || Math.hypot(fingerX-attackX,fingerY-attackY)>size*.42f) && fingerFar()){
                plan.clear();planFinger(false);
            }
            if(owner.equals("finger")&&plan.isEmpty()){
                if(fingerDown){planFinger(false);}
            }
            nextStep();
        }
        void walk(double delta){
            if(clock.step()<clip.loopStart||gaitExit){
                clock.advance(delta);if(clock.finished())endStep();return;
            }
            if(owner.equals("finger")){
                float target=fingerStandX(),targetY=fingerStandY();
                if((target-x)*facing< -size*.45f && Math.abs(fingerX-x)>size){
                    // Finish the planted turn before changing run direction.
                    Step replacement=new Step("run");replacement.travel=true;replacement.x=target;replacement.y=targetY;
                    replacement.continuous=true;plan.clear();facePose("turn",facing);plan.add(replacement);
                    sideReady=true;endStep();return;
                }
                step.x=target;step.y=targetY;
            }
            float left=step.x-x,up=step.y-ground(),remaining=(float)Math.hypot(left,up);
            float nominal=clip.stride*size/clip.reference*1000/Math.max(1,clip.loopMs);
            if(owner.equals("stalk"))nominal*=.68f;
            float goal=nominal*(.94f+.14f*(float)Math.sin(clock.loopElapsed()*.0038));
            goal=Math.min(goal,(float)Math.sqrt(2*nominal*2.7*remaining));
            float acceleration=nominal*2.8f*(float)delta/1000;
            speed+=clamp(goal-speed,-acceleration,acceleration);
            float travel=Math.min(remaining,speed*(float)delta/1000);
            if(remaining>0){x+=left*travel/remaining;offsetY+=up*travel/remaining;}
            clock.gait(travel/Math.max(1,clip.stride*size/clip.reference)*clip.loopMs);
            if(remaining-travel<.12f){
                if(owner.equals("finger")&&plan.isEmpty())appendCatch();
                if(step.continuous){sideReady=true;endStep();}else{clock.exitLoop();gaitExit=true;}
            }
        }
        float fingerStandX(){int dir=toward(fingerX);
            return safeX(fingerX-dir*size*.42f);}
        float fingerStandY(){return safeY(clamp(ground(),fingerY+size*.38f,fingerY+size*1.7f));}
        void appendCatch(){
            facePose("cursor-pounce",toward(fingerX));facePose("punch",toward(fingerX));
        }
        void planFinger(boolean initial){
            if(Math.abs(fingerX-x)>size*.9f||Math.abs(fingerStandY()-ground())>size*.35f)
                route("run",fingerStandX(),fingerStandY(),true);
            else if(initial||Math.abs(ground()-fingerY)>size*.78f)appendCatch();
            else facePose(random.nextBoolean()?"reach":"punch",toward(fingerX));
        }
        boolean fingerFar(){return Math.abs(fingerX-x)>size*1.03f||Math.abs(fingerStandY()-ground())>size*.45f;}
        void trackFinger(float px,float py){
            if(action.equals("run") && (px-x)*facing<0)reserve("turn");
            fingerX=px;fingerY=py;
            if(!owner.equals("finger")||!fingerFar()||step==null||step.travel||step.bridge!=0)return;
            boolean preparing=step.name.equals("cursor-pounce")&&(loading||(clock!=null&&clock.step()<clip.loopStart));
            if(!preparing)return;
            // A real swipe begins over the cat. A distant endpoint can arrive after the
            // crouch has started. Keep its planted pose and transfer control to the run.
            remember();revision++;loading=false;loadingAction=null;clock=null;clip=null;step=null;
            plan.clear();sideReady=true;fingerAim=0;
            route("run",fingerStandX(),fingerStandY(),true);nextStep();
        }
        void beginFinger(float px,float py){
            fingerArmed=false;cancelSocial();cancelStory();fingerDown=true;owner="finger";fingerMiss=Float.POSITIVE_INFINITY;
            fingerX=px;fingerY=py;fingerAim=115;reserve("run");reserve("cursor-pounce");invalidate.run();
        }
        void tick(double delta){
            if(controlled)return;
            carryMs=Math.max(0,carryMs-(float)delta);
            object.move(delta,width,size/110f);
            if(clock==null){
                if(!loading&&!owner.isEmpty()){
                    if(owner.equals("finger") && fingerAim>0){fingerAim-=delta;if(fingerAim>0)return;planFinger(true);}
                    nextStep();
                }
                else if(!loading && owner.isEmpty() && !socialActive && !fingerArmed){
                    if(!Float.isNaN(nextGroundMin)){
                        float target=nextGroundMin;nextGroundMin=Float.NaN;
                        if(ground()+1<target){
                            // Finish the current story before walking down from
                            // a height inherited from the previous wallpaper.
                            owner="walk";route("walk",freeDestination(x+(x<width/2?1:-1)*size*.55f),target,false);
                            nextStep();return;
                        }
                    }
                    restMs-=delta;
                    if(restMs<=0&&(queued!=null||automatic)){
                        String next=queued;queued=null;
                        if(next==null)next=random.nextInt(4)==0?"walk":MobileActions.all[random.nextInt(MobileActions.all.length)];
                        start(next);
                    }
                }
                return;
            }
            age+=delta;fade=Math.min(1,fade+(float)delta/165);
            if(fade>=1)previousPose=null;
            if(step.travel){walk(delta);return;}
            clock.advance(delta);
            if(step.bridge>0 && clock.step()>=clip.loopStart){endStep();return;}
            if(isJump()){
                float phase=clamp((float)(clock.loopElapsed()/Math.max(1,clip.loopMs)),0,1);
                // The paw strike follows landing. Its textures can wait for liftoff,
                // leaving the first run and the planted crouch free of speculative work.
                if(lift==0 && phase>0 && phase<1 && owner.equals("finger"))reserve("punch");
                lift=4*jumpHeight*phase*(1-phase);x=startX+jumpDx*smooth(phase);
                if(step.platform)offsetY=startY-floor+landingDy*smooth(phase);
                if(owner.equals("finger") && phase>0 && phase<1){
                    int f=clock.frame();if(f<clip.paws.length){
                        float px=x+facing*clip.paws[f][0]*size/110,py=footY()-clip.paws[f][1]*size/110;
                        fingerMiss=Math.min(fingerMiss,(float)Math.hypot(px-fingerX,py-fingerY));
                    }
                }
            }else if(action.equals("climb")){
                float cap=Math.min(size*1.45f,Math.max(0,ground()-size-8));
                lift=cap*smooth((float)age/4300)*(1-smooth((float)(age-4800)/1300));
            }else if(isStop()){x=startX+stopTravel*smooth((float)age/1450);}
            if(object.kind.equals("ball")&&(action.equals("ball")||action.equals("ball-pounce")))object.contact(this,delta,size/110);
            if(clock.finished())endStep();
        }
        int currentFrame(){return clock==null?idleFrame:clock.frame();}
        void draw(Canvas canvas){
            MotionLibrary.Clip current=clip==null?idle:clip;if(current==null)return;
            float t=previousPose!=null&&clip!=null?smooth(fade):1;
            int dir = clip==null?idleFacing:facing;
            int incoming = Math.round(255*t);
            if(incoming==255){current.paint(canvas,currentFrame(),x,footY(),size,dir,paint);return;}
            if(incoming==0){previousPose.paint(canvas,previousFrame,x,footY(),size,previousFacing,paint);return;}
            // Mix premultiplied source pixels, then composite once onto the
            // wallpaper. Two SRC_OVER passes let the photo show through the fur.
            RectF layerBounds = bounds();
            layerBounds.inset(-2,-2);
            int saved = canvas.saveLayer(layerBounds,null);
            try {
                paint.setAlpha(255-incoming);
                previousPose.paint(canvas,previousFrame,x,footY(),size,previousFacing,paint);
                paint.setAlpha(incoming);
                paint.setXfermode(ADD_POSE);
                current.paint(canvas,currentFrame(),x,footY(),size,dir,paint);
            } finally {
                paint.setXfermode(null);
                paint.setAlpha(255);
                canvas.restoreToCount(saved);
            }
        }
        RectF bounds(){return bounds(false);}
        RectF bounds(boolean whole){
            MotionLibrary.Clip c=clip!=null?clip:idle;
            if(c==null)return new RectF(x-size*.6f,footY()-size,x+size*.6f,footY());
            RectF box=frameBounds(c,currentFrame(),clip==null?idleFacing:facing,whole);
            if(previousPose!=null && fade<1)box.union(frameBounds(previousPose,previousFrame,previousFacing,false));
            return box;
        }
        RectF frameBounds(MotionLibrary.Clip c,int f,int dir,boolean whole){
            float s=size/c.reference,l=(whole?c.left:-c.anchors[f][0])*s,r=(whole?c.right:c.rectangles[f][2]-c.anchors[f][0])*s;
            return new RectF(x+(dir<0?-r:l),footY()+(whole?c.top:-c.anchors[f][1])*s,
                x+(dir<0?-l:r),footY()+(whole?c.bottom:c.rectangles[f][3]-c.anchors[f][1])*s);
        }
    }
    private static final PorterDuffXfermode ADD_POSE = new PorterDuffXfermode(PorterDuff.Mode.ADD);
    final MotionLibrary library;
    final Random random=new Random();
    final Actor[] actors=new Actor[2];
    final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    final Runnable invalidate;
    final PetSettings settings;
    final Context application;
    final String positionMode;
    boolean positioned,automatic=true,closed,socialActive;
    int menuActor=-1,socialPhase,socialLeader;
    long socialCount;
    String socialName="",pendingSocial="";
    double socialTimer=14500;
    float width=400,height=600,size=74,floor=540;
    final RectF menuBounds=new RectF();
    final Paint uiPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
    PetScene(Context context,Runnable invalidate){this(context,invalidate,"preview");}
    PetScene(Context context,Runnable invalidate,String mode){
        this.invalidate=invalidate;application=context.getApplicationContext();positionMode=mode;
        library=MotionLibrary.get(context);settings=new PetSettings(context);size=settings.size();
        actors[0]=new Actor(settings.coat(),width*(settings.companion()?.18f:.28f));
        if(settings.companion())actors[1]=new Actor(settings.companionCoat(),width*.82f);
        for(Actor actor:actors)if(actor!=null)library.preload(actor.coat,"cursor-pounce",null);
    }
    void resize(float w,float h,float bottom){
        if(w<=0||h<=0)return;
        float oldW=width,oldH=height;float oldFloor=floor;
        width=w;height=h;floor=h-bottom;size=Math.min(settings.size(),w/(actors[1]!=null?5.4f:4f));
        for(int i=0;i<actors.length;i++){Actor a=actors[i];if(a==null)continue;
            float sx=w/oldW,sy=h/oldH;
            a.x*=sx;a.startX*=sx;a.offsetY=(oldFloor+a.offsetY)*sy-floor;a.startY*=sy;
            for(Step step:a.plan)if(step.travel||step.platform){step.x=a.safeX(step.x*sx);step.y=a.safeY(step.y*sy);}
            if(a.step!=null&&(a.step.travel||a.step.platform)){a.step.x=a.safeX(a.step.x*sx);a.step.y=a.safeY(a.step.y*sy);}
            a.landingDy*=sy;a.attackX*=sx;a.attackY*=sy;a.fingerX*=sx;a.fingerY*=sy;a.jumpDx*=sx;a.stopTravel*=sx;a.jumpHeight*=sy;
            a.object.x*=sx;a.object.y*=sy;a.object.observed=false;
            if(!positioned){
                String key="position."+positionMode+"."+i;
                float px=settings.prefs.getFloat(key+".x",a.x/w),py=settings.prefs.getFloat(key+".y",floor/h);
                a.x=a.safeX(px*w);a.startX=a.x;a.offsetY=a.safeY(py*h)-floor;
            }
        }
        if(!positioned||w!=oldW||h!=oldH||floor!=oldFloor)fitBackdropGround(!positioned);
        positioned=true;
    }
    void fitBackdropGround(boolean initial){
        PlayBackdrop design=PlayBackdrop.selected(settings);
        float edge=design==PlayBackdrop.PHOTO?Float.NaN:design.farthestWalkableY(width,height,floor);
        for(Actor a:actors)if(a!=null){
            a.nextGroundMin=edge;
            if(initial&&a.ground()<edge){
                a.offsetY=a.safeY(edge)-floor;a.startY=a.ground();
            }
        }
    }
    void social(String name,int leader){if(actors[1]==null)return;pendingSocial=name;socialLeader=leader;
        for(Actor a:actors){a.queued=null;a.fingerArmed=false;}}
    void cancelSocial(){if(socialActive)for(Actor a:actors)if(a!=null)a.cancelStory();socialActive=false;pendingSocial="";socialTimer=30000;}
    void socialTick(double dt){
        Actor a=actors[socialLeader],b=actors[1-socialLeader];
        if(b==null||a==null)return;
        if(!socialActive && pendingSocial.isEmpty() && automatic && !a.fingerArmed&&!b.fingerArmed){
            socialTimer-=dt;if(socialTimer<=0) social(MobileActions.social[random.nextInt(MobileActions.social.length)],random.nextInt(2));
        }
        if(a.busy()||b.busy()||a.controlled||b.controlled||a.fingerArmed||b.fingerArmed)return;
        if(!pendingSocial.isEmpty()&&!socialActive){
            socialActive=true;socialName=pendingSocial;pendingSocial="";socialPhase=0;
        }
        if(!socialActive)return;
        int dir=a.x<=b.x?1:-1;
        float gap=size*(socialName.equals("paw")?70f/110:.88f);
        float center=clamp((a.x+b.x)/2,size*.75f+gap/2,width-size*.75f-gap/2);
        float lane=a.safeY((a.ground()+b.ground())/2);
        a.owner=b.owner="pair-"+socialName;
        if(socialPhase==0){
            boolean exchange=socialName.equals("paw");
            if(exchange){
                for(Actor actor:actors)actor.reserve("walk");
                for(Actor actor:actors){actor.reserve("punch");actor.reserve("reach");}
            }
            a.route("walk",center-dir*gap/2,lane,exchange);b.route("walk",center+dir*gap/2,lane,exchange);
        }else if(socialPhase==1){
            switch(socialName){
                case "perch":{
                    float raised=a.safeY(lane-size*.52f);
                    for(Actor actor:actors){actor.object.place("ledge",actor.x,raised);actor.platformJump(actor.x,raised);
                        actor.pose("look-down");actor.platformJump(actor.x,lane);}break;
                }
                case "race":case "follow":{
                    float move=(center<width/2?1:-1)*size*(socialName.equals("race")?1.8f:1.15f);
                    move=clamp(move,Math.max(a.margin()-a.x,b.margin()-b.x),Math.min(width-a.margin()-a.x,width-b.margin()-b.x));
                    String gait=socialName.equals("race")?"run":"walk";
                    a.route(gait,a.x+move,lane,false);b.route(gait,b.x+move,lane,false);break;
                }
                case "nap":a.facePose("sleep",dir);b.facePose("sleep",-dir);break;
                case "snack":a.facePose("treat",dir);b.facePose("reach",-dir);break;
                case "sniff":a.facePose("sniff",dir);b.facePose("sniff",-dir);break;
                case "surprise":a.facePose("sneeze",dir);b.facePose("startled",-dir);break;
                default:a.facePose("punch",dir);b.facePose("reach",-dir);
            }
        }else if(socialPhase==2&&socialName.equals("paw")){
            a.facePose("reach",dir);b.facePose("punch",-dir);
        }else{
            a.owner=b.owner="";socialActive=false;socialCount++;socialTimer=22000+random.nextInt(24000);
            a.restMs=1100;b.restMs=1600;return;
        }
        socialPhase++;a.nextStep();b.nextStep();
    }
    boolean animating(){if(!settings.enabled())return false;for(Actor a:actors)if(a!=null&&a.busy()&&!a.controlled)return true;return false;}
    void tick(double delta){
        if(closed||!settings.enabled()||delta<=0)return;
        double remaining=Math.min(delta,250);
        while(remaining>.001){double dt=Math.min(16,remaining);socialTick(dt);
            for(Actor a:actors)if(a!=null){
                // An incoming invitation reserves each cat at its next quiet boundary.
                if(!pendingSocial.isEmpty()&&!a.busy()&&!a.controlled)continue;
                a.tick(dt);
            }remaining-=dt;
        }
    }
    void draw(Canvas canvas){
        if(!settings.enabled())return;
        for(Actor a:actors)if(a!=null)a.object.draw(canvas,size/110);
        for(Actor a:actors)if(a!=null)a.draw(canvas);
        if(menuActor>=0){RectF r=menuRect();uiPaint.setColor(0xfffaf7f2);canvas.drawRoundRect(r,16,16,uiPaint);
            uiPaint.setColor(0xff7560bb);uiPaint.setTextSize(15);uiPaint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("종료하기",r.centerX(),r.top+29,uiPaint);canvas.drawText("취소",r.centerX(),r.top+71,uiPaint);}
    }
    void touch(float x,float y){int i=hit(x,y);if(i>=0)actors[i].request("punch");}
    int hit(float x,float y){if(!settings.enabled())return -1;
        for(int i=actors.length-1;i>=0;i--)if(actors[i]!=null){RectF b=actors[i].bounds();b.inset(-8,-8);if(b.contains(x,y))return i;}return -1;}
    int closest(float x,float y){if(actors[1]==null)return 0;return Math.hypot(x-actors[0].x,y-actors[0].footY()+size*.5f)<=Math.hypot(x-actors[1].x,y-actors[1].footY()+size*.5f)?0:1;}
    void drag(int index,float dx,float dy){
        Actor a=actors[index];if(a.busy()){cancelSocial();a.cancelStory();}
        a.nextGroundMin=Float.NaN;a.fingerArmed=false;a.x=a.safeX(a.x+dx);a.offsetY=a.safeY(a.ground()+dy)-floor;a.startX=a.x;
    }
    void savePositions(){if(positionMode.equals("preview"))return;
        android.content.SharedPreferences.Editor editor=settings.prefs.edit();
        for(int i=0;i<actors.length;i++)if(actors[i]!=null){String key="position."+positionMode+"."+i;
            editor.putFloat(key+".x",actors[i].x/width).putFloat(key+".y",actors[i].ground()/height);}editor.apply();}
    void openMenu(int index){menuActor=index;actors[index].controlled=true;}
    void closeMenu(){if(menuActor>=0)actors[menuActor].controlled=false;menuActor=-1;}
    RectF menuRect(){Actor a=actors[menuActor];float l=clamp(a.x-80,8,width-176),t=clamp(a.footY()-size-112,12,height-104);menuBounds.set(l,t,l+168,t+88);return menuBounds;}
    void selectMenu(float x,float y){RectF b=menuRect();boolean exit=b.contains(x,y)&&y<b.top+44;closeMenu();if(exit)PetLifecycle.stop(application);}
    void command(String action){for(Actor a:actors)if(a!=null)a.request(action);}
    void close(){closed=true;}
    String state(){return actors[0].action+(actors[1]==null?"":" / "+actors[1].action);}
}
