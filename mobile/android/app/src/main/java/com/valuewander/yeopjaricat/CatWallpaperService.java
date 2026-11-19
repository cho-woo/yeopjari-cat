package com.valuewander.yeopjaricat;

import android.app.KeyguardManager;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.service.wallpaper.WallpaperService;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;

public final class CatWallpaperService extends WallpaperService {
    static volatile long renderedFrames;
    static volatile boolean wallpaperVisible;
    final ArrayList<CatEngine> engines=new ArrayList<>();
    void updateHomeVisibility(){boolean shown=false;for(CatEngine e:engines)if(!e.isPreview()&&e.visible)shown=true;wallpaperVisible=shown;HomeSceneStore.visibility(this,shown);}
    @Override public Engine onCreateEngine(){CatEngine engine=new CatEngine();engines.add(engine);return engine;}
    @Override protected void dump(FileDescriptor fd,PrintWriter out,String[] args){
        super.dump(fd,out,args);out.println("cat_home_visible="+wallpaperVisible+" cat_frames="+renderedFrames);
        for(CatEngine e:engines){
            out.println("preview="+e.isPreview()+" visible="+e.visible+" ready="+e.surfaceReady+" viewport="+e.scene.width+"x"+e.scene.height+" cat_painted="+e.lastPaintCat+" error="+e.lastError);
            for(int i=0;i<e.scene.actors.length;i++){
                PetScene.Actor a=e.scene.actors[i];if(a==null)continue;
                android.graphics.RectF bounds=a.bounds();
                out.println("actor="+i+" coat="+a.coat+" action="+a.action+" jumps="+a.landings+" owner="+a.owner+" hitX="+bounds.centerX()+" hitY="+bounds.centerY());
            }
        }
    }
    final class CatEngine extends Engine implements SharedPreferences.OnSharedPreferenceChangeListener {
        PetScene scene;
        PetTouch touch;
        Backdrop backdrop;
        boolean visible,surfaceReady,closed,lastPaintCat;
        float cameraX,cameraY;
        final PetSettings settings=new PetSettings(CatWallpaperService.this);
        final PetFrameLoop clock=new PetFrameLoop(elapsed->{
            if(closed||!visible||!surfaceReady)return false;
            render(elapsed);
            return settings.enabled() && scene.animating();
        });
        int offsetX,offsetY;
        String lastError="none";
        final float density=getResources().getDisplayMetrics().density;
        final KeyguardManager keyguard=(KeyguardManager)getSystemService(KEYGUARD_SERVICE);
        CatEngine(){setTouchEventsEnabled(true);settings.prefs.registerOnSharedPreferenceChangeListener(this);}
        @Override public void onCreate(SurfaceHolder holder){super.onCreate(holder);reload();}
        void reload(){
            if(touch!=null)touch.cancel();if(backdrop!=null)backdrop.close();
            if(isPreview()&&scene!=null)scene.close();
            scene=isPreview()?new PetScene(CatWallpaperService.this,this::schedule):HomeSceneStore.get(CatWallpaperService.this);
            touch=new PetTouch(CatWallpaperService.this,()->scene,index->scene.openMenu(index),this::schedule);
            touch.followEmptySpace=true;
            backdrop=new Backdrop(CatWallpaperService.this,this::schedule);
            if(surfaceReady){android.graphics.Rect r=getSurfaceHolder().getSurfaceFrame();resizeViewport(r.width(),r.height());}
        }
        void schedule(){
            if(closed||!visible||!surfaceReady)return;
            if(settings.enabled())clock.start();else{clock.stop();render(0);}
        }
        void render(double delta){
            if(closed||!surfaceReady)return;
            boolean paintCat=settings.enabled() && (isPreview()||(!CatOverlayService.displaying&&!keyguard.isKeyguardLocked()));
            if(delta>0 && paintCat)scene.tick(delta);
            boolean trace=android.os.Build.VERSION.SDK_INT>=29 && android.os.Trace.isEnabled() && paintCat && !isPreview();
            if(trace){
                PetScene.Actor a=scene.actors[0];
                android.os.Trace.beginSection("CatWallpaperPose/"+a.action+"/"+a.currentFrame()+"/"+Math.round(a.x*100)+"/"+Math.round(a.lift*100));
                try{paintSurface(paintCat);}finally{android.os.Trace.endSection();}
            }else paintSurface(paintCat);
        }
        void paintSurface(boolean paintCat){
            Canvas canvas=null;
            try {
                try{canvas=getSurfaceHolder().lockHardwareCanvas();}catch(UnsupportedOperationException e){canvas=getSurfaceHolder().lockCanvas();}
                if(canvas!=null){
                    canvas.drawColor(0xffece3f9);canvas.save();canvas.scale(density,density);
                    // Android scrolls a virtual wallpaper surface, often several screens wide.
                    // Cancel that camera translation; pet state stays in the visible viewport.
                    // Offset callbacks can describe the launcher's desired virtual size,
                    // even when Android supplied a screen-sized drawing surface. Only
                    // compensate for excess pixels that exist in this actual buffer.
                    canvas.translate(Math.max(0,canvas.getWidth()/density-scene.width)*cameraX,
                        Math.max(0,canvas.getHeight()/density-scene.height)*cameraY);
                    backdrop.draw(canvas,scene.width,scene.height);if(paintCat)scene.draw(canvas);lastPaintCat=paintCat&&settings.enabled();
                    canvas.restore();renderedFrames++;lastError="none";
                }
            }catch(IllegalArgumentException|IllegalStateException e){lastError=e.toString();}
            finally{
                if(canvas!=null)try{getSurfaceHolder().unlockCanvasAndPost(canvas);}catch(IllegalArgumentException|IllegalStateException e){lastError=e.toString();}
            }
        }
        @Override public void onVisibilityChanged(boolean value){visible=value;updateHomeVisibility();clock.stop();if(!value&&touch!=null)touch.cancel();if(value&&!isPreview()&&settings.enabled())AppUpdates.get(CatWallpaperService.this).check(false);schedule();}
        @Override public void onZoomChanged(float zoom){if(!isPreview()&&visible)HomeSceneStore.zoom(CatWallpaperService.this,zoom);}
        @Override public void onSurfaceCreated(SurfaceHolder holder){super.onSurfaceCreated(holder);surfaceReady=true;schedule();}
        float groundBottom(float height){int percent=Math.max(30,Math.min(88,settings.prefs.getInt("home_floor_percent",58)));return height*(1-percent/100f);}
        void resizeViewport(int w,int h){android.util.DisplayMetrics m=getResources().getDisplayMetrics();float width=Math.min(w,m.widthPixels)/density,height=Math.min(h,m.heightPixels)/density;scene.resize(width,height,groundBottom(height));}
        @Override public void onSurfaceChanged(SurfaceHolder holder,int format,int w,int h){super.onSurfaceChanged(holder,format,w,h);resizeViewport(w,h);render(0);schedule();}
        @Override public void onSurfaceRedrawNeeded(SurfaceHolder holder){super.onSurfaceRedrawNeeded(holder);render(0);}
        @Override public void onOffsetsChanged(float x,float y,float xs,float ys,int xp,int yp){cameraX=x;cameraY=y;offsetX=xp;offsetY=yp;if(visible)render(0);}
        @Override public void onSurfaceDestroyed(SurfaceHolder holder){surfaceReady=false;clock.stop();super.onSurfaceDestroyed(holder);}
        @Override public void onTouchEvent(MotionEvent event){if((isPreview()||(!CatOverlayService.displaying&&!keyguard.isKeyguardLocked()))&&touch!=null)touch.event(event.getActionMasked(),event.getPointerCount(),event.getRawX()/density,event.getRawY()/density,event.getEventTime());super.onTouchEvent(event);}
        @Override public void onSharedPreferenceChanged(SharedPreferences prefs,String key){if(key==null||key.startsWith("position."))return;if(PetSettings.changesScene(key)||key.equals("photo"))reload();schedule();}
        @Override public void onDestroy(){closed=true;engines.remove(this);updateHomeVisibility();if(touch!=null)touch.cancel();clock.stop();settings.prefs.unregisterOnSharedPreferenceChangeListener(this);if(isPreview())scene.close();backdrop.close();super.onDestroy();}
    }
}
