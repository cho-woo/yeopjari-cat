package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import java.io.IOException;
import java.io.InputStream;
import java.lang.ref.WeakReference;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class Backdrop {
    private static final ExecutorService reader=Executors.newSingleThreadExecutor(task->
        new Thread(()->{android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);task.run();},"pet-background"));
    private static final Handler main=new Handler(Looper.getMainLooper());
    // The preview and live wallpaper often request the same picture together.
    private static String cachedKey="";
    private static WeakReference<Bitmap> cached=new WeakReference<>(null);

    private final Context context;
    private final Runnable invalidate;
    private final Paint paint=new Paint(Paint.FILTER_BITMAP_FLAG);
    final RectF destination=new RectF();
    Bitmap bitmap;
    PlayBackdrop displayed;
    private boolean tall;
    private String requested="";
    private volatile int revision;

    Backdrop(Context context,Runnable invalidate,boolean tall){
        this.context=context.getApplicationContext();this.invalidate=invalidate;this.tall=tall;refresh();
    }
    void viewport(float width,float height){
        boolean portrait=width<height*.74f;
        if(portrait!=tall){tall=portrait;refresh();}
    }
    void refresh(){
        PetSettings settings=new PetSettings(context);
        PlayBackdrop choice=PlayBackdrop.selected(settings);
        boolean portrait=tall;
        String key=choice==PlayBackdrop.PHOTO?settings.photo():choice.key+(portrait?"-tall":"-wide");
        if(key.equals(requested))return;
        requested=key;
        int ticket=++revision;
        reader.execute(()->{
            if(ticket!=revision)return;
            Bitmap loaded=key.equals(cachedKey)?cached.get():null;
            PlayBackdrop design=choice;
            if(loaded==null){
                if(choice==PlayBackdrop.PHOTO){
                    try{loaded=readPhoto(Uri.parse(key));}
                    catch(IOException|SecurityException|IllegalArgumentException unavailable){ /* Photo was moved or access expired. */ }
                    if(loaded==null)design=PlayBackdrop.SUNROOM;
                }
                if(loaded==null)loaded=BitmapFactory.decodeResource(context.getResources(),portrait?design.portraitImage:design.image);
                if(loaded!=null&&design==choice){cachedKey=key;cached=new WeakReference<>(loaded);}
            }
            Bitmap result=loaded;
            PlayBackdrop resultDesign=design;
            main.post(()->{
                if(ticket!=revision)return;
                displayed=resultDesign;bitmap=result;invalidate.run();
            });
        });
    }
    private Bitmap readPhoto(Uri uri)throws IOException{
        BitmapFactory.Options options=new BitmapFactory.Options();
        options.inJustDecodeBounds=true;
        try(InputStream in=context.getContentResolver().openInputStream(uri)){BitmapFactory.decodeStream(in,null,options);}
        options.inJustDecodeBounds=false;options.inSampleSize=1;
        while(Math.max(options.outWidth,options.outHeight)/options.inSampleSize>2048)options.inSampleSize*=2;
        try(InputStream in=context.getContentResolver().openInputStream(uri)){return BitmapFactory.decodeStream(in,null,options);}
    }
    void draw(Canvas canvas,float width,float height,float floor){
        canvas.drawColor(0xffece3f9);
        if(bitmap==null)return;
        displayed.frame(destination,width,height,bitmap.getWidth(),bitmap.getHeight(),floor);
        canvas.drawBitmap(bitmap,null,destination,paint);
    }
    void close(){++revision;bitmap=null;}
}
