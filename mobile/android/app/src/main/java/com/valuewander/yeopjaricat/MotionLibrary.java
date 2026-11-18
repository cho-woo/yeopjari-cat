package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.util.LruCache;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class MotionLibrary {
    interface Ready { void accept(Clip clip); }
    static final class Clip {
        final JSONObject spec;
        final Bitmap[] pages;
        final int[] frames, times;
        final int loopStart, loopEnd, loopMs;
        final float reference, stride;
        final float[][] paws;
        final int[][] rectangles;
        final float[][] anchors;
        final int[] page;
        final Rect sourceRect=new Rect();
        final android.graphics.RectF poseRect=new android.graphics.RectF();
        float left=Float.POSITIVE_INFINITY,top=Float.POSITIVE_INFINITY,right=Float.NEGATIVE_INFINITY,bottom=Float.NEGATIVE_INFINITY;
        Clip(JSONObject spec, Bitmap[] pages) throws Exception {
            this.spec=spec; this.pages=pages;
            reference=(float)spec.getDouble("reference_height"); stride=(float)spec.optDouble("stride_pixels",0);
            JSONArray points=spec.optJSONArray("paws");
            paws=points==null?new float[0][2]:new float[points.length()][2];
            for(int i=0;i<paws.length;i++)for(int k=0;k<2;k++)paws[i][k]=(float)points.getJSONArray(i).getDouble(k);
            JSONObject sequence=spec.getJSONObject("sequence");
            ArrayList<Integer> frameList=new ArrayList<>(), timeList=new ArrayList<>();
            append(sequence.getJSONArray("intro"),frameList,timeList);
            loopStart=frameList.size(); append(sequence.getJSONArray("loop"),frameList,timeList); loopEnd=frameList.size();
            append(sequence.getJSONArray("outro"),frameList,timeList);
            frames=frameList.stream().mapToInt(Integer::intValue).toArray();
            times=timeList.stream().mapToInt(Integer::intValue).toArray();
            int total=0; for(int i=loopStart;i<loopEnd;i++) total+=times[i]; loopMs=total;
            JSONArray all=spec.getJSONArray("frames");
            rectangles=new int[all.length()][4]; anchors=new float[all.length()][2]; page=new int[all.length()];
            for(int i=0;i<all.length();i++) {
                JSONObject f=all.getJSONObject(i); page[i]=f.getInt("page");
                for(int k=0;k<4;k++) rectangles[i][k]=f.getJSONArray("rect").getInt(k);
                for(int k=0;k<2;k++) anchors[i][k]=(float)f.getJSONArray("anchor").getDouble(k);
                left=Math.min(left,-anchors[i][0]);top=Math.min(top,-anchors[i][1]);right=Math.max(right,rectangles[i][2]-anchors[i][0]);bottom=Math.max(bottom,rectangles[i][3]-anchors[i][1]);
            }
        }
        private static void append(JSONArray array, ArrayList<Integer> frames, ArrayList<Integer> times) throws Exception {
            for(int i=0;i<array.length();i++) { JSONObject step=array.getJSONObject(i);frames.add(step.getInt("frame"));times.add(step.getInt("ms")); }
        }
        MotionClock clock() { return new MotionClock(frames,times,loopStart,loopEnd); }
        int bytes() { int n=0;for(Bitmap b:pages)n+=b.getAllocationByteCount();return n; }
        void paint(Canvas canvas, int frame, float x, float floor, float height, int facing, Paint paint) {
            int[] r=rectangles[frame]; float scale=height/reference;
            int saved=canvas.save(); canvas.translate(x,floor);canvas.scale(facing*scale,scale);
            sourceRect.set(r[0],r[1],r[0]+r[2],r[1]+r[3]);
            poseRect.set(-anchors[frame][0],-anchors[frame][1],r[2]-anchors[frame][0],r[3]-anchors[frame][1]);
            canvas.drawBitmap(pages[page[frame]],sourceRect,poseRect,paint);
            canvas.restoreToCount(saved);
        }
    }
    @android.annotation.SuppressLint("StaticFieldLeak") // Only the application context is retained, never an Activity.
    private static MotionLibrary instance;
    static synchronized MotionLibrary get(Context c) { if(instance==null)instance=new MotionLibrary(c.getApplicationContext());return instance; }
    final Context context;
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    final Handler main=new Handler(Looper.getMainLooper());
    final HashMap<Integer,Clip> neutrals=new HashMap<>();
    final HashMap<String,ArrayList<Ready>> waiting=new HashMap<>();
    final LruCache<String,Clip> cache=new LruCache<>(Math.min(48*1024*1024,(int)(Runtime.getRuntime().maxMemory()/5))) { @Override protected int sizeOf(String key,Clip clip){return clip.bytes();} };
    private MotionLibrary(Context context) { this.context=context; }
    Clip neutral(int coat){
        if(!neutrals.containsKey(coat))try{neutrals.put(coat,readClip(coat,"neutral"));}
        catch(Exception e){android.util.Log.e("YeopjariCat","Cannot load initial cat",e);}
        return neutrals.get(coat);
    }
    private Clip readClip(int coat,String action)throws Exception{
        JSONObject spec;
        try(InputStream in=context.getAssets().open("motions/c"+coat+"-"+action+".json")){
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int read;
            while((read=in.read(buffer))!=-1)bytes.write(buffer,0,read);
            spec=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
        }
        JSONArray ps=spec.getJSONArray("pages");Bitmap[] pages=new Bitmap[ps.length()];
        for(int i=0;i<pages.length;i++)try(InputStream in=context.getAssets().open("motions/"+ps.getJSONObject(i).getString("file"))){
            pages[i]=BitmapFactory.decodeStream(in);if(pages[i]==null)throw new IllegalStateException("Cannot decode atlas");
        }
        return new Clip(spec,pages);
    }
    void request(int coat, String action, Ready callback) {
        String key=coat+":"+action;
        Clip found=cache.get(key);if(found!=null){callback.accept(found);return;}
        if(waiting.containsKey(key)){waiting.get(key).add(callback);return;}
        ArrayList<Ready> callbacks=new ArrayList<>();callbacks.add(callback);waiting.put(key,callbacks);
        worker.execute(()->{
            Clip clip=null;
            try {
                clip=readClip(coat,action);cache.put(key,clip);
            } catch(Exception e) { android.util.Log.e("YeopjariCat","Motion load failed: "+key,e); }
            Clip result=clip;
            main.post(()->{ArrayList<Ready> list=waiting.remove(key);if(list!=null)for(Ready cb:list)cb.accept(result);});
        });
    }
}
