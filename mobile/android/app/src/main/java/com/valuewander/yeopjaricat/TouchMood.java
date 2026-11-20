package com.valuewander.yeopjaricat;

/** Each cat remembers only recent, completed pokes. Times use the input event's monotonic clock. */
final class TouchMood {
    static final int GENTLE=1, WARNING=2, ANGRY=3;
    private final long[] taps=new long[6];
    private int count;
    private long last=-1, quietUntil=-1;

    int tap(long now){
        if(last>=0 && now-last<45)return 0;
        last=now;
        if(now<quietUntil){
            quietUntil=now+5500;
            return 0;
        }
        int kept=0;
        for(int i=0;i<count;i++)if(now-taps[i]<=3000)taps[kept++]=taps[i];
        taps[kept++]=now;
        count=kept;
        if(count==6){
            count=0;quietUntil=now+5500;
            return ANGRY;
        }
        if(count>=3)return WARNING;
        return GENTLE;
    }

    // The opening glance and the hiss share the same consecutive source frames.
    // An isolated warning retraces the glance; another poke can continue it forward.
    static MotionClock warning(MotionClock full,int lastGlance){
        int[] frames=new int[lastGlance*2+1],times=new int[frames.length];
        for(int i=0;i<frames.length;i++){
            int index=i<=lastGlance?i:2*lastGlance-i;
            frames[i]=full.frame[index];times[i]=full.duration[index];
        }
        return new MotionClock(frames,times,frames.length,frames.length);
    }
}
