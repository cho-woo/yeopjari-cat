package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.View;

final class SceneView extends View {
    PetScene scene;
    Backdrop backdrop;
    PetTouch touch;
    android.app.AlertDialog menuDialog;
    final PetFrameLoop frames=new PetFrameLoop(this::drawFrame);
    boolean running;
    long renderedFrames;
    SceneView(Context context) { super(context);setContentDescription("고양이 미리보기. 고양이를 톡 누르면 앞발로 반응해요.");reload(); }
    void reload() {
        if(touch!=null)touch.cancel();if(menuDialog!=null){menuDialog.dismiss();menuDialog=null;}if(scene!=null)scene.close();if(backdrop!=null)backdrop.close();
        scene=new PetScene(getContext(),this::wake);backdrop=new Backdrop(getContext(),this::invalidate);
        touch=new PetTouch(getContext(),()->scene,index->showMenu(),this::wake);
        touch.previewInput=true;
        if(getWidth()>0)scene.resize(getWidth()/density(),getHeight()/density(),20);
        invalidate();
    }
    float density(){return getResources().getDisplayMetrics().density;}
    void wake(){invalidate();frames.wake();}
    void start(){if(!running){running=true;frames.start();}}
    void stop(){running=false;frames.stop();}
    private boolean drawFrame(double elapsed){scene.tick(elapsed);invalidate();return scene.animating();}
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){scene.resize(w/density(),h/density(),20);}
    @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);canvas.save();canvas.scale(density(),density());backdrop.draw(canvas,getWidth()/density(),getHeight()/density());scene.draw(canvas);canvas.restore();renderedFrames++;}
    void showMenu(){menuDialog=PetMenus.show(getContext(),false,touch);}
    @Override public boolean onTouchEvent(MotionEvent event){boolean handled=touch.event(event.getActionMasked(),event.getPointerCount(),event.getX()/density(),event.getY()/density(),event.getEventTime());if(handled&&event.getActionMasked()==MotionEvent.ACTION_DOWN)getParent().requestDisallowInterceptTouchEvent(true);if(event.getActionMasked()==MotionEvent.ACTION_UP||event.getActionMasked()==MotionEvent.ACTION_CANCEL){getParent().requestDisallowInterceptTouchEvent(false);if(event.getActionMasked()==MotionEvent.ACTION_UP)performClick();}return handled;}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override protected void onDetachedFromWindow(){stop();touch.cancel();if(menuDialog!=null)menuDialog.dismiss();scene.close();backdrop.close();super.onDetachedFromWindow();}
}
