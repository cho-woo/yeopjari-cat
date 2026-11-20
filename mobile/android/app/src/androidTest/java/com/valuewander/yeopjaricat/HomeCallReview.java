package com.valuewander.yeopjaricat;

import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Delivers the gestures to the real launcher. Only the encounter setup uses scene state. */
final class HomeCallReview {
    final MotionSmoke test;
    final LauncherPlayReview launcher;
    final JSONArray cases=new JSONArray();
    final float density;
    HomeCallReview(MotionSmoke test){
        this.test=test;launcher=new LauncherPlayReview(test);
        density=test.activity.getResources().getDisplayMetrics().density;
    }
    void tap(float x,float y){
        // Submit a physical tap as one ordered pair. Waiting for the launcher to
        // become idle between DOWN and UP can turn it into a multi-second hold.
        long now=SystemClock.uptimeMillis();
        for(int action:new int[]{MotionEvent.ACTION_DOWN,MotionEvent.ACTION_UP}){
            MotionEvent event=MotionEvent.obtain(now,now,action,Math.round(x*density),Math.round(y*density),0);
            event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
            test.check(test.getUiAutomation().injectInputEvent(event,false),"Launcher input failed");
            event.recycle();
        }
    }
    void select(PetScene.Actor a)throws Exception{RectF b=a.bounds();tap(b.centerX(),b.centerY());}
    void setup(PetScene s,int role){
        test.runOnMainSync(()->{
            s.automatic=false;s.cancelSocial();for(PetScene.Actor a:s.actors){
                a.cancelStory();a.pendingMood=0;a.nextGroundMin=Float.NaN;
            }
            PetScene.Actor a=s.actors[role],b=s.actors[1-role];
            s.drag(role,s.width*.30f-a.x,s.height*.55f-a.ground());
            s.drag(1-role,s.width*.76f-b.x,s.height*.39f-b.ground());
        });
    }
    void trip(PetScene s,int coat,int role,int round)throws Exception{
        setup(s,role);
        PetScene.Actor a=s.actors[role],other=s.actors[1-role];
        float start=a.x,x=s.width*.73f,y=s.height*.64f,otherX=other.x,otherY=other.ground();
        SystemClock.sleep(220);select(a);SystemClock.sleep(200);tap(x,y);
        test.waitFor(()->a.owner.equals("home-call"),"Empty launcher tap did not call coat "+coat+" role "+role+" owner "+a.owner);
        int frames=0,lastFrame=-1;float maxStep=0,priorX=a.x,priorY=a.ground();boolean turned=false,redirected=false;
        long startTime=SystemClock.uptimeMillis();
        while(a.owner.equals("home-call") && SystemClock.uptimeMillis()-startTime<19000){
            float nowX=a.x,nowY=a.ground();
            maxStep=Math.max(maxStep,(float)Math.hypot(nowX-priorX,nowY-priorY));priorX=nowX;priorY=nowY;
            int frame=a.currentFrame();if(frame!=lastFrame){frames++;lastFrame=frame;}
            if(a.action.equals("turn"))turned=true;
            if(round==1 && !redirected && nowX>start+s.width*.19f && a.action.equals("run")){
                select(a);SystemClock.sleep(150);x=s.width*.17f;y=s.height*.48f;tap(x,y);redirected=true;
            }
            SystemClock.sleep(24);
        }
        test.check(a.finishedOwner.equals("home-call")&&!a.owner.equals("home-call"),"No full recovery "+a.action);
        float error=(float)Math.hypot(a.x-a.safeX(x),a.ground()-a.safeY(y));
        test.check(error<1 && frames>=8,"Bad arrival error="+error+" poses="+frames+" turn="+turned);
        test.check(round==0 || (redirected&&turned),"Missing planted turn");
        test.check(Math.abs(otherX-other.x)+Math.abs(otherY-other.ground())<.5f,"Unselected cat followed");
        cases.put(new JSONObject().put("coat",coat).put("role",role).put("round",round+1)
            .put("arrival_error_dp",(double)error).put("sampled_pose_changes",frames)
            .put("largest_sampled_step_dp",(double)maxStep).put("redirected",redirected).put("turn_seen",turned));
        Bundle update=new Bundle();update.putString("stream","Home call coat="+coat+" role="+role+" round="+(round+1)+" error="+error+" poses="+frames);test.sendStatus(1,update);
        Files.write(new File(test.directory,"home-calls.json").toPath(),cases.toString(2).getBytes(StandardCharsets.UTF_8));
    }
    void callDuringAutoplay(PetScene s,int role)throws Exception{
        PetScene.Actor a=s.actors[role];
        test.runOnMainSync(()->s.automatic=true);
        test.waitFor(()->a.clock!=null && !a.owner.equals("home-call"),"Autonomous action did not start");
        String interrupted=a.owner;
        // Choose an unoccupied launcher cell away from both rendered cats.
        float x=a.x>s.width*.5f?s.width*.27f:s.width*.72f,y=s.height*.56f;
        if(s.hit(x,y)>=0)y=s.height*.36f;
        test.check(s.hit(x,y)<0,"Destination occupied in autoplay fixture");
        select(a);SystemClock.sleep(100);tap(x,y);
        test.waitFor(()->a.owner.equals("home-call"),"Call missed during "+interrupted);
        test.waitFor(()->!a.owner.equals("home-call") && a.finishedOwner.equals("home-call"),"Autoplay interrupted the call");
        float error=(float)Math.hypot(a.x-a.safeX(x),a.ground()-a.safeY(y));
        test.check(error<1,"Autoplay arrival error "+error);
        cases.put(new JSONObject().put("coat",a.coat).put("role",role).put("autoplay",true)
            .put("interrupted_action",interrupted).put("arrival_error_dp",(double)error));
    }
    void run()throws Exception{
        test.runOnMainSync(()->new PetSettings(test.activity).prefs.edit().putBoolean("pets_enabled",true)
            .putBoolean("overlay",false).putBoolean("companion",true).apply());
        launcher.applyHome();int[] coats={0,8,10};
        for(int i=0;i<coats.length;i++){
            int main=coats[i],buddy=coats[(i+1)%coats.length];
            test.runOnMainSync(()->new PetSettings(test.activity).prefs.edit().putInt("coat",main).putInt("companion_coat",buddy).apply());
            test.waitFor(()->HomeSceneStore.get(test.activity).actors[0].coat==main && CatWallpaperService.wallpaperVisible,"Wrong scene");
            PetScene s=HomeSceneStore.get(test.activity);
            for(int round=0;round<2;round++)for(int role=0;role<2;role++)trip(s,role==0?main:buddy,role,round);
            for(int role=0;role<2;role++)callDuringAutoplay(s,role);
        }
        PetScene s=HomeSceneStore.get(test.activity);setup(s,0);
        PetScene.Actor a=s.actors[0];float x=a.x,y=a.ground();
        select(a);SystemClock.sleep(110);
        test.shell("input swipe 530 1620 530 600 340");
        test.waitFor(()->launcher.node("All apps")!=null || launcher.node("Search your phone and more")!=null,"App drawer did not open");
        test.shot("home-call-app-drawer");
        test.shell("input keyevent KEYCODE_HOME");SystemClock.sleep(600);
        tap(s.width*.63f,s.height*.65f);SystemClock.sleep(800);
        test.check(!a.owner.equals("home-call")&&Math.hypot(a.x-x,a.ground()-y)<1,"Swipe left selection armed");
        select(a);SystemClock.sleep(6400);
        tap(s.width*.65f,s.height*.62f);SystemClock.sleep(500);
        test.check(!a.owner.equals("home-call")&&Math.hypot(a.x-x,a.ground()-y)<1,"Expired selection moved cat");
        Files.write(new File(test.directory,"home-calls.json").toPath(),cases.toString(2).getBytes(StandardCharsets.UTF_8));
        test.runOnMainSync(()->s.automatic=true);
        Bundle end=new Bundle();end.putString("stream","HOME_CALL_PASSED");test.finish(MotionSmoke.RESULT_OK,end);
    }
}
