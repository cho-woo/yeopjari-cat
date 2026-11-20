package com.valuewander.yeopjaricat;

import android.view.Choreographer;

/** Advance from display timestamps. An input or an asset becoming ready wakes a resting scene. */
final class PetFrameLoop implements Choreographer.FrameCallback {
    interface Frame {boolean draw(double elapsedMs);}
    private final Choreographer display=Choreographer.getInstance();
    private final Frame frame;
    // The authored frames are at most 30 fps. Do not relayout four launcher windows
    // at 90/120 Hz while repeatedly presenting the same source frame.
    private static final long FRAME_NS=1_000_000_000L/30;
    private long previous,nextFrame;
    private boolean running,resting;

    PetFrameLoop(Frame frame){this.frame=frame;}
    void start(){
        if(running){wake();return;}
        running=true;previous=nextFrame=0;display.postFrameCallback(this);
    }
    void wake(){
        if(!running||!resting)return;
        resting=false;previous=nextFrame=0;display.removeFrameCallback(this);display.postFrameCallback(this);
    }
    void stop(){
        running=false;resting=false;previous=nextFrame=0;display.removeFrameCallback(this);
    }
    @Override public void doFrame(long nanos){
        if(!running)return;
        if(nextFrame!=0 && nanos+1_000_000L<nextFrame){display.postFrameCallback(this);return;}
        nextFrame=nextFrame==0||nanos-nextFrame>=FRAME_NS?nanos+FRAME_NS:nextFrame+FRAME_NS;
        double elapsed=previous==0?0:Math.max(0,nanos-previous)/1_000_000.0;
        previous=nanos;resting=false;
        boolean active=frame.draw(elapsed);
        if(!running)return;
        resting=!active;
        if(resting)display.postFrameCallbackDelayed(this,90);
        else display.postFrameCallback(this);
    }
}
