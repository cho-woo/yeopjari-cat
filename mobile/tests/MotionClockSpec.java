package com.valuewander.yeopjaricat;

public final class MotionClockSpec {
    public static void main(String[] args) {
        int cases=0;
        for(int total:new int[]{42,73,107,158,229})for(int step:new int[]{1,16,33,64,1500}){
            int[] frame=new int[total],duration=new int[total];
            int ms=0;for(int i=0;i<total;i++){frame[i]=i;duration[i]=(int)Math.round((i+1)*1000.0/24)-(int)Math.round(i*1000.0/24);ms+=duration[i];}
            int loopStart=total==229?36:0,loopEnd=total==229?193:0;
            MotionClock clock=new MotionClock(frame,duration,loopStart,loopEnd);
            int previous=-1;double elapsed=0;
            while(!clock.finished()){
                if(clock.frame()<previous)throw new AssertionError("Source clock rewound");
                previous=clock.frame();clock.advance(step);elapsed+=step;
                if(elapsed>ms+step)throw new AssertionError("Recovery never ended");
            }
            if(clock.frame()!=total-1)throw new AssertionError("Recovery discarded");
            double completed=clock.loopElapsed();clock.advance(100000);
            if(clock.frame()!=total-1||clock.loopElapsed()!=completed)throw new AssertionError("Finished clock moved");
            if(total==229){int expected=0;for(int i=36;i<193;i++)expected+=duration[i];if(Math.abs(completed-expected)>.001)throw new AssertionError("Walk travel inconsistent");}
            cases++;
        }
        System.out.println("Motion clock: "+cases+" timing and complete-recovery cases passed");
    }
}
