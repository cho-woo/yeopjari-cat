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
