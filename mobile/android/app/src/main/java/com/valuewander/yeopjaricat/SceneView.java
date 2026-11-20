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
    final boolean homePlay;
    SceneView(Context context){this(context,false);}
    SceneView(Context context,boolean homePlay) { super(context);this.homePlay=homePlay;setContentDescription("고양이 미리보기. 고양이를 톡 누르면 반응해요. 연달아 건드리면 귀를 젖히고 하악질해요.");reload(); }
    void reload() {
        if(touch!=null)touch.cancel();if(menuDialog!=null){menuDialog.dismiss();menuDialog=null;}if(scene!=null&&!homePlay)scene.close();if(backdrop!=null)backdrop.close();
        scene=homePlay?HomeSceneStore.get(getContext()):new PetScene(getContext(),this::wake);android.util.DisplayMetrics metrics=getResources().getDisplayMetrics();
        backdrop=new Backdrop(getContext(),this::invalidate,homePlay&&metrics.widthPixels<metrics.heightPixels*.74f);
        touch=new PetTouch(getContext(),()->scene,index->showMenu(),this::wake);
        touch.followEmptySpace=true;
        resize();
        invalidate();
    }
    float density(){return getResources().getDisplayMetrics().density;}
    void wake(){invalidate();frames.wake();}
    void start(){if(!running){running=true;frames.start();}}
    void stop(){running=false;frames.stop();}
    private boolean drawFrame(double elapsed){scene.tick(elapsed);invalidate();return scene.animating();}
    void resize(){
        if(getWidth()<=0||getHeight()<=0)return;
        float w=getWidth()/density(),h=getHeight()/density();
        backdrop.viewport(w,h);
        scene.resize(w,h,homePlay?scene.settings.homeBottom(h):20);
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){resize();}
    @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);canvas.save();canvas.scale(density(),density());backdrop.draw(canvas,getWidth()/density(),getHeight()/density(),scene.floor);scene.draw(canvas);canvas.restore();renderedFrames++;}
    void showMenu(){menuDialog=PetMenus.show(getContext(),false,touch);}
    @SuppressWarnings("deprecation")
    boolean systemGestureStart(MotionEvent event){
        if(!homePlay || event.getActionMasked()!=MotionEvent.ACTION_DOWN || android.os.Build.VERSION.SDK_INT<29)return false;
        android.view.WindowInsets insets=getRootWindowInsets();if(insets==null)return false;
        android.graphics.Insets edges=insets.getSystemGestureInsets();
        return event.getX()<edges.left || event.getX()>=getWidth()-edges.right ||
            event.getY()>=getHeight()-edges.bottom;
    }
    @Override public boolean onTouchEvent(MotionEvent event){
        if(systemGestureStart(event))return false;
        boolean handled=touch.event(event.getActionMasked(),event.getPointerCount(),event.getX()/density(),event.getY()/density(),event.getEventTime());if(handled&&event.getActionMasked()==MotionEvent.ACTION_DOWN)getParent().requestDisallowInterceptTouchEvent(true);if(event.getActionMasked()==MotionEvent.ACTION_UP||event.getActionMasked()==MotionEvent.ACTION_CANCEL){getParent().requestDisallowInterceptTouchEvent(false);if(event.getActionMasked()==MotionEvent.ACTION_UP)performClick();}return handled;}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override protected void onDetachedFromWindow(){stop();touch.cancel();if(menuDialog!=null)menuDialog.dismiss();if(!homePlay)scene.close();backdrop.close();super.onDetachedFromWindow();}
}
