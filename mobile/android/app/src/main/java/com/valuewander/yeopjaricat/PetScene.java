package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import java.util.Random;

/** Shared wallpaper/preview/overlay scene. All coordinates are density-independent. */
final class PetScene {
    final class Actor {
        final int coat;
        MotionLibrary.Clip idle, clip;
        MotionClock clock;
        String action="idle", queued, loadingAction;
        float x, startX,offsetY;
        boolean controlled;
        int facing=1, walkDirection=1;
        double restMs;
        boolean loading;
        Actor(int coat,float x) {
            this.coat=coat;this.x=x;restMs=600+random.nextInt(600);
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
            if(actors[1]!=null&&Math.abs(footY()-((this==actors[0]?actors[1]:actors[0]).footY()))<size) {
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
            if(controlled)return;
            if(clock!=null) {
                clock.advance(delta);
                if(action.equals("walk")) x=startX+walkDirection*clip.stride*size/clip.reference*(float)(clock.loopElapsed()/Math.max(1,clip.loopMs));
                if(clock.finished()) {
                    // Keep the actual authored end pose while resting. A new action owns its entire source clock.
                    idle=clip;clock=null;clip=null;action="idle";
                    restMs=400+random.nextInt(601);
                }
            } else if(!loading) {
                restMs-=delta;
                if(restMs<=0&&(automatic||queued!=null)) {
                    String next=queued;queued=null;
                    if(next==null)next=new String[]{"walk","walk","walk","punch","groom","knead"}[random.nextInt(6)];
                    request(next);
                }
            }
        }
        void draw(Canvas canvas) {
            MotionLibrary.Clip paintClip=clip!=null?clip:idle;if(paintClip==null)return;
            int frame=clock!=null?clock.frame():paintClip.frames[paintClip.frames.length-1];
            paintClip.paint(canvas,frame,x,footY(),size,facing,paint);
        }
        float footY(){return floor+offsetY;}
        RectF bounds(){return bounds(false);}
        RectF bounds(boolean whole){
            MotionLibrary.Clip c=clip!=null?clip:idle;
            if(c==null)return new RectF(x-size*.6f,footY()-size,x+size*.6f,footY());
            int f=clock!=null?clock.frame():c.frames[c.frames.length-1];float s=size/c.reference;
            float left=-c.anchors[f][0]*s,right=(c.rectangles[f][2]-c.anchors[f][0])*s;
            if(whole){left=c.left*s;right=c.right*s;}
            if(facing<0){float old=left;left=-right;right=-old;}
            return new RectF(x+left,footY()+(whole?c.top*s:-c.anchors[f][1]*s),x+right,footY()+(whole?c.bottom*s:(c.rectangles[f][3]-c.anchors[f][1])*s));
        }
    }
    final MotionLibrary library;
    final Random random=new Random();
    final Actor[] actors=new Actor[2];
    final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    final Runnable invalidate;
    final PetSettings settings;
    final Context application;
    final String positionMode;
    boolean positioned;
    boolean automatic=true;
    int menuActor=-1;
    final RectF menuBounds=new RectF();
    final Paint uiPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
    float width=400,height=600,size=74,floor=540;
    private boolean closed;
    PetScene(Context context,Runnable invalidate) {
        this(context,invalidate,"preview");
    }
    PetScene(Context context,Runnable invalidate,String mode) {
        this.invalidate=invalidate;application=context.getApplicationContext();positionMode=mode;library=MotionLibrary.get(context);settings=new PetSettings(context);size=settings.size();
        actors[0]=new Actor(settings.coat(),width*(settings.companion()?.18f:.28f));
        if(settings.companion())actors[1]=new Actor(settings.companionCoat(),width*.82f);
    }
    void resize(float width,float height,float bottom) {
        if(width<=0||height<=0)return;
        float previous=this.width,oldHeight=this.height,oldFloor=floor;this.width=width;this.height=height;floor=height-bottom;size=Math.min(settings.size(),width/(settings.companion()?5.4f:4f));
        for(int i=0;i<actors.length;i++){Actor a=actors[i];if(a==null)continue;
            // Relocation belongs to a viewport change, never to an action transition.
            float change=a.x*(width/previous)-a.x;a.x+=change;a.startX+=change;
            a.offsetY=(oldFloor+a.offsetY)*height/oldHeight-floor;
            if(!positioned&&!positionMode.equals("preview")){
                String key="position."+positionMode+"."+i;
                float savedX=settings.prefs.getFloat(key+".x",a.x/width),savedY=settings.prefs.getFloat(key+".y",floor/height);
                a.x=Math.max(size*.65f,Math.min(width-size*.65f,savedX*width));a.startX=a.x;
                a.offsetY=Math.max(size+12,Math.min(height-24,savedY*height))-floor;
            }
        }
        positioned=true;
    }
    boolean animating(){if(!settings.enabled())return false;for(Actor a:actors)if(a!=null&&a.busy()&&!a.controlled)return true;return false;}
    void tick(double delta) { if(!closed&&settings.enabled())for(Actor a:actors)if(a!=null)a.tick(Math.min(a.busy()?250:1000,delta)); }
    void draw(Canvas canvas) {
        if(!settings.enabled())return;
        for(Actor a:actors)if(a!=null)a.draw(canvas);
        if(menuActor>=0){RectF r=menuRect();uiPaint.setColor(0xfffaf7f2);canvas.drawRoundRect(r,16,16,uiPaint);uiPaint.setColor(0xff7560bb);uiPaint.setTextSize(15);uiPaint.setTextAlign(Paint.Align.CENTER);canvas.drawText("종료하기",r.centerX(),r.top+29,uiPaint);uiPaint.setColor(0xff756e80);canvas.drawText("취소",r.centerX(),r.top+71,uiPaint);}
    }
    void touch(float x,float y) {
        int selected=hit(x,y);if(selected>=0)actors[selected].request("punch");
        // A launcher only forwards touches it chooses to forward. Never observe other apps' input.
    }
    int hit(float x,float y){if(!settings.enabled())return -1;for(int i=actors.length-1;i>=0;i--)if(actors[i]!=null){RectF b=actors[i].bounds();b.inset(-8,-8);if(b.contains(x,y))return i;}return -1;}
    void drag(int index,float dx,float dy){Actor a=actors[index];RectF b=a.bounds(true);float moveX=Math.max(Math.min(0,8-b.left),Math.min(Math.max(0,width-8-b.right),dx));float moveY=Math.max(Math.min(0,28-b.top),Math.min(Math.max(0,height-28-b.bottom),dy));a.x+=moveX;a.startX+=moveX;a.offsetY+=moveY;}
    void savePositions(){if(positionMode.equals("preview"))return;android.content.SharedPreferences.Editor editor=settings.prefs.edit();for(int i=0;i<actors.length;i++)if(actors[i]!=null){String key="position."+positionMode+"."+i;editor.putFloat(key+".x",actors[i].x/width).putFloat(key+".y",actors[i].footY()/height);}editor.apply();}
    void openMenu(int index){menuActor=index;actors[index].controlled=true;}
    void closeMenu(){if(menuActor>=0&&actors[menuActor]!=null)actors[menuActor].controlled=false;menuActor=-1;}
    RectF menuRect(){Actor a=actors[menuActor];float left=Math.max(8,Math.min(width-176,a.x-80)),top=Math.max(12,Math.min(height-104,a.footY()-size-112));menuBounds.set(left,top,left+168,top+88);return menuBounds;}
    void selectMenu(float x,float y){RectF b=menuRect();boolean exit=b.contains(x,y)&&y<b.top+44;closeMenu();if(exit)PetLifecycle.stop(application);}
    void command(String action) { for(Actor a:actors)if(a!=null)a.request(action); }
    void close(){closed=true;}
    String state(){return actors[0].action+(actors[1]==null?"":" / "+actors[1].action);}
}
