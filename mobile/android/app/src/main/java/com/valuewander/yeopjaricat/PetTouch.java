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
    private static final long SELECTION_MS=6000,COMMAND_WINDOW_MS=1000;
    private int selectedHomeCat=-1;
    private boolean validTap;
    private long selectedUntil,emptyTapUp=-1,launcherCommand=-1;
    private float commandX,commandY;

    int selectedHomeCat(long now){return now<=selectedUntil?selectedHomeCat:-1;}
    private void clearHomeSelection(){
        validTap=false;selectedHomeCat=-1;emptyTapUp=-1;launcherCommand=-1;
    }
    // A wallpaper also hears raw taps on icons and widgets. Only the launcher
    // can confirm an unoccupied cell. Its command may arrive before or after UP.
    void launcherEmptyTap(float x,float y,long now){
        if(selectedHomeCat(now)<0)return;
        launcherCommand=now;commandX=x;commandY=y;finishHomeCall(now);
    }
    private void finishHomeCall(long now){
        int selected=selectedHomeCat(now);
        if(selected<0 || emptyTapUp<0 || launcherCommand<0 ||
                Math.abs(emptyTapUp-launcherCommand)>COMMAND_WINDOW_MS ||
                Math.hypot(tapX-commandX,tapY-commandY)>slop*2)return;
        PetScene value=source.get();
        clearHomeSelection();
        if(value.settings.enabled() && value.actors[selected]!=null){
            value.actors[selected].goToHomeTap(tapX,tapY);invalidate.run();
        }
    }

    private boolean launcherTap(PetScene value,int action,int pointers,float x,float y,long time){
        if(action==MotionEvent.ACTION_DOWN){
            validTap=value.settings.enabled() && pointers==1;
            tapTarget=value.hit(x,y);tapX=x;tapY=y;tapDown=time;emptyTapUp=-1;
        }else if(action==MotionEvent.ACTION_CANCEL || action==MotionEvent.ACTION_POINTER_DOWN || pointers!=1 ||
                Math.hypot(x-tapX,y-tapY)>slop || time-tapDown>=ViewConfiguration.getLongPressTimeout()){
            clearHomeSelection();
        }else if(validTap && action==MotionEvent.ACTION_UP){
            validTap=false;
            if(tapTarget>=0){
                selectedHomeCat=tapTarget;selectedUntil=time+SELECTION_MS;
                value.actors[tapTarget].tap(time);invalidate.run();
            }else if(value.hit(x,y)<0 && selectedHomeCat(time)>=0){
                tapX=x;tapY=y;emptyTapUp=SystemClock.uptimeMillis();finishHomeCall(emptyTapUp);
            }
        }
        return validTap;
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
    void cancel(){clearHomeSelection();tapTarget=-1;if(tracking&&actor!=null){actor.fingerDown=false;tracking=false;}handler.removeCallbacks(hold);if(gesture!=null){if(menuOpen)gesture.down=false;else gesture.cancel();}if(!menuOpen&&actor!=null)actor.controlled=false;gesture=null;}
}
