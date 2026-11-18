package com.valuewander.yeopjaricat;

import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.os.SystemClock;
import android.os.Trace;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.inspector.WindowInspector;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** One first gesture per OS process, using the production launcher windows and Android input dispatcher. */
final class ColdJumpReview {
    final MotionSmoke test;
    final int coat;
    final CountDownLatch firstDraw=new CountDownLatch(1),done=new CountDownLatch(1);
    final long[][] draws=new long[2000][10];
    final String[] states=new String[draws.length],loads=new String[draws.length];
    int count;
    long started,gestureNs,baselineLandings;
    final float[] path=new float[4];
    boolean armed,ran,pounced,struck,geometry,frozen;
    final CountDownLatch apex=new CountDownLatch(1);
    View window;
    PetScene scene;
    ColdJumpReview(MotionSmoke test,int coat){this.test=test;this.coat=coat;}

    final ViewTreeObserver.OnDrawListener observer=()->{
        if(count>=draws.length)throw new AssertionError("Draw buffer full");
        PetScene.Actor a=scene.actors[0];
        long[] row=draws[count];
        row[0]=SystemClock.elapsedRealtimeNanos();row[1]=window.getDrawingTime()*1_000_000L;
        row[2]=a.currentFrame();row[3]=Math.round(a.lift*100);
        row[4]=window.getWidth();row[5]=window.getHeight();row[6]=Math.round(a.x*100);
        row[7]=a.landings;row[8]=a.clip==null?-1:a.clip.page[a.currentFrame()];
        row[9]=a.clock==null?-1:a.clock.step();
        states[count]=a.action;loads[count]=a.loading?a.loadingAction:"";
        Trace.beginSection("ColdJumpDraw "+count);Trace.endSection();count++;
        firstDraw.countDown();
        if(!armed)return;
        if(a.action.equals("run"))ran=true;
        if(ran && a.action.equals("cursor-pounce") && a.lift>0)pounced=true;
        if(geometry && !frozen && ran && a.action.equals("cursor-pounce") && a.lift>scene.size*.16f){
            a.controlled=true;frozen=true;apex.countDown();
        }
        if(pounced && a.action.equals("punch"))struck=true;
        if(struck && a.landings>baselineLandings && !a.busy())done.countDown();
    };
    void run()throws Exception{
        started=SystemClock.elapsedRealtimeNanos();geometry="true".equals(test.arguments.getString("geometry"));
        String pkg=test.getTargetContext().getPackageName();
        new PetSettings(test.getTargetContext()).prefs.edit().putInt("coat",coat)
            .putInt("companion_coat",coat==0?8:coat==8?10:0).putBoolean("companion",true)
            .putBoolean("pets_enabled",true).putBoolean("overlay",false).putBoolean("touch_home",true)
            .putFloat("position.home.0.x",.22f).putFloat("position.home.0.y",.60f)
            .putFloat("position.home.1.x",.78f).putFloat("position.home.1.y",.42f).commit();
        test.activity=(MainActivity)test.startActivitySync(new Intent(test.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        test.runOnMainSync(()->test.activity.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME)));
        long deadline=SystemClock.uptimeMillis()+30000;
        while(window==null && SystemClock.uptimeMillis()<deadline){
            test.runOnMainSync(()->{
                for(View v:WindowInspector.getGlobalWindowViews()){
                    if(!(v instanceof CatOverlayService.PetWindow)||((CatOverlayService.PetWindow)v).actor!=0||!v.isAttachedToWindow())continue;
                    window=v;scene=HomeSceneStore.get(test.activity);scene.automatic=false;
                    window.getViewTreeObserver().addOnDrawListener(observer);window.invalidate();break;
                }
            });
            if(window==null)SystemClock.sleep(12);
        }
        test.check(window!=null && firstDraw.await(5,TimeUnit.SECONDS),"No launcher drawing");
        try{
            if(geometry)capturePose("idle");
            test.runOnMainSync(()->{
                PetScene.Actor a=scene.actors[0];
                baselineLandings=a.landings;test.check(baselineLandings==0,"Already played a jump");
                a.armFinger();armed=true;path[0]=a.bounds().centerX();path[1]=a.bounds().centerY();
                path[2]=scene.width*.87f;path[3]=a.ground()-scene.size*.95f;
            });
            float density=test.activity.getResources().getDisplayMetrics().density;
            long down=SystemClock.uptimeMillis();gestureNs=SystemClock.elapsedRealtimeNanos();
            for(int i=0;i<=13;i++){
                long target=down+Math.min(i,12)*25L;
                long wait=target-SystemClock.uptimeMillis();if(wait>0)SystemClock.sleep(wait);
                float f=Math.min(1,i/12f);
                MotionEvent ev=MotionEvent.obtain(down,SystemClock.uptimeMillis(),i==0?MotionEvent.ACTION_DOWN:i==13?MotionEvent.ACTION_UP:MotionEvent.ACTION_MOVE,
                    (path[0]+f*(path[2]-path[0]))*density,(path[1]+f*(path[3]-path[1]))*density,0);
                ev.setSource(InputDevice.SOURCE_TOUCHSCREEN);
                try{test.check(test.getUiAutomation().injectInputEvent(ev,false),"Touch dispatch failed");}finally{ev.recycle();}
            }
            if(geometry){
                test.check(apex.await(20,TimeUnit.SECONDS),"Missing airborne pose");
                capturePose("jump");test.runOnMainSync(()->scene.actors[0].controlled=false);
            }
            test.check(done.await(45,TimeUnit.SECONDS),"Missing ordered chase, airborne jump, punch or recovery: "+scene.state());
        }finally{test.runOnMainSync(()->window.getViewTreeObserver().removeOnDrawListener(observer));}
        if("true".equals(test.arguments.getString("check-padding")))checkLauncherPadding();
        JSONObject report=new JSONObject().put("version",test.getTargetContext().getPackageManager().getPackageInfo(pkg,0).versionName)
            .put("coat",coat).put("pid",Process.myPid()).put("process_start_uptime_ms",Process.getStartUptimeMillis())
            .put("runner_boottime_ns",started).put("down_boottime_ns",gestureNs).put("ordered_landed_and_recovered",true);
        JSONArray rows=new JSONArray();
        for(int i=0;i<count;i++){
            JSONArray row=new JSONArray();row.put(states[i]).put(loads[i]);for(long v:draws[i])row.put(v);rows.put(row);
        }
        report.put("columns","action,waiting,boottime_ns,view_drawing_time_ns,frame,lift_centi_dp,width_px,height_px,x_centi_dp,landings,page,step").put("draws",rows);
        Files.write(new File(test.directory,"cold-jump.json").toPath(),report.toString().getBytes(StandardCharsets.UTF_8));
        Bundle b=new Bundle();b.putString("stream","COLD_JUMP_PASSED "+coat);test.finish(-1,b);
    }
    private void capturePose(String name)throws Exception{
        SystemClock.sleep(180);
        android.graphics.Bitmap screen=test.getUiAutomation().takeScreenshot();
        test.check(screen!=null,"No compositor screenshot");
        android.graphics.Bitmap reference=android.graphics.Bitmap.createBitmap(screen.getWidth(),screen.getHeight(),android.graphics.Bitmap.Config.ARGB_8888);
        test.runOnMainSync(()->{
            android.graphics.Canvas c=new android.graphics.Canvas(reference);
            float density=window.getResources().getDisplayMetrics().density;c.scale(density,density);
            scene.actors[0].draw(c);
        });
        try(java.io.FileOutputStream a=new java.io.FileOutputStream(new File(test.directory,name+"-screen.png"));
            java.io.FileOutputStream b=new java.io.FileOutputStream(new File(test.directory,name+"-reference.png"))){
            screen.compress(android.graphics.Bitmap.CompressFormat.PNG,100,a);
            reference.compress(android.graphics.Bitmap.CompressFormat.PNG,100,b);
        }finally{screen.recycle();reference.recycle();}
    }
    private void checkLauncherPadding()throws Exception{
        int[] point={-1,-1};
        test.runOnMainSync(()->{
            CatOverlayService.PetWindow view=(CatOverlayService.PetWindow)window;
            float density=view.getResources().getDisplayMetrics().density;
            android.graphics.RectF cat=scene.actors[0].bounds(true);
            int y=Math.round(cat.centerY()*density);
            int x=Math.round((cat.left-12)*density);
            if(x<2)x=Math.round((cat.right+12)*density);
            test.check(x>0&&x<test.activity.getResources().getDisplayMetrics().widthPixels,"Point outside display");
            point[0]=x;point[1]=y;
        });
        test.check(point[0]>=0,"No transparent test point inside the buffer");
        test.shell("input swipe "+point[0]+" "+point[1]+" "+point[0]+" "+point[1]+" 850");
        test.waitFor(()->{
            android.view.accessibility.AccessibilityNodeInfo root=test.getUiAutomation().getRootInActiveWindow();
            if(root==null)return false;
            return !root.findAccessibilityNodeInfosByText("Home settings").isEmpty()
                    || !root.findAccessibilityNodeInfosByText("Widgets").isEmpty();
        },"Launcher did not receive long press in transparent padding");
        test.shell("input keyevent KEYCODE_BACK");
    }
}
