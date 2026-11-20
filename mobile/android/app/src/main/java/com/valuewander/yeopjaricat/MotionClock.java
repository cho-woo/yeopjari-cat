package com.valuewander.yeopjaricat;

/** One uninterrupted authored sequence; touches cannot reset a running clock. */
final class MotionClock {
    final int[] frame;
    final int[] duration;
    final int loopStart, loopEnd;
    private int index;
    private double within;
    private boolean finished;
    private double loopElapsed;

    MotionClock(int[] frame, int[] duration, int loopStart, int loopEnd) {
        if (frame.length == 0 || frame.length != duration.length) throw new IllegalArgumentException("Empty sequence");
        this.frame = frame; this.duration = duration; this.loopStart = loopStart; this.loopEnd = loopEnd;
        for (int value : duration) if (value <= 0) throw new IllegalArgumentException("Invalid exposure");
    }
    int step(){return index;}
    void beginLoop(){if(loopStart<loopEnd){index=loopStart;within=0;finished=false;}}
    void exitLoop(){if(loopEnd<frame.length){index=loopEnd;within=0;}else finished=true;}
    void gait(double ms){
        if(index<loopStart || index>=loopEnd)return;
        while(ms>0){double part=Math.min(ms,duration[index]-within);within+=part;loopElapsed+=part;ms-=part;
            if(within>=duration[index]){within=0;if(++index==loopEnd)index=loopStart;}}
    }
    int frame() { return frame[Math.min(index, frame.length-1)]; }
    boolean finished() { return finished; }
    double loopElapsed() { return loopElapsed; }
    void advance(double milliseconds) {
        if (milliseconds < 0 || !Double.isFinite(milliseconds)) throw new IllegalArgumentException("Invalid delta");
        while (milliseconds > 0 && !finished) {
            double consumed = Math.min(milliseconds, duration[index] - within);
            if (index >= loopStart && index < loopEnd) loopElapsed += consumed;
            within += consumed; milliseconds -= consumed;
            if (within >= duration[index]) {
                within = 0;
                if (index == frame.length-1) finished = true;
                else index++;
            }
        }
    }
}
