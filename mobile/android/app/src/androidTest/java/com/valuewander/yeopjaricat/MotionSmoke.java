package com.valuewander.yeopjaricat;

import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Runs only in an isolated emulator install. No debug control endpoint ships. */
public final class MotionSmoke extends Instrumentation {
    MainActivity activity;
    JSONArray results=new JSONArray();
    File directory;
    Bundle arguments;
    @Override public void onCreate(Bundle args){super.onCreate(args);arguments=args;start();}
    void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    void shell(String command)throws Exception{try(ParcelFileDescriptor p=getUiAutomation().executeShellCommand(command);java.io.FileInputStream in=new java.io.FileInputStream(p.getFileDescriptor())){byte[] b=new byte[1024];while(in.read(b)!=-1){}}}
    void shot(String name)throws Exception{android.view.accessibility.AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();if(root!=null)check(root.findAccessibilityNodeInfosByText("isn't responding").isEmpty(),"System dialog obscures actual screen");Bitmap bitmap=getUiAutomation().takeScreenshot();check(bitmap!=null,"Screenshot missing");try(FileOutputStream out=new FileOutputStream(new File(directory,name+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,out);}bitmap.recycle();}
    void waitFor(java.util.function.BooleanSupplier condition,String message)throws Exception{long deadline=SystemClock.uptimeMillis()+20000;while(!condition.getAsBoolean()&&SystemClock.uptimeMillis()<deadline)Thread.sleep(80);check(condition.getAsBoolean(),message);}
    View named(View v,String name){if(v instanceof TextView&&((TextView)v).getText().toString().equals(name))return v;if(v instanceof ViewGroup){ViewGroup g=(ViewGroup)v;for(int i=0;i<g.getChildCount();i++){View found=named(g.getChildAt(i),name);if(found!=null)return found;}}return null;}
    void click(String name){runOnMainSync(()->{View v=named(activity.getWindow().getDecorView(),name);check(v!=null,"Missing control "+name);v.performClick();});}
    @Override public void onStart(){
        try{
            directory=new File(getTargetContext().getExternalFilesDir(null),"mobile-review");directory.mkdirs();
            if(arguments!=null&&arguments.getString("mode","").startsWith("wallpaper-return")){new WallpaperReview(this).run();return;}
            if(arguments!=null&&arguments.getString("mode","").startsWith("update")){new UpdateReview(this,directory).run(arguments.getString("mode"));return;}
            if(arguments!=null && "cold-jump".equals(arguments.getString("mode"))){
                new ColdJumpReview(this,Integer.parseInt(arguments.getString("coat","0"))).run();return;
            }
            activity=(MainActivity)startActivitySync(new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            waitForIdleSync();
            if(arguments!=null&&"snow-playback".equals(arguments.getString("mode"))){new SnowGraphicsReview(this).playback();return;}
            if(arguments!=null&&"snow-graphics".equals(arguments.getString("mode"))){new SnowGraphicsReview(this).run();return;}
            if(arguments!=null&&("pacing".equals(arguments.getString("mode"))||"pacing-home".equals(arguments.getString("mode")))){new MotionPacingReview(this).run(Integer.parseInt(arguments.getString("coat","0")),"pacing-home".equals(arguments.getString("mode")));return;}
            if(arguments!=null&&arguments.getString("mode","").startsWith("parity")){
                new ActionParityReview(this).run(arguments.getString("mode"),Integer.parseInt(arguments.getString("coat","0")));return;
            }
            if(arguments!=null&&"touch".equals(arguments.getString("mode"))){new TouchReview(this,activity,directory).run();Bundle b=new Bundle();b.putString("stream","TOUCH_REVIEW_PASSED");finish(RESULT_OK,b);return;}
            if(arguments!=null&&"wallpaper".equals(arguments.getString("mode"))){wallpaperChecks();return;}
            check(!new PetSettings(getTargetContext()).prefs.getBoolean("overlay",false),"Overlay must default OFF");
            check(!CatOverlayService.running,"Default must not start overlay service");
            check(!Settings.canDrawOverlays(getTargetContext()),"Fresh install must not request overlay permission");
            results.put(new JSONObject().put("default_home_only",true).put("overlay_service_running",false).put("overlay_permission_granted",false));
            for(int coat:new int[]{0,8,10}){
                final int c=coat;
                runOnMainSync(()->{new PetSettings(activity).prefs.edit().putInt("coat",c).putBoolean("companion",false).apply();activity.preview.reload();activity.preview.scene.automatic=false;});
                waitFor(()->activity.preview.scene.actors[0].idle!=null,"Idle not decoded");
                shot("coat-"+coat+"-idle");
                for(String action:new String[]{"walk","groom","knead","punch","sleep","settle"}){
                    runOnMainSync(()->activity.preview.scene.command(action));
                    waitFor(()->activity.preview.scene.actors[0].clock!=null,"Action did not start "+action);
                    long before=activity.preview.renderedFrames,start=SystemClock.uptimeMillis();
                    Thread.sleep(600);
                    if(action.equals("punch")){
                        // Rapid commands must queue once, never rewind the current punch movie.
                        final MotionClock[] first={null};final int[] frame={0};
                        runOnMainSync(()->{PetScene.Actor a=activity.preview.scene.actors[0];first[0]=a.clock;frame[0]=a.clock.frame();for(int i=0;i<80;i++)a.request("punch");check(a.clock==first[0]&&a.clock.frame()==frame[0],"Touch spam rewound source clock");a.queued=null;});
                    }
                    shot("coat-"+coat+"-"+action);
                    waitFor(()->activity.preview.scene.actors[0].clock==null,"Recovery did not finish "+action);
                    long rendered=activity.preview.renderedFrames-before;
                    check(rendered>10,"Normal app view did not render");
                    results.put(new JSONObject().put("coat",coat).put("role","primary").put("action",action).put("app_draws",rendered).put("wall_time_ms",SystemClock.uptimeMillis()-start).put("complete_recovery",true));
                }
                runOnMainSync(()->{new PetSettings(activity).prefs.edit().putBoolean("companion",true).putInt("companion_coat",c).apply();activity.preview.reload();activity.preview.scene.automatic=false;});
                waitFor(()->activity.preview.scene.actors[1]!=null&&activity.preview.scene.actors[1].idle!=null,"Companion not ready");
                runOnMainSync(()->activity.preview.scene.actors[1].request("punch"));
                waitFor(()->activity.preview.scene.actors[1].clock!=null,"Companion punch not started");Thread.sleep(1000);shot("coat-"+coat+"-companion");
                waitFor(()->activity.preview.scene.actors[1].clock==null,"Companion recovery incomplete");
                results.put(new JSONObject().put("coat",coat).put("role","companion").put("action","punch").put("complete_recovery",true));
                final float[] origin={0};
                runOnMainSync(()->{origin[0]=activity.preview.scene.actors[0].x;activity.preview.scene.command("walk");});
                waitFor(()->activity.preview.scene.actors[0].clock!=null,"Paired walk did not start");
                check(activity.preview.scene.actors[0].action.equals("walk"),"Two cats incorrectly prevent every stroll");
                waitFor(()->activity.preview.scene.actors[0].clock==null,"Paired stroll recovery incomplete");
                check(Math.abs(activity.preview.scene.actors[0].x-origin[0])>100,"Paired stroll made no progress");
                check(Math.abs(activity.preview.scene.actors[1].x-activity.preview.scene.actors[0].x)>activity.preview.scene.size,"Pair paths overlap");
                shot("coat-"+coat+"-paired-walk");
                results.put(new JSONObject().put("coat",coat).put("paired_stroll_no_overlap",true));
                runOnMainSync(()->activity.preview.scene.command("walk"));
                waitFor(()->activity.preview.scene.actors[0].clock!=null,"Paired stroll not started");
                runOnMainSync(()->check(activity.preview.scene.actors[0].action.equals("walk"),"Two-cat layout prevented all walking"));
                long pairUntil=SystemClock.uptimeMillis()+18000;
                while(SystemClock.uptimeMillis()<pairUntil&&activity.preview.scene.actors[0].clock!=null){
                    runOnMainSync(()->{PetScene s=activity.preview.scene;check(Math.abs(s.actors[0].x-s.actors[1].x)>=s.size*1.1f,"Paired paths collided");});Thread.sleep(40);
                }
                check(activity.preview.scene.actors[0].clock==null,"Paired stroll did not recover");
                results.put(new JSONObject().put("coat",coat).put("paired_walk_reserved",true));
            }
            // The optional path starts only after the user setting is enabled.
            shell("appops set com.valuewander.yeopjaricat SYSTEM_ALERT_WINDOW allow");shell("pm grant com.valuewander.yeopjaricat android.permission.POST_NOTIFICATIONS");
            click("다른 앱 위에 고양이 표시");waitFor(()->CatOverlayService.running,"Optional overlay not started");
            check(!CatOverlayService.displaying,"Native windows must not obscure this app's controls");
            shell("am start -a android.settings.SETTINGS");waitFor(()->CatOverlayService.displaying,"Optional overlay did not appear over another app");
            long before=CatOverlayService.renderedFrames;Thread.sleep(1500);check(CatOverlayService.renderedFrames>before,"Optional overlay did not animate");shot("optional-overlay");
            runOnMainSync(()->activity.startActivity(new Intent(activity,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)));waitFor(()->MainActivity.visibleActivities>0,"Settings did not return");
            click("다른 앱 위에 고양이 표시");waitFor(()->!CatOverlayService.running,"Overlay switch did not stop service");
            results.put(new JSONObject().put("overlay_explicit_opt_in",true).put("overlay_stop_switch",true));
            runOnMainSync(()->{new PetSettings(activity).prefs.edit().putInt("coat",0).putInt("companion_coat",8).putBoolean("companion",true).apply();activity.preview.reload();});
            Thread.sleep(1000);shot("settings-final");
            JSONObject report=new JSONObject().put("passed",true).put("scope","Android 15 emulator, actual SceneView elapsed-time playback; wallpaper lifecycle checked separately").put("results",results);
            try(FileOutputStream out=new FileOutputStream(new File(directory,"report.json"))){out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}
            Bundle b=new Bundle();b.putString("stream",report.toString());finish(RESULT_OK,b);
        }catch(Throwable error){Bundle b=new Bundle();b.putString("stream",android.util.Log.getStackTraceString(error));finish(RESULT_CANCELED,b);}
    }
    static final int RESULT_OK=-1,RESULT_CANCELED=0;
    void wallpaperChecks()throws Exception{
        runOnMainSync(()->activity.stopService(new Intent(activity,CatOverlayService.class)));
        // Starting instrumentation force-stops the target process and can reset
        // an already selected live wallpaper. Apply it through the system picker
        // after this runner is attached, rather than treating that reset as app behavior.
        Bundle ready=new Bundle();ready.putString("stream","READY_FOR_WALLPAPER_PICKER");sendStatus(1,ready);
        long deadline=SystemClock.uptimeMillis()+90000;
        while(!CatWallpaperService.wallpaperVisible&&SystemClock.uptimeMillis()<deadline)Thread.sleep(80);
        check(CatWallpaperService.wallpaperVisible,"Wallpaper not applied after runner attached");
        long before=CatWallpaperService.renderedFrames;Thread.sleep(2000);check(CatWallpaperService.renderedFrames>before+10,"Home wallpaper did not render");shot("home-wallpaper");
        check(!CatOverlayService.running,"Home must not require overlay service");
        shell("am start -a android.settings.SETTINGS");waitFor(()->!CatWallpaperService.wallpaperVisible,"Wallpaper stayed visible behind opaque app");Thread.sleep(1000);
        before=CatWallpaperService.renderedFrames;Thread.sleep(1500);check(CatWallpaperService.renderedFrames==before,"Hidden wallpaper kept rendering");shot("default-other-app");
        shell("input keyevent KEYCODE_HOME");waitFor(()->CatWallpaperService.wallpaperVisible,"Wallpaper did not resume on Home");
        shell("input keyevent KEYCODE_SLEEP");Thread.sleep(1500);before=CatWallpaperService.renderedFrames;Thread.sleep(1500);check(CatWallpaperService.renderedFrames==before,"Screen-off wallpaper kept rendering");
        shell("input keyevent KEYCODE_WAKEUP");shell("wm dismiss-keyguard");waitFor(()->CatWallpaperService.wallpaperVisible,"Wallpaper did not resume after unlock");
        results.put(new JSONObject().put("home_animated",true).put("no_overlay_service_by_default",true).put("opaque_app_pauses_rendering",true).put("screen_off_pauses_rendering",true).put("return_home_resumes",true));
        JSONObject report=new JSONObject().put("passed",true).put("scope","Actual Android 15 emulator wallpaper lifecycle").put("results",results);
        try(FileOutputStream out=new FileOutputStream(new File(directory,"wallpaper-report.json"))){out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}
        Bundle b=new Bundle();b.putString("stream",report.toString());finish(RESULT_OK,b);
    }
}
