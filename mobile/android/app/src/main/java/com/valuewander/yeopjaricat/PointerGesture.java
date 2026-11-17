package com.valuewander.yeopjaricat;

/** A drag owns the press; a long press can never become a tap on release. */
final class PointerGesture {
    interface Listener { void drag(float dx,float dy);void tap();void menu();void release(boolean dragged); }
    final Listener listener;
    final float slop;
    boolean down,dragged,longPressed;
    float originX,originY,lastX,lastY;
    long started;
    PointerGesture(float slop,Listener listener){this.slop=slop;this.listener=listener;}
    void down(float x,float y,long time){this.down=true;dragged=false;longPressed=false;originX=lastX=x;originY=lastY=y;started=time;}
    void move(float x,float y){
        if(!down||longPressed)return;
        if(!dragged&&Math.hypot(x-originX,y-originY)>slop)dragged=true;
        if(dragged)listener.drag(x-lastX,y-lastY);
        if(dragged){lastX=x;lastY=y;}
    }
    void hold(long now){if(down&&!dragged&&!longPressed&&now-started>=550){longPressed=true;listener.menu();}}
    void up(long now){if(!down)return;hold(now);down=false;if(!dragged&&!longPressed)listener.tap();listener.release(dragged);}
    void up(float x,float y,long now){move(x,y);up(now);}
    void cancel(){if(down){down=false;listener.release(dragged);}dragged=false;longPressed=false;}
}
