package com.valuewander.yeopjaricat;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import java.util.function.IntConsumer;
import java.util.function.Supplier;

final class PetTouch {
    final Handler handler=new Handler(Looper.getMainLooper());
    final Supplier<PetScene> source;
    final IntConsumer menu;
    final Runnable invalidate;
    final float slop;
    PetScene scene;
    PetScene.Actor actor;
    PointerGesture gesture;
    boolean menuOpen,tracking;
    int selectedActor;
    long releaseTime;
    boolean followEmptySpace,launcherTapsOnly;
    private int tapTarget=-1;
    private float tapX,tapY;
    private long tapDown;

    private boolean launcherTap(PetScene value,int action,int pointers,float x,float y,long time){
        if(action==MotionEvent.ACTION_DOWN){
            tapTarget=value.hit(x,y);tapX=x;tapY=y;tapDown=time;
        }else if(action==MotionEvent.ACTION_CANCEL || pointers!=1 ||
                Math.hypot(x-tapX,y-tapY)>slop || time-tapDown>=ViewConfiguration.getLongPressTimeout()){
            tapTarget=-1;
        }else if(action==MotionEvent.ACTION_UP){
            int target=tapTarget;tapTarget=-1;
            if(target>=0 && value.hit(x,y)==target){value.actors[target].tap(time);invalidate.run();}
        }
        return tapTarget>=0;
    }
    PetTouch(Context context,Supplier<PetScene> source,IntConsumer menu,Runnable invalidate){
        this.source=source;this.menu=menu;this.invalidate=invalidate;
        slop=ViewConfiguration.get(context).getScaledTouchSlop()/context.getResources().getDisplayMetrics().density;
    }
    final Runnable hold=()->{if(gesture!=null)gesture.hold(SystemClock.uptimeMillis());};
    boolean event(int action,int pointers,float x,float y,long time){
        PetScene value=source.get();
        if(launcherTapsOnly)return launcherTap(value,action,pointers,x,y,time);
        if(value.menuActor>=0){if(action==MotionEvent.ACTION_UP){value.selectMenu(x,y);releaseMenu();}return true;}
        if(pointers>1||action==MotionEvent.ACTION_CANCEL){cancel();return true;}
        if(action==MotionEvent.ACTION_DOWN){
            cancel();int index=value.hit(x,y);boolean empty=index<0;
            if(!value.settings.enabled()||(empty&&!followEmptySpace))return false;
            scene=value;selectedActor=empty?value.closest(x,y):index;actor=scene.actors[selectedActor];
            if(empty||actor.fingerArmed){tracking=true;actor.beginFinger(x,y);return true;}
            final int selected=selectedActor;
            gesture=new PointerGesture(slop,new PointerGesture.Listener(){
                public void drag(float dx,float dy){actor.controlled=true;scene.drag(selected,dx,dy);invalidate.run();}
                public void tap(){actor.tap(releaseTime);invalidate.run();}
                public void menu(){actor.controlled=true;menuOpen=true;menu.accept(selected);invalidate.run();}
                public void release(boolean dragged){if(!menuOpen){actor.controlled=false;if(dragged)scene.savePositions();}}
            });
            gesture.down(x,y,time);handler.postDelayed(hold,550);return true;
        }
        if(tracking){
            actor.trackFinger(x,y);
            if(action==MotionEvent.ACTION_UP){actor.fingerDown=false;tracking=false;}
            return true;
        }
        if(gesture==null)return false;
        if(action==MotionEvent.ACTION_MOVE)gesture.move(x,y);
        if(action==MotionEvent.ACTION_UP){releaseTime=time;handler.removeCallbacks(hold);gesture.up(x,y,time);}
        return true;
    }
    void releaseMenu(){menuOpen=false;if(actor!=null)actor.controlled=false;if(scene!=null)scene.closeMenu();invalidate.run();}
    void cancel(){tapTarget=-1;if(tracking&&actor!=null){actor.fingerDown=false;tracking=false;}handler.removeCallbacks(hold);if(gesture!=null){if(menuOpen)gesture.down=false;else gesture.cancel();}if(!menuOpen&&actor!=null)actor.controlled=false;gesture=null;}
}
