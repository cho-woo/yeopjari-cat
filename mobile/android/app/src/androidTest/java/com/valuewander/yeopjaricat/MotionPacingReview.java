package com.valuewander.yeopjaricat;

import android.content.Intent;
import android.view.InputDevice;
import android.view.View;
import android.view.inspector.WindowInspector;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Choreographer;
import android.view.MotionEvent;
import android.view.ViewTreeObserver;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.function.BooleanSupplier;

/** Identical instrumentation can run against the previous public APK. Uses wall time and the visible Activity. */
final class MotionPacingReview {
    final MotionSmoke test;
    final ActionParityReview fixture;
    final LinkedHashMap<String,ArrayList<Long>> gaps=new LinkedHashMap<>();
    final JSONArray loads=new JSONArray(),stories=new JSONArray();
    final String[] loading=new String[2];
    final long[] loadSince=new long[2];
    final int[] lastFrames={-1,-1};
    final String[] lastActions={"",""};
    String trial="",previousPhase="";
    int focus;
    boolean home;
    PetScene scene;
    View observed;
    long downTime;
    long lastDraw,trialStart;
    boolean sampling;
    final java.util.HashSet<String> actions=new java.util.HashSet<>();
    MotionPacingReview(MotionSmoke test){this.test=test;fixture=new ActionParityReview(test);}
    String phase(){
        PetScene s=scene;
        if(trial.equals("pair")){
            PetScene.Actor a=s.actors[0],b=s.actors[1];
            if(a.loading||b.loading)return "pair/loading";
            if(a.action.equals("punch")||b.action.equals("punch")||a.action.equals("reach")||b.action.equals("reach"))return "pair/paws";
            return "pair/"+(a.action.equals("walk")||b.action.equals("walk")?"walk":"transition");
        }
        PetScene.Actor a=s.actors[focus];
        return "finger/"+(a.loading?"loading":a.action);
    }
    final ViewTreeObserver.OnDrawListener draw=this::onDraw;
    void onDraw(){
        if(trial.isEmpty())return;
        long now=System.nanoTime()/1000000;
        String p=phase();
        if(lastDraw>0){
            long gap=now-lastDraw;
            gaps.computeIfAbsent(trial.equals("pair")?"pair/all":"finger/all",k->new ArrayList<>()).add(gap);
            gaps.computeIfAbsent(previousPhase,k->new ArrayList<>()).add(gap);
        }
        previousPhase=p;lastDraw=now;
        for(int i=0;i<2;i++){
            PetScene.Actor a=scene.actors[i];if(a.clock==null)continue;
            if(lastFrames[i]!=a.currentFrame()||!lastActions[i].equals(a.action)){
                actions.add(i+"/"+a.action);lastFrames[i]=a.currentFrame();lastActions[i]=a.action;
            }
        }
    }
    void sampleLoads(long now){
        for(int i=0;i<2;i++){
            PetScene.Actor a=scene.actors[i];String next=a.loading?a.loadingAction:null;
            if(java.util.Objects.equals(next,loading[i]))continue;
            if(loading[i]!=null)try{
                loads.put(new JSONObject().put("trial",trial).put("actor",i).put("clip",loading[i])
                    .put("at_ms",loadSince[i]-trialStart).put("observed_wait_ms",now-loadSince[i]));
            }catch(org.json.JSONException e){throw new AssertionError(e);}
            loading[i]=next;loadSince[i]=now;
        }
    }
    final Choreographer.FrameCallback probe=new Choreographer.FrameCallback(){
        public void doFrame(long nanos){
            if(!sampling)return;
            if(!trial.isEmpty())sampleLoads(SystemClock.uptimeMillis());
            Choreographer.getInstance().postFrameCallback(this);
        }
    };
    void begin(String name,int index){
        trial=name;focus=index;actions.clear();lastDraw=0;previousPhase="";
        trialStart=SystemClock.uptimeMillis();
    }
    void finishTrial()throws Exception{
        String name=trial;long elapsed=SystemClock.uptimeMillis()-trialStart;
        stories.put(new JSONObject().put("trial",name).put("wall_ms",elapsed).put("actions",new JSONArray(actions)));
        trial="";lastDraw=0;
    }
    void await(BooleanSupplier done,String label)throws Exception{
        long end=SystemClock.uptimeMillis()+85000;
        boolean[] ready={false};
        do{Thread.sleep(55);test.runOnMainSync(()->ready[0]=done.getAsBoolean());}
        while(!ready[0]&&SystemClock.uptimeMillis()<end);
        test.check(ready[0],"Timed out "+label+" "+scene.state());
    }
    void swipe(int index)throws Exception{
        PetScene s=scene;PetScene.Actor a=s.actors[index];float[] points=new float[4];
        long landings=a.landings;
        test.runOnMainSync(()->{
            // Place the two cats on separate lanes before each gesture, as a user can by dragging.
            s.drag(index,s.width*(index==0?.22f:.78f)-a.x,s.floor-a.ground());
            PetScene.Actor other=s.actors[1-index];
            s.drag(1-index,s.width*(index==0?.80f:.20f)-other.x,s.floor-s.size*1.18f-other.ground());
            a.armFinger();points[0]=a.bounds().centerX();points[1]=a.bounds().centerY();
            points[2]=s.width*(index==0?.86f:.14f);points[3]=a.ground()-s.size*.88f;
            begin(index==0?"finger-primary":"finger-companion",index);

        });
        if(home)Thread.sleep(240); // The launcher moves the input window after the setup drag.
        touch(MotionEvent.ACTION_DOWN,points[0],points[1]);
        for(int j=1;j<=12;j++){Thread.sleep(30);float t=j/12f;
            touch(MotionEvent.ACTION_MOVE,points[0]+(points[2]-points[0])*t,points[1]+(points[3]-points[1])*t);}
        touch(MotionEvent.ACTION_UP,points[2],points[3]);
        await(()->!a.busy(),trial);
        test.runOnMainSync(()->{
            test.check(a.landings>landings&&actions.contains(index+"/run")&&actions.contains(index+"/cursor-pounce")&&actions.contains(index+"/punch"),"Incomplete swipe "+actions);
            try{finishTrial();}catch(Exception e){throw new AssertionError(e);}
        });
    }
    void touch(int type,float x,float y){
        long now=SystemClock.uptimeMillis();
        if(!home){
            test.runOnMainSync(()->test.check(test.activity.preview.touch.event(type,1,x,y,now),"Gesture not handled"));
            return;
        }
        if(type==MotionEvent.ACTION_DOWN)downTime=now;
        float density=test.activity.getResources().getDisplayMetrics().density;
        MotionEvent event=MotionEvent.obtain(downTime,now,type,x*density,y*density,0);
        event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        try{test.check(test.getUiAutomation().injectInputEvent(event,true),"Launcher input injection failed");}
        finally{event.recycle();}
    }
    void openHome(int coat)throws Exception{
        String pkg=test.getTargetContext().getPackageName();
        test.shell("appops set "+pkg+" SYSTEM_ALERT_WINDOW allow");
        test.shell("appops set "+pkg+" GET_USAGE_STATS allow");
        test.runOnMainSync(()->{
            new PetSettings(test.activity).prefs.edit().putBoolean("overlay",false).putBoolean("touch_home",true).apply();
            scene=HomeSceneStore.get(test.activity);scene.automatic=false;scene.random.setSeed(coat+485);
            test.activity.startForegroundService(new Intent(test.activity,CatOverlayService.class));
            test.activity.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));
        });
        await(()->{
            for(View root:WindowInspector.getGlobalWindowViews())if(root instanceof CatOverlayService.PetWindow
                &&((CatOverlayService.PetWindow)root).actor==0&&root.isAttachedToWindow()){observed=root;return true;}
            return false;
        },"launcher cat window");
    }
    void run(int coat,boolean onHome)throws Exception{
        home=onHome;
        String mode=home?"pacing-home":"pacing";
        fixture.configure(coat,coat==0?8:coat==8?10:0);
        scene=fixture.scene;observed=test.activity.preview;
        if(home)openHome(coat);
        test.runOnMainSync(()->{
            observed.getViewTreeObserver().addOnDrawListener(draw);
            sampling=true;Choreographer.getInstance().postFrameCallback(probe);
            if(!home)test.activity.preview.start();
        });
        try{
            swipe(0);swipe(1);
            PetScene s=scene;long count=s.socialCount;
            test.runOnMainSync(()->{
                for(int i=0;i<2;i++){PetScene.Actor a=s.actors[i];s.drag(i,s.width*(i==0?.23f:.77f)-a.x,s.floor-a.ground());}
                begin("pair",0);s.social("paw",0);
            });
            await(()->s.socialCount>count,"pair");
            test.runOnMainSync(()->{for(int i=0;i<2;i++)test.check(actions.contains(i+"/reach")&&actions.contains(i+"/punch"),"Missing pair response "+actions);
                try{finishTrial();}catch(Exception e){throw new AssertionError(e);}});
        }finally{test.runOnMainSync(()->{sampling=false;Choreographer.getInstance().removeFrameCallback(probe);observed.getViewTreeObserver().removeOnDrawListener(draw);
            if(home)test.activity.stopService(new Intent(test.activity,CatOverlayService.class));});}
        JSONObject metrics=new JSONObject();
        for(java.util.Map.Entry<String,ArrayList<Long>> e:gaps.entrySet()){
            ArrayList<Long> list=e.getValue();Collections.sort(list);int n=list.size();
            metrics.put(e.getKey(),new JSONObject().put("draw_intervals",n).put("p50_ms",list.get(n/2)).put("p95_ms",list.get((n-1)*95/100))
                .put("max_ms",list.get(n-1)).put("over_100_ms",list.stream().filter(x->x>100).count()));
        }
        JSONObject report=new JSONObject().put("version",test.getTargetContext().getPackageManager().getPackageInfo(test.getTargetContext().getPackageName(),0).versionName)
            .put("surface",home?"launcher":"activity").put("coat",coat).put("stories",stories).put("draw_intervals",metrics).put("observed_clip_waits",loads);
        Files.write(new File(test.directory,mode+"-"+coat+".json").toPath(),report.toString(2).getBytes(StandardCharsets.UTF_8));
        Bundle result=new Bundle();result.putString("stream","PARITY_PASSED "+mode+" "+coat);test.finish(-1,result);
    }
}
