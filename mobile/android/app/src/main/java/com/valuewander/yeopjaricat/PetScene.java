package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import java.util.Random;

/** Shared wallpaper/preview/overlay scene. All coordinates are density-independent. */
final class PetScene {
    final class Actor {
        final int coat;
        MotionLibrary.Clip idle, clip;
        MotionClock clock;
        String action="idle", queued, loadingAction;
        float x, startX;
        int facing=1, walkDirection=1;
        double restMs;
        boolean loading;
        Actor(int coat,float x) {
            this.coat=coat;this.x=x;restMs=8000+random.nextInt(8000);
            library.request(coat,"settle",value->{if(!closed){idle=value;invalidate.run();}});
        }
        boolean busy(){return loading||clock!=null;}
        void request(String next) {
            if(busy()){queued=next;return;}
            if(next.equals("walk")&&!planWalk())next="groom";
            String wanted=next;loading=true;loadingAction=wanted;
            library.request(coat,wanted,value->{
                if(closed)return;
                loading=false;loadingAction=null;if(value==null){restMs=5000;return;}
                clip=value;clock=value.clock();action=wanted;startX=x;
                if(action.equals("walk"))facing=walkDirection;
                invalidate.run();
            });
        }
        boolean planWalk() {
            float distance=212f*size/96f, margin=size*.78f;
            boolean right=x+distance<=width-margin,left=x-distance>=margin;
            if(actors[1]!=null) {
                Actor other=this==actors[0]?actors[1]:actors[0];
                right &= !(other.x>x && other.x<x+distance+size*1.2f);
                left &= !(other.x<x && other.x>x-distance-size*1.2f);
                if(other.action.equals("walk")||"walk".equals(other.loadingAction)){
                    float end=(other.loading?other.x:other.startX)+other.walkDirection*distance;
                    right &= !(end>x-size*1.2f&&end<x+distance+size*1.2f);
                    left &= !(end<x+size*1.2f&&end>x-distance-size*1.2f);
                }
            }
            if(!right&&!left)return false;
            walkDirection=right&&left?(random.nextBoolean()?1:-1):(right?1:-1);return true;
        }
        void tick(double delta) {
            if(clock!=null) {
                clock.advance(delta);
                if(action.equals("walk")) x=startX+walkDirection*clip.stride*size/clip.reference*(float)(clock.loopElapsed()/Math.max(1,clip.loopMs));
                if(clock.finished()) {
                    // Keep the actual authored end pose while resting. A new action owns its entire source clock.
                    idle=clip;clock=null;clip=null;action="idle";
                    restMs=45000+random.nextInt(55001);
                    if(queued!=null){restMs=1400;}
                }
            } else if(!loading) {
                restMs-=delta;
                if(restMs<=0) {
                    String next=queued;queued=null;
                    if(next==null)next=new String[]{"walk","groom","knead","settle"}[random.nextInt(4)];
                    request(next);
                }
            }
        }
        void draw(Canvas canvas) {
            MotionLibrary.Clip paintClip=clip!=null?clip:idle;if(paintClip==null)return;
            int frame=clock!=null?clock.frame():paintClip.frames[paintClip.frames.length-1];
            paintClip.paint(canvas,frame,x,floor,size,facing,paint);
        }
    }
    final MotionLibrary library;
    final Random random=new Random();
    final Actor[] actors=new Actor[2];
    final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    final Runnable invalidate;
    final PetSettings settings;
    float width=400,height=600,size=74,floor=540;
    private boolean closed;
    PetScene(Context context,Runnable invalidate) {
        this.invalidate=invalidate;library=MotionLibrary.get(context);settings=new PetSettings(context);size=settings.size();
        actors[0]=new Actor(settings.coat(),width*(settings.companion()?.18f:.28f));
        if(settings.companion())actors[1]=new Actor(settings.companionCoat(),width*.82f);
    }
    void resize(float width,float height,float bottom) {
        if(width<=0||height<=0)return;
        float previous=this.width;this.width=width;this.height=height;floor=height-bottom;size=Math.min(settings.size(),width/(settings.companion()?5.4f:4f));
        for(Actor a:actors)if(a!=null) {
            // Relocation belongs to a viewport change, never to an action transition.
            float change=a.x*(width/previous)-a.x;a.x+=change;a.startX+=change;
        }
    }
    boolean animating(){for(Actor a:actors)if(a!=null&&a.busy())return true;return false;}
    void tick(double delta) { if(!closed)for(Actor a:actors)if(a!=null)a.tick(Math.min(a.busy()?250:1000,delta)); }
    void draw(Canvas canvas) {
        for(Actor a:actors)if(a!=null)a.draw(canvas);
    }
    void touch(float x,float y) {
        Actor nearest=actors[0];
        for(Actor a:actors)if(a!=null&&Math.abs(a.x-x)<Math.abs(nearest.x-x))nearest=a;
        if(Math.abs(x-nearest.x)<size&&Math.abs(y-floor+size*.5f)<size)nearest.request("punch");
        // A launcher only forwards touches it chooses to forward. Never observe other apps' input.
    }
    void command(String action) { for(Actor a:actors)if(a!=null)a.request(action); }
    void close(){closed=true;}
    String state(){return actors[0].action+(actors[1]==null?"":" / "+actors[1].action);}
}
