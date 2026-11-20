package com.valuewander.yeopjaricat;

import android.graphics.Canvas;
import android.service.wallpaper.WallpaperService;
import android.view.SurfaceHolder;

/** An independent green live wallpaper used only to test restoring another service. */
public final class FixtureWallpaperService extends WallpaperService {
    @Override public Engine onCreateEngine(){return new Engine(){
        void paint(){Canvas canvas=null;try{canvas=getSurfaceHolder().lockCanvas();if(canvas!=null)canvas.drawColor(0xff186961);}catch(RuntimeException ignored){}finally{if(canvas!=null)getSurfaceHolder().unlockCanvasAndPost(canvas);}}
        @Override public void onVisibilityChanged(boolean value){if(value)paint();}
        @Override public void onSurfaceChanged(SurfaceHolder h,int format,int w,int height){paint();}
        @Override public void onSurfaceRedrawNeeded(SurfaceHolder h){paint();}
    };}
}
