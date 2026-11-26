package com.valuewander.yeopjaricat;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import org.json.JSONArray;
import org.json.JSONObject;

/** Uses the installed app's two actors and Canvas. Frame-sweep tests advance the
 * production clock; the separate live mode leaves scheduling to Android. */
final class ActionParityReview {
    final MotionSmoke test;
    final MainActivity activity;
    final JSONArray cases=new JSONArray();
    PetScene scene;
    Bitmap view;
    float maxTravel;
    int rootSamples;
    final PlayObject[] missed={new PlayObject(),new PlayObject()};
    ActionParityReview(MotionSmoke test){this.test=test;activity=test.activity;}
    void check(boolean condition,String message){test.check(condition,message);}
    void configure(int a,int b){
        test.runOnMainSync(()->{
            activity.preview.stop();new PetSettings(activity).prefs.edit().putInt("coat",a).putInt("companion_coat",b)
                .putBoolean("companion",true).putBoolean("pets_enabled",true).apply();
            activity.preview.reload();scene=activity.preview.scene;scene.automatic=false;scene.random.setSeed(a+485);
            view=Bitmap.createBitmap(Math.round(scene.width),Math.round(scene.height),Bitmap.Config.ARGB_8888);
        });
    }
    interface Done {boolean yes();}
    void sweep(String label,Done done)throws Exception{
        long deadline=SystemClock.elapsedRealtime()+65000;
        double[] simulated={0};boolean[] finished={false};
        HashSet<String> shown=new HashSet<>();
        while(!finished[0]&&SystemClock.elapsedRealtime()<deadline){
            test.runOnMainSync(()->{
                for(int batch=0;batch<36;batch++){
                    if(done.yes()){finished[0]=true;break;}
                    if(scene.actors[0].loading||scene.actors[1].loading)break;
                    float[] old={scene.actors[0].x,scene.actors[0].footY(),scene.actors[1].x,scene.actors[1].footY()};
                    scene.tick(32);simulated[0]+=32;
                    view.eraseColor(Color.TRANSPARENT);scene.draw(new Canvas(view));
                    for(int i=0;i<2;i++){
                        PetScene.Actor a=scene.actors[i];float delta=(float)Math.hypot(a.x-old[i*2],a.footY()-old[i*2+1]);
                        if(label.endsWith("/pair/paw") && a.step!=null && scene.socialActive){
                            check(scene.socialPhase!=2 || a.step.bridge>=0,"Seated outro between paw exchanges");
                            check(scene.socialPhase!=3 || a.step.bridge<=0,"Restarted seated intro at return swat");
                        }
                        check(Float.isFinite(delta)&&delta<scene.size*.27f,label+" root discontinuity "+delta+" in "+a.action);
                        maxTravel=Math.max(maxTravel,delta);rootSamples++;
                        if(a.clock==null)continue;
                        check(a.clip.spec.optInt("coat")==a.coat,label+" coat changed");int f=a.clock.frame();
                        check(f>=0&&f<a.clip.rectangles.length&&!a.bounds().isEmpty(),label+" missing held art");
                        if(a.action.equals("ball")||a.action.equals("ball-pounce")){
                            PlayObject miss=missed[i];miss.x=scene.width+scene.size*3;miss.y=a.ground();miss.contact(a,32,scene.size/110);
                            check(miss.hits==0,"Distant ball moved on missed swing");
                        }
                        shown.add(i+"/"+a.action);
                    }
                    check(simulated[0]<120000,label+" timed out "+scene.state()+" "+scene.actors[0].owner);
                }
                activity.preview.invalidate();
            });
            Thread.sleep(5);
        }
        check(finished[0],label+" did not recover: "+scene.state());
        android.util.Log.i("MotionParity",label+" complete");
        cases.put(new JSONObject().put("case",label).put("simulated_ms",simulated[0]).put("rendered",new JSONArray(shown)));
    }
    void matrix(int coat,int companion)throws Exception{
        configure(coat,companion);
        test.waitFor(()->scene.actors[0].idle!=null&&scene.actors[1].idle!=null,"Initial registration missing");
        test.runOnMainSync(()->scene.drag(1,0,-scene.size*1.1f));
        for(String action:MobileActions.all){
            int[] previous={scene.actors[0].ballHits,scene.actors[1].ballHits};
            test.runOnMainSync(()->{
                if(PetAppetite.food(action))scene.settings.prefs.edit().putFloat("appetite.0.amount",45).putFloat("appetite.1.amount",45).apply();
                scene.command(action);
            });
            sweep(coat+"/"+companion+"/"+action,()->!scene.actors[0].busy()&&!scene.actors[1].busy());
            for(int i=0;i<2;i++){
                PetScene.Actor a=scene.actors[i];check(a.finishedOwner.equals(action),"Missing completion "+action);
                if(action.startsWith("ball"))check(a.ballHits>previous[i],"No registered contact: "+a.coat+" "+action);
            }
        }
        for(String kind:MobileActions.social){long count=scene.socialCount;
            test.runOnMainSync(()->scene.social(kind,1));sweep(coat+"/pair/"+kind,()->scene.socialCount>count);}
        for(int actor=0;actor<2;actor++){
            PetScene.Actor a=scene.actors[actor];long landed=a.landings;
            test.runOnMainSync(()->{
                a.armFinger();float x=a.bounds().centerX(),y=a.bounds().centerY();long now=SystemClock.uptimeMillis();
                PetTouch touch=activity.preview.touch;
                check(touch.event(MotionEvent.ACTION_DOWN,1,x,y,now),"Cat touch failed");
                float dest=a.x<scene.width*.5f?scene.width-10:10;
                for(int n=0;n<20;n++)touch.event(MotionEvent.ACTION_MOVE,1,x+(dest-x)*n/19,y-30,now+15*n);
                touch.event(MotionEvent.ACTION_UP,1,dest,y-30,now+320);
            });
            sweep(coat+"/finger/"+actor,()->!a.busy());
            check(a.fingerMiss<scene.size*.38f,"Finger outside paw reach "+a.fingerMiss);
            check(a.finishedOwner.equals("finger")&&a.landings>landed,"Finger missed jump: "+a.coat);
            String frames=cases.getJSONObject(cases.length()-1).getJSONArray("rendered").toString();
            for(String name:new String[]{"run","cursor-pounce","punch"})check(frames.contains(name),"Finger missed "+name);
        }
        PetScene.Actor lead=scene.actors[0];long priorLandings=lead.landings;
        test.runOnMainSync(()->{
            // An actual drag places the cat on a lane with room on both sides.
            scene.drag(0,scene.width/2-lead.x,0);lead.beginFinger(scene.width-8,lead.ground()-scene.size*.65f);
        });
        sweep(coat+"/pursuit-start",()->lead.action.equals("run")&&lead.x>scene.width*.52f);
        test.runOnMainSync(()->{lead.fingerX=8;});
        sweep(coat+"/planted-reversal",()->lead.action.equals("turn")&&lead.age>100);
        test.runOnMainSync(()->{
            MotionClock held=lead.clock;
            for(int i=0;i<48;i++){lead.fingerX=lead.x+(i%2==0?-12:12);scene.tick(8);}
            check(held==lead.clock,"Nearby finger movement reset the planted turn");
            lead.fingerX=scene.width*.24f;lead.fingerDown=false;
        });
        sweep(coat+"/release-and-land",()->!lead.busy());
        check(lead.landings>priorLandings,"No landing after fast reversals");
        test.runOnMainSync(()->{scene.drag(0,scene.width*.25f-lead.x,0);
            lead.beginFinger(lead.x,lead.ground()-scene.size*.5f);});
        sweep(coat+"/near-start",()->lead.action.equals("cursor-pounce")&&lead.clock!=null&&lead.age<500);
        test.runOnMainSync(()->{lead.trackFinger(scene.width*.9f,lead.ground()-scene.size*.72f);lead.fingerDown=false;});
        sweep(coat+"/swipe-away-from-crouch",()->!lead.busy());
        String swipe=cases.getJSONObject(cases.length()-1).getJSONArray("rendered").toString();
        check(swipe.contains("run")&&swipe.contains("cursor-pounce")&&swipe.contains("punch"),"Realistic swipe failed to chase");
        view.recycle();
    }
    void live(int coat)throws Exception{
        configure(coat,coat==0?8:coat==8?10:0);
        long started=SystemClock.elapsedRealtime();long[] previousDraw={0};
        java.util.ArrayList<Long> intervals=new java.util.ArrayList<>();
        android.view.ViewTreeObserver.OnDrawListener observer=()->{
            long now=SystemClock.elapsedRealtime();
            if(scene.animating()&&previousDraw[0]>0)intervals.add(now-previousDraw[0]);
            previousDraw[0]=now;
        };
        test.runOnMainSync(()->activity.preview.getViewTreeObserver().addOnDrawListener(observer));
        test.runOnMainSync(()->{activity.preview.start();scene.social("paw",0);});
        long pairDeadline=SystemClock.elapsedRealtime()+85000;
        while(scene.socialCount==0&&SystemClock.elapsedRealtime()<pairDeadline){
            Thread.sleep(1500);
            test.runOnMainSync(()->android.util.Log.i("MotionPlayback","pair="+scene.socialPhase+" states="+scene.state()+
                " ages="+Math.round(scene.actors[0].age)+","+Math.round(scene.actors[1].age)+" frames="+activity.preview.renderedFrames));
        }
        check(scene.socialCount>0,"Live pair did not finish: "+scene.socialPhase+" "+scene.state()+" "+activity.preview.renderedFrames);
        test.runOnMainSync(()->{PetScene.Actor a=scene.actors[1];a.armFinger();
            PetTouch touch=activity.preview.touch;long now=SystemClock.uptimeMillis();
            touch.event(MotionEvent.ACTION_DOWN,1,a.bounds().centerX(),a.bounds().centerY(),now);
            float dest=a.x>scene.width/2?12:scene.width-12;
            touch.event(MotionEvent.ACTION_MOVE,1,dest,scene.height*.4f,now+120);
            touch.event(MotionEvent.ACTION_UP,1,dest,scene.height*.4f,now+240);
            scene.actors[0].request("ball-pounce");
        });
        long end=SystemClock.elapsedRealtime()+65000;
        while((scene.actors[0].busy()||scene.actors[1].busy())&&SystemClock.elapsedRealtime()<end)Thread.sleep(100);
        check(scene.actors[1].landings>0&&scene.actors[0].ballHits>0,"Live pursuit or ball contact missing");
        test.runOnMainSync(()->{scene.actors[0].request("tower");scene.actors[1].request("scratch");});
        Thread.sleep(16000);
        test.runOnMainSync(()->scene.automatic=true);
        while(SystemClock.elapsedRealtime()-started<64000)Thread.sleep(100);
        test.shot("live-end-"+coat);
        test.runOnMainSync(()->activity.preview.getViewTreeObserver().removeOnDrawListener(observer));
        java.util.Collections.sort(intervals);check(!intervals.isEmpty(),"No visible draws");
        cases.put(new JSONObject().put("coat",coat).put("live_frames",activity.preview.renderedFrames)
            .put("display_draws_sampled",intervals.size()).put("draw_interval_p95_ms",intervals.get((intervals.size()-1)*95/100))
            .put("wall_time_ms",SystemClock.elapsedRealtime()-started).put("pair_events",scene.socialCount).put("ball_hits",scene.actors[0].ballHits).put("finger_landings",scene.actors[1].landings));
    }
    void run(String mode,int coat)throws Exception{
        if(mode.equals("parity-live"))live(coat);else matrix(coat,coat==0?8:coat==8?10:0);
        JSONObject report=new JSONObject().put("mode",mode).put("coat",coat).put("passed",true).put("cases",cases)
            .put("root_samples",rootSamples).put("max_root_dp_per_32ms",maxTravel);
        File file=new File(test.directory,mode+"-"+coat+".json");
        try(FileOutputStream out=new FileOutputStream(file)){out.write(report.toString().getBytes(StandardCharsets.UTF_8));}
        Bundle b=new Bundle();b.putString("stream","PARITY_PASSED "+coat+" "+cases.length());test.finish(-1,b);
    }
}
