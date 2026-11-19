package com.valuewander.yeopjaricat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.ColorSpace;
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
import java.util.concurrent.LinkedBlockingDeque;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

final class MotionLibrary {
    // Cache a projection of the original walk clip, retaining its frame IDs and anchors.
    static final String WALK_RECOVERY="walk-outro";
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
        Clip(JSONObject spec, Bitmap[] pages, boolean outroOnly) throws Exception {
            this.spec=spec; this.pages=pages;
            reference=(float)spec.getDouble("reference_height"); stride=(float)spec.optDouble("stride_pixels",0);
            JSONArray points=spec.optJSONArray("paws");
            paws=points==null?new float[0][2]:new float[points.length()][2];
            for(int i=0;i<paws.length;i++)for(int k=0;k<2;k++)paws[i][k]=(float)points.getJSONArray(i).getDouble(k);
            JSONObject sequence=spec.getJSONObject("sequence");
            ArrayList<Integer> frameList=new ArrayList<>(), timeList=new ArrayList<>();
            if(!outroOnly)append(sequence.getJSONArray("intro"),frameList,timeList);
            loopStart=frameList.size();
            if(!outroOnly)append(sequence.getJSONArray("loop"),frameList,timeList);
            loopEnd=frameList.size();
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
        int bytes() { int n=0;for(Bitmap b:pages)if(b!=null)n+=b.getAllocationByteCount();return n; }
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
    private final LinkedBlockingDeque<Runnable> jobs=new LinkedBlockingDeque<>();
    final ThreadPoolExecutor worker=new ThreadPoolExecutor(1,1,0,TimeUnit.MILLISECONDS,jobs,
        task->new Thread(()->{android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);task.run();},"cat-atlas"));
    final Handler main=new Handler(Looper.getMainLooper());
    final HashMap<Integer,Clip> neutrals=new HashMap<>();
    final HashMap<String,Load> waiting=new HashMap<>();
    // A displayed recovery pose can outlive the LRU entry. Reuse its atlas when
    // either scene requests it again, without keeping discarded poses alive.
    private final HashMap<String,java.lang.ref.WeakReference<Clip>> displayed=new HashMap<>();
    final LruCache<String,Clip> cache=new LruCache<>(Math.min(48*1024*1024,(int)(Runtime.getRuntime().maxMemory()/5))) {
        @Override protected int sizeOf(String key,Clip clip){return clip.bytes();}
        @Override protected void entryRemoved(boolean evicted,String key,Clip old,Clip replacement){
            if(evicted)displayed.put(key,new java.lang.ref.WeakReference<>(old));
        }
    };
    private MotionLibrary(Context context) { this.context=context; }
    Clip neutral(int coat){
        if(!neutrals.containsKey(coat))try{neutrals.put(coat,readClip(coat,"neutral"));}
        catch(Exception e){android.util.Log.e("YeopjariCat","Cannot load initial cat",e);}
        return neutrals.get(coat);
    }
    private Clip readClip(int coat,String action)throws Exception{
        boolean outroOnly=WALK_RECOVERY.equals(action);
        String source=outroOnly?"walk":action;
        JSONObject spec;
        try(InputStream in=context.getAssets().open("motions/c"+coat+"-"+source+".json")){
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int read;
            while((read=in.read(buffer))!=-1)bytes.write(buffer,0,read);
            spec=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
        }
        JSONArray ps=spec.getJSONArray("pages");Bitmap[] pages=new Bitmap[ps.length()];
        Clip clip=new Clip(spec,pages,outroOnly);
        boolean[] needed=new boolean[pages.length];
        for(int frame:clip.frames)needed[clip.page[frame]]=true;
        BitmapFactory.Options decode = new BitmapFactory.Options();
        decode.inPreferredConfig = Bitmap.Config.ARGB_8888;
        decode.inPreferredColorSpace = ColorSpace.get(ColorSpace.Named.SRGB);
        decode.inScaled = false;
        for(int i=0;i<pages.length;i++){
            if(outroOnly && !needed[i])continue;
            try(InputStream in=context.getAssets().open("motions/"+ps.getJSONObject(i).getString("file"))){
                pages[i]=BitmapFactory.decodeStream(in,null,decode);
                if(pages[i]==null)throw new IllegalStateException("Cannot decode atlas");
                if(i==(outroOnly?clip.page[clip.frames[0]]:0))pages[i].prepareToDraw();
            }
        }
        return clip;
    }
    // Submitted and delivered on the main thread. Only bitmap decoding runs on the worker.
    private final class Load implements Runnable {
        final int coat;
        final String action,key;
        final ArrayList<Ready> listeners=new ArrayList<>();
        Load(int coat,String action,String key){this.coat=coat;this.action=action;this.key=key;}
        public void run(){
            Clip clip=null;
            try{clip=readClip(coat,action);}catch(Exception e){android.util.Log.e("YeopjariCat","Motion load failed: "+key,e);}
            Clip result=clip;
            main.post(()->{
                waiting.remove(key);
                if(result!=null)cache.put(key,result);
                for(Ready listener:listeners)listener.accept(result);
            });
        }
    }
    void request(int coat,String action,Ready callback){schedule(coat,action,callback,true);}
    void preload(int coat,String action,Ready callback){schedule(coat,action,callback,false);}
    private void schedule(int coat,String action,Ready callback,boolean current){
        if(WALK_RECOVERY.equals(action)){
            String full=coat+":walk";
            Clip resident=cache.get(full);
            if(resident!=null){if(callback!=null)callback.accept(resident);return;}
            if(waiting.containsKey(full))action="walk";
        }
        String key=coat+":"+action;
        Clip found=cache.get(key);
        if(found==null){
            java.lang.ref.WeakReference<Clip> ref=displayed.remove(key);
            if(ref!=null){found=ref.get();if(found!=null)cache.put(key,found);}
        }
        if(found!=null){if(callback!=null)callback.accept(found);return;}
        Load job=waiting.get(key);
        if(job==null){
            job=new Load(coat,action,key);
            waiting.put(key,job);
            worker.execute(job);
        }
        if(callback!=null)job.listeners.add(callback);
        // A newly requested action outranks speculative work for either cat.
        if(current && jobs.remove(job))jobs.addFirst(job);
    }
}
