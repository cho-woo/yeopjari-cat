package com.valuewander.yeopjaricat;

import android.graphics.RectF;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashSet;

final class TouchReview {
    final MotionSmoke test;final MainActivity activity;final File folder;
    final JSONArray results=new JSONArray();
    TouchReview(MotionSmoke test,MainActivity activity,File folder){this.test=test;this.activity=activity;this.folder=folder;}
    void event(int action,float x,float y,long down){MotionEvent e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,x,y,0);e.setSource(InputDevice.SOURCE_TOUCHSCREEN);test.sendPointerSync(e);e.recycle();}
    float[] point(int role){final float[] p=new float[2];test.runOnMainSync(()->{int[] screen=new int[2];activity.preview.getLocationOnScreen(screen);RectF b=activity.preview.scene.actors[role].bounds();float d=activity.preview.density();p[0]=screen[0]+b.centerX()*d;p[1]=screen[1]+b.centerY()*d;});return p;}
    void idle()throws Exception{test.waitFor(()->activity.preview.scene.actors[0].idle!=null&&activity.preview.scene.actors[1]!=null&&activity.preview.scene.actors[1].idle!=null,"Two cats not loaded");}
    void run()throws Exception{
        for(int coat:new int[]{0,8,10}){
            test.runOnMainSync(()->{new PetSettings(activity).prefs.edit().putBoolean("pets_enabled",true).putBoolean("companion",true).putInt("coat",coat).putInt("companion_coat",coat).apply();activity.preview.reload();activity.preview.scene.automatic=false;});idle();
            for(int role=0;role<2;role++){
                int selected=role;final float[] before=new float[2];
                test.runOnMainSync(()->{before[0]=activity.preview.scene.actors[selected].x;before[1]=activity.preview.scene.actors[selected].footY();});
                float[] p=point(role);float dx=(role==0?32:-32)*activity.preview.density(),dy=-24*activity.preview.density();long down=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,p[0],p[1],down);
                for(int i=1;i<=6;i++){Thread.sleep(35);event(MotionEvent.ACTION_MOVE,p[0]+dx*i/6,p[1]+dy*i/6,down);}
                event(MotionEvent.ACTION_UP,p[0]+dx,p[1]+dy,down);Thread.sleep(150);
                test.check(Math.abs(activity.preview.scene.actors[role].x-before[0]-(role==0?32:-32))<2,"Drag X is not continuous");
                test.check(Math.abs(activity.preview.scene.actors[role].footY()-before[1]+24)<2,"Drag Y is not continuous: before="+before[1]+" after="+activity.preview.scene.actors[role].footY());
                test.check(activity.preview.menuDialog==null||!activity.preview.menuDialog.isShowing(),"Drag opened menu");
                test.shot("touch-c"+coat+"-role"+role+"-drag");
                p=point(role);down=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,p[0],p[1],down);Thread.sleep(700);
                test.waitFor(()->activity.preview.menuDialog!=null&&activity.preview.menuDialog.isShowing(),"Long press did not open exit menu");
                Thread.sleep(250); // Capture the settled dialog, not its entrance animation.
                test.shot("touch-c"+coat+"-role"+role+"-menu");event(MotionEvent.ACTION_UP,p[0],p[1],down);
                test.runOnMainSync(()->activity.preview.menuDialog.getListView().performItemClick(activity.preview.menuDialog.getListView().getChildAt(0),0,0));
                test.waitFor(()->!new PetSettings(activity).enabled(),"Exit did not disable pets");Thread.sleep(100);test.shot("touch-c"+coat+"-role"+role+"-exit");
                test.check(!CatOverlayService.running,"Exit left touch layer running");
                test.runOnMainSync(()->activity.power.performClick());test.check(new PetSettings(activity).enabled(),"Resume failed");
                results.put(new JSONObject().put("coat",coat).put("role",role).put("drag_2d",true).put("long_press_menu",true).put("global_exit",true).put("resume",true));
            }
            // Real elapsed-time playback, with both roles active. No synthetic fast-forward.
            test.runOnMainSync(()->{activity.preview.reload();});idle();
            long until=SystemClock.uptimeMillis()+35000;int[] active={0,0};int samples=0;
            @SuppressWarnings("unchecked") HashSet<MotionClock>[] clocks=new HashSet[]{new HashSet<>(),new HashSet<>()};
            while(SystemClock.uptimeMillis()<until){test.runOnMainSync(()->{for(int i=0;i<2;i++){MotionClock c=activity.preview.scene.actors[i].clock;if(c!=null){active[i]++;clocks[i].add(c);}}});samples++;Thread.sleep(80);}
            for(int role=0;role<2;role++){double ratio=active[role]/(double)samples;test.check(ratio>=.80,"Too much idle time: "+ratio);test.check(clocks[role].size()>=3,"Too few complete actions");results.put(new JSONObject().put("coat",coat).put("role",role).put("real_time_observation_ms",35000).put("playing_fraction",ratio).put("distinct_actions",clocks[role].size()));}
            test.shot("cadence-c"+coat);
        }
        Files.write(new File(folder,"touch-report.json").toPath(),new JSONObject().put("passed",true).put("scope","Actual app View on Android15 AVD; overlay/home layer checked separately").put("results",results).toString(2).getBytes(StandardCharsets.UTF_8));
    }
}
