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
    boolean previewInput;
    PetTouch(Context context,Supplier<PetScene> source,IntConsumer menu,Runnable invalidate){
        this.source=source;this.menu=menu;this.invalidate=invalidate;
        slop=ViewConfiguration.get(context).getScaledTouchSlop()/context.getResources().getDisplayMetrics().density;
    }
    final Runnable hold=()->{if(gesture!=null)gesture.hold(SystemClock.uptimeMillis());};
    boolean event(int action,int pointers,float x,float y,long time){
        PetScene value=source.get();
        if(value.menuActor>=0){if(action==MotionEvent.ACTION_UP){value.selectMenu(x,y);releaseMenu();}return true;}
        if(pointers>1||action==MotionEvent.ACTION_CANCEL){cancel();return true;}
        if(action==MotionEvent.ACTION_DOWN){
            cancel();int index=value.hit(x,y);boolean empty=index<0;
            if(!value.settings.enabled()||(empty&&!previewInput))return false;
            scene=value;selectedActor=empty?value.closest(x,y):index;actor=scene.actors[selectedActor];
            if(empty||actor.fingerArmed){tracking=true;actor.beginFinger(x,y);return true;}
            actor.controlled=true;
            final int selected=selectedActor;
            gesture=new PointerGesture(slop,new PointerGesture.Listener(){
                public void drag(float dx,float dy){scene.drag(selected,dx,dy);invalidate.run();}
                public void tap(){actor.controlled=false;actor.request("punch");}
                public void menu(){menuOpen=true;menu.accept(selected);invalidate.run();}
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
        if(action==MotionEvent.ACTION_UP){handler.removeCallbacks(hold);gesture.up(x,y,time);}
        return true;
    }
    void releaseMenu(){menuOpen=false;if(actor!=null)actor.controlled=false;if(scene!=null)scene.closeMenu();invalidate.run();}
    void cancel(){if(tracking&&actor!=null){actor.fingerDown=false;tracking=false;}handler.removeCallbacks(hold);if(gesture!=null){if(menuOpen)gesture.down=false;else gesture.cancel();}if(!menuOpen&&actor!=null)actor.controlled=false;gesture=null;}
}
