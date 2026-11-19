package com.valuewander.yeopjaricat;

import android.app.Instrumentation.ActivityMonitor;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;

final class LauncherPlayReview {
    final MotionSmoke test;
    LauncherPlayReview(MotionSmoke test){this.test=test;}

    AccessibilityNodeInfo node(String text){
        AccessibilityNodeInfo root=test.getUiAutomation().getRootInActiveWindow();
        if(root==null)return null;
        for(AccessibilityNodeInfo n:root.findAccessibilityNodeInfosByText(text))
            if(text.equals(String.valueOf(n.getText())) || text.equals(String.valueOf(n.getContentDescription())))return n;
        return null;
    }
    void pickerButton(String label)throws Exception{
        test.waitFor(()->node(label)!=null,"Missing picker button "+label);
        AccessibilityNodeInfo n=node(label);
        while(n!=null&&!n.isClickable())n=n.getParent();
        test.check(n!=null&&n.performAction(AccessibilityNodeInfo.ACTION_CLICK),"Picker did not accept "+label);
    }
    void applyHome()throws Exception{
        android.app.WallpaperManager wm=android.app.WallpaperManager.getInstance(test.activity);
        Intent i=new Intent(android.app.WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER)
            .putExtra(android.app.WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,new ComponentName(test.activity,CatWallpaperService.class));
        test.runOnMainSync(()->test.activity.startActivity(i));
        pickerButton("Set wallpaper");
        test.waitFor(()->node("Home screen")!=null || (wm.getWallpaperInfo()!=null &&
            wm.getWallpaperInfo().getServiceName().equals(CatWallpaperService.class.getName())),"Missing destination");
        if(node("Home screen")!=null)pickerButton("Home screen");
        test.shell("input keyevent KEYCODE_HOME");
        test.waitFor(()->CatWallpaperService.wallpaperVisible,"Live wallpaper did not become visible");
        test.waitFor(()->{
            AccessibilityNodeInfo root=test.getUiAutomation().getRootInActiveWindow();
            return root!=null && "com.google.android.apps.nexuslauncher".contentEquals(root.getPackageName());
        },"Launcher is not focused");
        if(node("All apps")!=null){test.shell("input keyevent KEYCODE_BACK");test.shell("input keyevent KEYCODE_HOME");}
        test.waitFor(()->node("All apps")==null,"Launcher is still showing its drawer");
        SystemClock.sleep(1000);
    }
    void event(int action,float x,float y,long down){
        MotionEvent e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,x,y,0);
        e.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);test.sendPointerSync(e);e.recycle();
    }
    void playground()throws Exception{
        ActivityMonitor monitor=test.addMonitor(PlayActivity.class.getName(),null,false);
        test.runOnMainSync(()->test.activity.homePlay.performClick());
        PlayActivity play=(PlayActivity)test.waitForMonitorWithTimeout(monitor,6000);test.removeMonitor(monitor);
        test.check(play!=null,"Playground did not open");
        SceneView view=play.play;PetScene s=view.scene;
        test.check(s==HomeSceneStore.get(play),"Playground is disconnected from Home");
        test.runOnMainSync(()->{s.automatic=false;s.cancelSocial();for(PetScene.Actor a:s.actors)if(a!=null){a.cancelStory();a.fingerArmed=false;}});
        test.waitFor(()->view.backdrop.bitmap!=null&&view.hasWindowFocus(),"Interactive background not ready");
        int[] origin=new int[2];view.getLocationOnScreen(origin);float d=view.density();
        PetScene.Actor a=s.actors[0];RectF body=a.bounds();
        float px=origin[0]+body.centerX()*d,py=origin[1]+body.centerY()*d;
        float dx=(a.x<s.width*.5f?1:-1)*s.width*.18f,dy=-s.height*.18f;
        float expectedX=a.safeX(a.x+dx),expectedY=a.safeY(a.footY()+dy);
        Bitmap before=test.getUiAutomation().takeScreenshot();
        long down=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,px,py,down);
        for(int i=1;i<=10;i++){SystemClock.sleep(33);event(MotionEvent.ACTION_MOVE,px+dx*d*i/10,py+dy*d*i/10,down);}
        event(MotionEvent.ACTION_UP,px+dx*d,py+dy*d,down);
        test.check(Math.abs(a.x-expectedX)<1 && Math.abs(a.footY()-expectedY)<1,"Drag did not place the selected cat");
        float saved=s.settings.prefs.getFloat("position.home.0.x",-1);
        test.check(Math.abs(saved-a.x/s.width)<.001,"New home position was not saved");
        Bitmap after=test.getUiAutomation().takeScreenshot();
        int diffs=0;
        for(int y=before.getHeight()/7;y<before.getHeight()/5;y+=2)for(int x=before.getWidth()/8;x<before.getWidth()/3;x+=2)
            if(before.getPixel(x,y)!=after.getPixel(x,y))diffs++;
        before.recycle();after.recycle();test.check(diffs==0,"Background slid while dragging: "+diffs);
        test.shot("playground-drag");
        down=SystemClock.uptimeMillis();px=origin[0]+s.width*.52f*d;py=origin[1]+s.height*.80f*d;
        event(MotionEvent.ACTION_DOWN,px,py,down);
        for(int i=1;i<=10;i++){SystemClock.sleep(30);event(MotionEvent.ACTION_MOVE,px,py-s.height*.44f*d*i/10,down);}
        event(MotionEvent.ACTION_UP,px,py-s.height*.44f*d,down);
        test.check(play.hasWindowFocus() && (a.owner.equals("finger") || s.actors[1].owner.equals("finger")),"Upward play gesture escaped or missed both cats");
        test.runOnMainSync(()->{s.automatic=true;play.finish();});
        test.waitFor(()->!HomeSceneStore.inPlay,"Play did not hand Home back");
    }
    void run()throws Exception{
        test.runOnMainSync(()->new PetSettings(test.activity).prefs.edit().putBoolean("pets_enabled",true)
            .putBoolean("overlay",false).putBoolean("companion",true).apply());
        playground();applyHome();
        PetScene launcher=HomeSceneStore.get(test.activity);
        test.runOnMainSync(()->{
            launcher.automatic=false;launcher.cancelSocial();
            for(PetScene.Actor a:launcher.actors)if(a!=null)a.cancelStory();
            PetScene.Actor other=launcher.actors[1];
            launcher.drag(1,launcher.width*.81f-other.x,launcher.height*.37f-other.footY());
            PetScene.Actor a=launcher.actors[0];
            launcher.drag(0,launcher.width*.46f-a.x,launcher.height*.55f-a.footY());
        });
        PetScene.Actor first=launcher.actors[0];float rootX=first.x,rootY=first.footY();
        float density=test.activity.getResources().getDisplayMetrics().density;RectF hit=first.bounds();
        int x=Math.round(hit.centerX()*density),y=Math.round(hit.centerY()*density);
        test.check(launcher.hit(x/density,y/density)==0,"Fixture did not isolate the touched cat");
        SystemClock.sleep(300);
        test.shot("launcher-before-tap");
        test.shell("input tap "+x+" "+y);
        try{test.waitFor(()->first.owner.equals("touch-gentle")||first.finishedOwner.equals("touch-gentle"),"Home petting was lost");}
        catch(AssertionError e){
            test.shot("launcher-tap-failed");
            throw new AssertionError("Home tap at "+x+","+y+" density "+density+" scene "+launcher.width+"x"+launcher.height+" owners=["+first.owner+","+launcher.actors[1].owner+"] visible="+HomeSceneStore.visible+" play="+HomeSceneStore.inPlay+" overlay="+CatOverlayService.displaying,e);
        }
        test.shell("input swipe "+x+" "+y+" "+x+" "+(y-630)+" 410");SystemClock.sleep(350);
        test.check(Math.abs(first.x-rootX)<.01 && Math.abs(first.footY()-rootY)<.01,"Launcher swipe dragged the cat");
        test.runOnMainSync(()->launcher.automatic=true);
        test.waitFor(()->node("All apps")!=null || node("Search web and more")!=null,"Launcher did not receive upward navigation");
        PetScene s=HomeSceneStore.get(test.activity);
        for(PetScene.Actor a:s.actors)if(a!=null)test.check(!a.fingerDown&&!a.owner.equals("finger"),"Launcher swipe armed a chase");
        test.shot("launcher-swipe");test.shell("input keyevent KEYCODE_HOME");
        int[] coats={0,8,10};JSONArray rows=new JSONArray();
        for(int round=0;round<2;round++)for(int i=0;i<3;i++){
            int coat=coats[i],other=coats[(i+1)%3];
            test.runOnMainSync(()->new PetSettings(test.activity).prefs.edit().putInt("coat",coat).putInt("companion_coat",other).apply());
            test.waitFor(()->CatWallpaperService.wallpaperVisible && HomeSceneStore.get(test.activity).actors[0].coat==coat,"Wrong home coat");
            PetScene home=HomeSceneStore.get(test.activity);AutomaticReview.Meter[] m={new AutomaticReview.Meter(),new AutomaticReview.Meter()};
            long start=SystemClock.uptimeMillis(),draws=CatWallpaperService.renderedFrames;
            while(SystemClock.uptimeMillis()-start<40000){
                test.runOnMainSync(()->{long t=SystemClock.uptimeMillis()-start+1;
                    for(int role=0;role<2;role++)m[role].sample(home,home.actors[role],t);});
                SystemClock.sleep(55);
            }
            long presentations=CatWallpaperService.renderedFrames-draws;
            test.check(presentations>400,"Home stopped painting");
            for(int role=0;role<2;role++)rows.put(m[role].result().put("coat",role==0?coat:other)
                .put("role",role).put("round",round+1).put("home_draws",presentations));
            Files.write(new File(test.directory,"autoplay-launcher.json").toPath(),rows.toString(2).getBytes(StandardCharsets.UTF_8));
            Bundle progress=new Bundle();progress.putString("stream","HOME "+coat+"/"+other+" "+m[0].result().getDouble("playing_percent")+"/"+m[1].result().getDouble("playing_percent"));test.sendStatus(1,progress);
        }
        AutomaticReview.checkMinimum(test,rows);
        Bundle b=new Bundle();b.putString("stream","LAUNCHER_PLAY_PASSED");test.finish(MotionSmoke.RESULT_OK,b);
    }
}
