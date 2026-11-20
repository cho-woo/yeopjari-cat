package com.valuewander.yeopjaricat;

import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import org.json.JSONArray;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.io.File;

final class TouchMoodReview {
    final MotionSmoke test;
    final TouchReview input;
    TouchMoodReview(MotionSmoke test){
        this.test=test;input=new TouchReview(test,test.activity,test.directory);
    }
    void tap(int role)throws Exception{
        float[] xy=input.point(role);
        long now=SystemClock.uptimeMillis();
        input.event(MotionEvent.ACTION_DOWN,xy[0],xy[1],now);
        input.event(MotionEvent.ACTION_UP,xy[0],xy[1],now);
        Thread.sleep(85);
    }
    void run()throws Exception{
        // Six old taps cannot accumulate, and rough play needs quiet before rearming.
        TouchMood mood=new TouchMood();
        for(int i=0;i<10;i++)test.check(mood.tap(i*3200)==TouchMood.GENTLE,"Spaced pokes accumulated");
        for(int i=0;i<6;i++)test.check(mood.tap(40000+i*150)==(i<2?1:i<5?2:3),"Burst threshold");
        test.check(mood.tap(45000)==0 && mood.tap(49900)==0 && mood.tap(56000)==1,"Quiet-time reset");
        JSONArray results=new JSONArray();
        for(int coat:new int[]{0,8,10}){
            test.runOnMainSync(()->{
                new PetSettings(test.activity).prefs.edit().putBoolean("pets_enabled",true).putBoolean("companion",true)
                    .putInt("coat",coat).putInt("companion_coat",coat).apply();
                test.activity.preview.reload();test.activity.preview.scene.automatic=false;
            });
            input.idle();
            for(int role=0;role<2;role++){
                PetScene scene=test.activity.preview.scene;
                PetScene.Actor actor=scene.actors[role],other=scene.actors[1-role];
                float startX=actor.x, floor=actor.footY();
                tap(role);
                test.check(actor.owner.equals("touch-gentle"),"Gentle reaction missing");
                tap(role);tap(role);
                test.waitFor(()->actor.owner.equals("touch-warning") && actor.clock!=null,"Glance missing");
                tap(role);tap(role);
                int glance=actor.currentFrame();
                tap(role);
                test.check(actor.owner.equals("touch-angry") && actor.currentFrame()>=glance,"Escalation rewound");
                MotionClock anger=actor.clock;
                test.waitFor(()->actor.currentFrame()>=45,"Hiss did not advance");
                float[] xy=input.point(role);long down=SystemClock.uptimeMillis();int before=actor.currentFrame();
                input.event(MotionEvent.ACTION_DOWN,xy[0],xy[1],down);Thread.sleep(260);
                test.check(actor.clock==anger && actor.currentFrame()>before,"Pointer down froze the face");
                input.event(MotionEvent.ACTION_UP,xy[0],xy[1],down);
                test.check(actor.clock==anger,"Extra tap restarted hiss");
                test.waitFor(()->actor.currentFrame()>=74,"Hiss peak missing");
                RectF bounds=actor.bounds();int[] origin=new int[2];test.activity.preview.getLocationOnScreen(origin);
                float density=test.activity.preview.density();
                test.shot("touch-mood-"+coat+"-"+role);
                test.waitFor(()->actor.finishedOwner.equals("touch-angry") && !actor.busy(),"Seated recovery missing");
                test.check(Math.abs(actor.x-startX)<.01 && Math.abs(actor.footY()-floor)<.01,"Reaction moved the root");
                test.check(other.owner.isEmpty(),"Second actor copied the mood");
                results.put(new JSONObject().put("coat",coat).put("role",role).put("handoff_frame",glance)
                    .put("recovery_frame",actor.currentFrame()).put("screen_center_x",origin[0]+bounds.centerX()*density)
                    .put("screen_center_y",origin[1]+bounds.centerY()*density).put("size_px",scene.size*density));
            }
        }
        test.runOnMainSync(()->{test.activity.preview.reload();test.activity.preview.scene.automatic=false;});
        input.idle();
        PetScene.Actor actor=test.activity.preview.scene.actors[0];
        tap(0);tap(0);tap(0);
        test.waitFor(()->actor.finishedOwner.equals("touch-warning") && !actor.busy(),"Isolated glance did not settle");
        test.check(actor.currentFrame()==0,"Glance did not return to its source pose");
        Thread.sleep(3100);
        test.runOnMainSync(()->actor.request("jump"));
        for(int i=0;i<6;i++)tap(0);
        test.waitFor(()->actor.owner.equals("touch-angry") && actor.clock!=null,"Pokes during jump were lost");
        test.check(actor.landings==1 && Math.abs(actor.lift)<.01,"Reaction interrupted the landing");
        test.waitFor(()->actor.finishedOwner.equals("touch-angry") && !actor.busy(),"Deferred reaction failed to settle");
        Files.write(new File(test.directory,"touch-moods.json").toPath(),results.toString(2).getBytes(StandardCharsets.UTF_8));
        Bundle b=new Bundle();b.putString("stream","TOUCH_MOODS_PASSED "+results);test.finish(MotionSmoke.RESULT_OK,b);
    }
}
