package com.valuewander.yeopjaricat;

import android.graphics.*;
import android.os.Bundle;
import java.io.File;
import java.io.FileOutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

final class SnowEarFrames {
    static void run(MotionSmoke t) throws Exception {
        String[] names={"fish","walk","cursor-pounce","ball"};
        // Same two pinnae, in the clip's floor-anchored 96px reference pose.
        int[][][] guides={
            {{50,144,103,183,87},{51,144,103,183,87}},
            {{172,122,82,172,83},{173,124,82,173,84}},
            {{41,133,85,174,81},{42,134,86,175,83}},
            {{42,119,75,164,77},{43,119,75,164,77}}
        };
        Bitmap tile=Bitmap.createBitmap(256,192,Bitmap.Config.ARGB_8888);
        Bitmap board=Bitmap.createBitmap(512,192*names.length,Bitmap.Config.ARGB_8888);
        Canvas output=new Canvas(board);output.drawColor(0xffe2dcd3);
        Paint paint=new Paint(Paint.FILTER_BITMAP_FLAG);
        PetScene scene=t.activity.preview.scene;
        int failures=0;
        StringBuilder report=new StringBuilder();
        for(int k=0;k<names.length;k++) {
            final String name=names[k];
            MotionLibrary.Clip[] loaded=new MotionLibrary.Clip[1];
            CountDownLatch ready=new CountDownLatch(1);
            t.runOnMainSync(()->scene.library.request(10,name,c->{loaded[0]=c;ready.countDown();}));
            t.check(ready.await(15,TimeUnit.SECONDS)&&loaded[0]!=null,name+" unavailable");
            report.append(name);
            for(int face:new int[]{1,-1}){
                int first=0;
                for(int j=0;j<2;j++) {
                    int[] g=guides[k][j];tile.eraseColor(0);
                    loaded[0].paint(new Canvas(tile),g[0],128,150,96,face,paint);
                    int pink=0;
                    for(int y=0;y<tile.getHeight();y++)for(int x=0;x<tile.getWidth();x++){
                        int sx=face==1?x:255-x;
                        int dx=sx-g[1],dy=y-g[2],fx=sx-g[3],fy=y-g[4];
                        if(dx*dx+dy*dy>144&&fx*fx+fy*fy>144)continue;
                        int c=tile.getPixel(x,y),r=Color.red(c),green=Color.green(c),b=Color.blue(c);
                        if(Color.alpha(c)>180&&r-green>23&&100*(r-green)-138*(green-b)>1500)pink++;
                    }
                    report.append(' ').append(pink);
                    if(face==1)output.drawBitmap(tile,null,new Rect(j*256,k*192,j*256+256,k*192+192),null);
                    if(pink<90)failures++;
                    if(j==0)first=pink;
                    else if(Math.min(first,pink)<.65*Math.max(first,pink))failures++;
                }
            }
            report.append('\n');
        }
        try(FileOutputStream file=new FileOutputStream(new File(t.directory,"ear-frames.png"))){
            board.compress(Bitmap.CompressFormat.PNG,100,file);
        }
        Bundle result=new Bundle();
        result.putString("stream",report.toString()+"EAR_FRAMES failures="+failures);
        t.finish(failures==0?MotionSmoke.RESULT_OK:MotionSmoke.RESULT_CANCELED,result);
    }
}

