package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.net.Uri;
import java.io.InputStream;

final class Backdrop {
    final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    Bitmap photo;
    private boolean closed;
    Backdrop(Context context,Runnable invalidate) {
        String uri=new PetSettings(context).prefs.getString("photo","");
        if(uri.isEmpty())return;
        MotionLibrary library=MotionLibrary.get(context);
        library.worker.execute(()->{
            Bitmap loaded=null;
            try {
                BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;
                try(InputStream in=context.getContentResolver().openInputStream(Uri.parse(uri))){BitmapFactory.decodeStream(in,null,options);}
                options.inJustDecodeBounds=false;options.inSampleSize=1;
                while(Math.max(options.outWidth,options.outHeight)/options.inSampleSize>2048)options.inSampleSize*=2;
                try(InputStream in=context.getContentResolver().openInputStream(Uri.parse(uri))){loaded=BitmapFactory.decodeStream(in,null,options);}
            } catch(Exception ignored){ /* A removed photo falls back to the bundled solid backdrop. */ }
            Bitmap result=loaded;library.main.post(()->{if(!closed){photo=result;invalidate.run();}});
        });
    }
    void draw(Canvas canvas,float width,float height) {
        canvas.drawColor(Color.rgb(236,227,249));
        if(photo!=null) {
            float scale=Math.max(width/photo.getWidth(),height/photo.getHeight());
            float w=photo.getWidth()*scale,h=photo.getHeight()*scale;
            canvas.drawBitmap(photo,null,new RectF((width-w)/2,(height-h)/2,(width+w)/2,(height+h)/2),paint);
        }
    }
    void close(){closed=true;photo=null;}
}
