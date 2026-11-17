package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Canvas;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

final class SceneView extends View {
    PetScene scene;
    Backdrop backdrop;
    long previous;
    boolean running;
    long renderedFrames;
    SceneView(Context context) { super(context);setContentDescription("고양이 미리보기. 고양이를 톡 누르면 앞발로 반응해요.");reload(); }
    void reload() {
        if(scene!=null)scene.close();if(backdrop!=null)backdrop.close();
        scene=new PetScene(getContext(),this::invalidate);backdrop=new Backdrop(getContext(),this::invalidate);
        if(getWidth()>0)scene.resize(getWidth()/density(),getHeight()/density(),20);
        invalidate();
    }
    float density(){return getResources().getDisplayMetrics().density;}
    void start(){if(running)return;running=true;previous=SystemClock.uptimeMillis();postOnAnimation(frame);}
    void stop(){running=false;removeCallbacks(frame);}
    private final Runnable frame=new Runnable(){public void run(){
        if(!running)return;long now=SystemClock.uptimeMillis();scene.tick(now-previous);previous=now;invalidate();postDelayed(this,scene.animating()?33:250);
    }};
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){scene.resize(w/density(),h/density(),20);}
    @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);canvas.save();canvas.scale(density(),density());backdrop.draw(canvas,getWidth()/density(),getHeight()/density());scene.draw(canvas);canvas.restore();renderedFrames++;}
    @Override public boolean onTouchEvent(MotionEvent event){if(event.getAction()==MotionEvent.ACTION_UP){scene.touch(event.getX()/density(),event.getY()/density());performClick();}return true;}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override protected void onDetachedFromWindow(){stop();scene.close();backdrop.close();super.onDetachedFromWindow();}
}
