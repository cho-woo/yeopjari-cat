package com.valuewander.yeopjaricat;

import android.app.KeyguardManager;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.service.wallpaper.WallpaperService;
import android.view.MotionEvent;
import android.view.SurfaceHolder;

public final class CatWallpaperService extends WallpaperService {
    static volatile long renderedFrames;
    static volatile boolean wallpaperVisible;
    @Override public Engine onCreateEngine(){return new CatEngine();}
    final class CatEngine extends Engine implements SharedPreferences.OnSharedPreferenceChangeListener {
        final Handler handler=new Handler(Looper.getMainLooper());
        PetScene scene;
        Backdrop backdrop;
        boolean visible,surfaceReady,closed;
        long previous;
        final float density=getResources().getDisplayMetrics().density;
        final KeyguardManager keyguard=(KeyguardManager)getSystemService(KEYGUARD_SERVICE);
        final PetSettings settings=new PetSettings(CatWallpaperService.this);
        CatEngine(){setTouchEventsEnabled(true);settings.prefs.registerOnSharedPreferenceChangeListener(this);reload();}
        void reload(){
            if(scene!=null)scene.close();if(backdrop!=null)backdrop.close();
            scene=new PetScene(CatWallpaperService.this,this::schedule);
            backdrop=new Backdrop(CatWallpaperService.this,this::schedule);
            if(surfaceReady){android.graphics.Rect r=getSurfaceHolder().getSurfaceFrame();scene.resize(r.width()/density,r.height()/density,groundBottom(r.height()/density));}
        }
        void schedule(){if(!closed&&visible&&surfaceReady){handler.removeCallbacks(frame);handler.post(frame);}}
        final Runnable frame=new Runnable(){public void run(){
            if(closed||!visible||!surfaceReady)return;
            long now=SystemClock.uptimeMillis();double delta=previous==0?0:Math.min(250,now-previous);previous=now;
            boolean paintCat=!CatOverlayService.running && (isPreview()||!keyguard.isKeyguardLocked());
            if(paintCat)scene.tick(delta);
            Canvas canvas=null;
            try {
                canvas=getSurfaceHolder().lockHardwareCanvas();
                if(canvas!=null){canvas.save();canvas.scale(density,density);backdrop.draw(canvas,canvas.getWidth()/density,canvas.getHeight()/density);if(paintCat)scene.draw(canvas);canvas.restore();renderedFrames++;}
            } catch(IllegalArgumentException|IllegalStateException ignored){ /* Surface lifetime is owned by Android. */ }
            finally{if(canvas!=null)getSurfaceHolder().unlockCanvasAndPost(canvas);}
            handler.postDelayed(this,paintCat&&scene.animating()?33:250);
        }};
        @Override public void onVisibilityChanged(boolean value){visible=value;if(!isPreview())wallpaperVisible=value;previous=0;handler.removeCallbacks(frame);schedule();}
        @Override public void onSurfaceCreated(SurfaceHolder holder){super.onSurfaceCreated(holder);surfaceReady=true;schedule();}
        float groundBottom(float height){int percent=Math.max(30,Math.min(88,settings.prefs.getInt("home_floor_percent",58)));return height*(1-percent/100f);}
        @Override public void onSurfaceChanged(SurfaceHolder holder,int format,int w,int h){super.onSurfaceChanged(holder,format,w,h);scene.resize(w/density,h/density,groundBottom(h/density));schedule();}
        @Override public void onSurfaceDestroyed(SurfaceHolder holder){surfaceReady=false;handler.removeCallbacks(frame);super.onSurfaceDestroyed(holder);}
        @Override public void onTouchEvent(MotionEvent event){if(event.getAction()==MotionEvent.ACTION_UP)scene.touch(event.getX()/density,event.getY()/density);super.onTouchEvent(event);}
        @Override public void onSharedPreferenceChanged(SharedPreferences prefs,String key){if(!"overlay".equals(key)){reload();schedule();}}
        @Override public void onDestroy(){closed=true;if(!isPreview())wallpaperVisible=false;handler.removeCallbacksAndMessages(null);settings.prefs.unregisterOnSharedPreferenceChangeListener(this);scene.close();backdrop.close();super.onDestroy();}
    }
}
