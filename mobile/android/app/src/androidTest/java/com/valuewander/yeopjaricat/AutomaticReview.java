package com.valuewander.yeopjaricat;

import android.os.Bundle;
import android.os.SystemClock;
import android.view.ViewTreeObserver;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

/** Passive, wall-clock playback through the actual Android View. Also runs on the previous APK. */
final class AutomaticReview {
    final MotionSmoke test;
    AutomaticReview(MotionSmoke test){this.test=test;}
    static final String[] states={"playing","loading","partner","rest","other"};
    static final class Meter {
        long[] ms=new long[states.length];
        long lastAt;int lastState,changes,starts;
        MotionClock lastClock;
        String lastPose="",lastOwner="";
        final JSONArray events=new JSONArray();
        float lastX,lastY,maxRootStep;
        void sample(PetScene s,PetScene.Actor a,long at) {
            if(lastAt!=0)ms[lastState]+=at-lastAt;
            int state=a.clock!=null?0:a.loading?1:s.socialActive||!s.pendingSocial.isEmpty()?2:a.owner.isEmpty()?3:4;
            if(lastAt==0 || state!=lastState || !lastOwner.equals(a.owner))events.put(Arrays.asList(at,state,a.owner,a.action).toString());
            if(a.clock!=null){
                int f=a.currentFrame();MotionLibrary.Clip clip=a.clip;
                String pose=a.action+"/"+clip.page[f]+"/"+Arrays.toString(clip.rectangles[f])+"/"+Arrays.toString(clip.anchors[f])+"/"+a.facing;
                if(!pose.equals(lastPose)){changes++;lastPose=pose;}
                if(a.clock!=lastClock){
                    starts++;
                    if(lastAt!=0)maxRootStep=Math.max(maxRootStep,(float)Math.hypot(a.x-lastX,a.footY()-lastY));
                }
            }
            lastClock=a.clock;lastAt=at;lastState=state;lastOwner=a.owner;lastX=a.x;lastY=a.footY();
        }
        JSONObject result()throws Exception{
            JSONObject times=new JSONObject();long total=0;
            for(int i=0;i<ms.length;i++){times.put(states[i],ms[i]);total+=ms[i];}
            return new JSONObject().put("time_ms",times).put("playing_percent",ms[0]*100.0/total)
                .put("distinct_drawn_pose_changes",changes).put("clip_starts",starts)
                .put("largest_boundary_root_step_dp",maxRootStep).put("events",events);
        }
    }
    static void checkMinimum(MotionSmoke test,JSONArray rows)throws Exception{
        for(int i=0;i<rows.length();i++){
            JSONObject row=rows.getJSONObject(i);
            test.check(row.getDouble("playing_percent")>=80,"Autoplay below 80%: "+row);
            test.check(row.getInt("distinct_drawn_pose_changes")>40,"No visible pose variety: "+row);
        }
    }
    void run()throws Exception{
        int rounds=Integer.parseInt(test.arguments.getString("rounds","3"));
        int seconds=Integer.parseInt(test.arguments.getString("seconds","40"));
        int[] coats={0,8,10};
        SceneView view=test.activity.preview;
        String label=test.arguments.getString("sample","candidate");
        JSONArray rows=new JSONArray();
        for(int round=0;round<rounds;round++)for(int i=0;i<3;i++){
            int primary=coats[i],companion=coats[(i+1)%3],seed=20261119+round*107+i;
            Meter[] meters={new Meter(),new Meter()};
            test.runOnMainSync(()->{
                new PetSettings(test.activity).prefs.edit().putBoolean("pets_enabled",true).putBoolean("companion",true)
                    .putInt("coat",primary).putInt("companion_coat",companion).apply();
                view.reload();view.scene.random.setSeed(seed);
            });
            long start=SystemClock.uptimeMillis();
            ViewTreeObserver.OnDrawListener monitor=()->{
                long t=SystemClock.uptimeMillis()-start+1;
                for(int role=0;role<2;role++)meters[role].sample(view.scene,view.scene.actors[role],t);
            };
            test.runOnMainSync(()->view.getViewTreeObserver().addOnDrawListener(monitor));
            SystemClock.sleep(seconds*1000L);
            test.runOnMainSync(()->{view.getViewTreeObserver().removeOnDrawListener(monitor);monitor.onDraw();});
            for(int role=0;role<2;role++)rows.put(meters[role].result().put("coat",coats[(i+role)%3])
                .put("role",role).put("round",round+1).put("seed",seed));
            Files.write(new File(test.directory,"autoplay-"+label+".json").toPath(),rows.toString(2).getBytes(StandardCharsets.UTF_8));
            Bundle progress=new Bundle();progress.putString("stream",label+" round="+(round+1)+" coats="+primary+"/"+companion+" played="+meters[0].result().getDouble("playing_percent")+"/"+meters[1].result().getDouble("playing_percent"));test.sendStatus(1,progress);
        }
        if(!label.equals("before"))checkMinimum(test,rows);
        Bundle b=new Bundle();b.putString("stream","AUTOMATIC_REVIEW_FINISHED");test.finish(MotionSmoke.RESULT_OK,b);
    }
}
