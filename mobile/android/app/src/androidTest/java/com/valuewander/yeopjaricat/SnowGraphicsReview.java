package com.valuewander.yeopjaricat;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.PixelCopy;
import android.view.View;
import android.view.ViewGroup;
import java.io.File;
import java.io.FileOutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Capture the actual hardware-rendered actor at both endpoints and half fade
 * in one window frame. On an opaque backdrop the middle must equal the mean
 * of the endpoints, even where coats overlap or the source has partial alpha. */
final class SnowGraphicsReview {
    final MotionSmoke test;
    static final int TILE=160;
    SnowGraphicsReview(MotionSmoke test){this.test=test;}

    void run() throws Exception {
        PetScene[] scenes=new PetScene[1];
        MotionLibrary.Clip[] groom=new MotionLibrary.Clip[1];
        CountDownLatch ready=new CountDownLatch(1),drawn=new CountDownLatch(1);
        test.runOnMainSync(()->{
            test.activity.preview.stop();
            scenes[0]=new PetScene(test.activity,()->{});
            scenes[0].library.request(10,"groom",c->{groom[0]=c;ready.countDown();});
        });
        test.check(ready.await(15,TimeUnit.SECONDS)&&groom[0]!=null,"groom not loaded");
        PetScene scene=scenes[0];
        View[] panel=new View[1];
        test.runOnMainSync(()->{
            scene.size=108;scene.floor=142;
            MotionLibrary.Clip neutral=scene.library.neutral(10);
            for(int i=0;i<2;i++){
                PetScene.Actor a=scene.new Actor(10,80,"ear-flick",0);
                scene.actors[i]=a;a.clip=neutral;a.clock=neutral.clock();
                a.previousPose=groom[0];a.previousFrame=103;
                a.previousFacing=1;a.facing=i==0?1:-1;
            }
            panel[0]=new View(test.activity){
                @Override protected void onDraw(Canvas canvas){
                    test.check(canvas.isHardwareAccelerated(),"expected Android hardware renderer");
                    for(int row=0;row<4;row++){
                        PetScene.Actor a=scene.actors[row%2];
                        for(int col=0;col<3;col++){
                            int save=canvas.save();canvas.translate(col*TILE,row*TILE);
                            canvas.clipRect(0,0,TILE,TILE);
                            canvas.drawColor(row<2?0xff192327:0xffe8dbcf);
                            a.fade=col*.5f;a.draw(canvas);canvas.restoreToCount(save);
                        }
                    }
                    drawn.countDown();
                }
            };
            test.activity.addContentView(panel[0],new ViewGroup.LayoutParams(3*TILE,4*TILE));
        });
        test.check(drawn.await(10,TimeUnit.SECONDS),"frame not drawn");
        test.waitForIdleSync();
        int[] xy=new int[2];test.runOnMainSync(()->panel[0].getLocationInWindow(xy));
        Bitmap image=Bitmap.createBitmap(3*TILE,4*TILE,Bitmap.Config.ARGB_8888);
        CountDownLatch copied=new CountDownLatch(1);
        int[] result={-1};
        PixelCopy.request(test.activity.getWindow(),new Rect(xy[0],xy[1],xy[0]+image.getWidth(),xy[1]+image.getHeight()),
            image,r->{result[0]=r;copied.countDown();},new Handler(Looper.getMainLooper()));
        test.check(copied.await(5,TimeUnit.SECONDS)&&result[0]==PixelCopy.SUCCESS,"window capture "+result[0]);
        int worst=0,off=0,painted=0;
        for(int y=0;y<4*TILE;y++)for(int x=0;x<TILE;x++){
            int l=image.getPixel(x,y),m=image.getPixel(x+TILE,y),r=image.getPixel(x+2*TILE,y);
            if(l!=image.getPixel(0,y)||r!=image.getPixel(2*TILE,y))painted++;
            int error=Math.max(error(Color.red(l),Color.red(m),Color.red(r)),
                Math.max(error(Color.green(l),Color.green(m),Color.green(r)),error(Color.blue(l),Color.blue(m),Color.blue(r))));
            if(error>3)off++;worst=Math.max(worst,error);
        }
        try(FileOutputStream out=new FileOutputStream(new File(test.directory,"snow-transition.png"))){
            image.compress(Bitmap.CompressFormat.PNG,100,out);
        }
        test.runOnMainSync(scene::close);
        test.check(painted>10000,"empty hardware capture");
        Bundle report=new Bundle();report.putString("stream","SNOW_GRAPHICS max_channel_error="+worst+" pixels_outside_3="+off+" painted_pixels="+painted);
        test.finish(off==0?MotionSmoke.RESULT_OK:MotionSmoke.RESULT_CANCELED,report);
    }
    void playback() throws Exception {
        ActionParityReview setup=new ActionParityReview(test);
        setup.configure(10,10);
        PetScene scene=setup.scene;
        test.runOnMainSync(()->{scene.socialTimer=120000;test.activity.preview.start();});
        String[][] pairs={{"tower","scratch"},{"box","groom"},{"toy","fish"}};
        for(String[] pair:pairs){
            PetScene.Actor a=scene.actors[0], b=scene.actors[1];
            long first=a.completed,second=b.completed;
            test.runOnMainSync(()->{a.request(pair[0]);b.request(pair[1]);test.activity.preview.wake();});
            long deadline=android.os.SystemClock.elapsedRealtime()+35000;
            boolean[] done={false};
            while(!done[0] && android.os.SystemClock.elapsedRealtime()<deadline){
                Thread.sleep(100);
                test.runOnMainSync(()->done[0]=a.completed>first&&b.completed>second);
            }
            test.check(done[0],"incomplete "+pair[0]+"/"+pair[1]+" at "+a.owner+"/"+a.action+"/"+a.age+
                " and "+b.owner+"/"+b.action+"/"+b.age+" enabled="+scene.settings.enabled()+
                " liveScene="+(scene==test.activity.preview.scene)+" running="+test.activity.preview.running+
                " displayed="+test.activity.preview.renderedFrames+" sceneClosed="+scene.closed);
            test.check(a.finishedOwner.equals(pair[0])&&b.finishedOwner.equals(pair[1]),"wrong owner");
        }
        Bundle result=new Bundle();result.putString("stream","SNOW_PLAYBACK_PASSED draws="+test.activity.preview.renderedFrames);
        test.finish(MotionSmoke.RESULT_OK,result);
    }
    void jump() throws Exception {
        ActionParityReview playback = new ActionParityReview(test);
        playback.configure(10, 10);
        PetScene scene = playback.scene;
        test.waitFor(() -> scene.actors[0].idle != null && scene.actors[1].idle != null, "Snowdrop not ready");
        test.runOnMainSync(() -> scene.command("jump"));
        playback.sweep("snow-jump", () -> !scene.actors[0].busy() && !scene.actors[1].busy());
        for (PetScene.Actor actor : scene.actors)
            test.check("jump".equals(actor.finishedOwner), "Jump command did not finish");

        MotionLibrary.Clip[] clip = new MotionLibrary.Clip[1];
        CountDownLatch ready = new CountDownLatch(1);
        test.runOnMainSync(() -> scene.library.request(10, "jump", c -> { clip[0] = c; ready.countDown(); }));
        test.check(ready.await(10, TimeUnit.SECONDS) && clip[0] != null, "Jump clip unavailable");
        Bitmap frame = Bitmap.createBitmap(160, 160, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(frame);
        android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);
        // Sample the two separate ear corridors relative to the recorded floor anchor.
        int failed = 0, minNear = Integer.MAX_VALUE, minFar = Integer.MAX_VALUE;
        for (int i = 0; i < clip[0].rectangles.length; i++) {
            for (int facing : new int[]{1, -1}) {
                frame.eraseColor(0);
                clip[0].paint(canvas, i, 80, 120, clip[0].reference, facing, paint);
                int near = earPixels(frame, facing, -9, 10, 30);
                int far = earPixels(frame, facing, 30, 46, 25);
                minNear = Math.min(minNear, near);
                minFar = Math.min(minFar, far);
                if (near < 90 || far < 18) failed++;
            }
        }
        String summary = "frames=" + clip[0].rectangles.length + " both_facings failed=" + failed +
                " min_near=" + minNear + " min_far=" + minFar;
        test.check(failed == 0, summary);
        Bundle result = new Bundle();
        result.putString("stream", "SNOW_JUMP_PASSED " + summary);
        test.finish(MotionSmoke.RESULT_OK, result);
    }
    private static int earPixels(Bitmap frame, int facing, int left, int right, int chroma) {
        int count = 0;
        for (int y = 17; y < 77; y++)
            for (int x = left; x < right; x++) {
                int pixel = frame.getPixel(facing == 1 ? 80 + x : 79 - x, y);
                if (Color.alpha(pixel) > 200 && Color.red(pixel) - Color.green(pixel) > chroma) count++;
            }
        return count;
    }
    static int error(int a,int middle,int b){return Math.abs(middle-(a+b+1)/2);}
}
