package com.valuewander.yeopjaricat;

import android.app.AlertDialog;
import android.app.KeyguardManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;

public final class CatOverlayService extends Service implements SharedPreferences.OnSharedPreferenceChangeListener {
    static volatile boolean running,displaying;
    static volatile long renderedFrames;
    static final String STOP="com.valuewander.yeopjaricat.STOP_OVERLAY";
    final Handler handler=new Handler(Looper.getMainLooper());
    final ArrayList<PetWindow> windows=new ArrayList<>();
    WindowManager manager;
    PetScene scene;
    PetSettings settings;
    AlertDialog menuDialog;
    float density;
    long previous;
    boolean receiverRegistered;
    final Runnable homeChanged=()->{ensureScene();refreshVisibility();};
    final BroadcastReceiver screen=new BroadcastReceiver(){@Override public void onReceive(Context c,Intent i){ensureScene();refreshVisibility();}};
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onCreate(){
        super.onCreate();manager=(WindowManager)getSystemService(WINDOW_SERVICE);density=getResources().getDisplayMetrics().density;
        settings=new PetSettings(this);settings.prefs.registerOnSharedPreferenceChangeListener(this);
        ((NotificationManager)getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(new NotificationChannel("pet_overlay","고양이 터치 조작",NotificationManager.IMPORTANCE_LOW));
        HomeSceneStore.listen(homeChanged);
        IntentFilter filter=new IntentFilter();filter.addAction(Intent.ACTION_SCREEN_OFF);filter.addAction(Intent.ACTION_SCREEN_ON);filter.addAction(Intent.ACTION_USER_PRESENT);
        if(Build.VERSION.SDK_INT>=33)registerReceiver(screen,filter,Context.RECEIVER_NOT_EXPORTED);else registerReceiver(screen,filter);
        receiverRegistered=true;
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        if(intent!=null&&STOP.equals(intent.getAction())){PetLifecycle.stop(this);stopSelf();return START_NOT_STICKY;}
        if(intent==null||!Settings.canDrawOverlays(this)||!settings.enabled()||(!settings.overlay()&&!settings.homeTouch())){stopSelf();return START_NOT_STICKY;}
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CatOverlayService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notice=new Notification.Builder(this,"pet_overlay").setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("옆자리 고양이 터치 조작").setContentText(settings.overlay()?"다른 앱 위 표시 켜짐":"홈 화면 터치 조작 켜짐 · 다른 앱 위 표시 꺼짐")
            .setContentIntent(open).setOngoing(true).addAction(new Notification.Action.Builder(null,"종료하기",stop).build()).build();
        try{
            if(Build.VERSION.SDK_INT>=34)startForeground(100,notice,android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);else startForeground(100,notice);
            running=true;ensureScene();refreshVisibility();
        }catch(SecurityException|IllegalStateException e){stopSelf();}
        return START_NOT_STICKY;
    }
    void ensureScene(){
        if(!running)return;
        PetScene selected=HomeSceneStore.get(this);
        if(selected==scene&&scene!=null)return;
        handler.removeCallbacks(frame);hideWindows();windows.clear();
        scene=selected;
        resizeScene();
        for(int i=0;i<scene.actors.length;i++)if(scene.actors[i]!=null)windows.add(new PetWindow(this,i));
    }
    @SuppressWarnings("deprecation") void resizeScene(){
        DisplayMetrics metrics=new DisplayMetrics();manager.getDefaultDisplay().getRealMetrics(metrics);density=metrics.density;
        float height=metrics.heightPixels/density;
        float bottom=height*(1-Math.max(30,Math.min(88,settings.prefs.getInt("home_floor_percent",58)))/100f);
        scene.resize(metrics.widthPixels/density,height,bottom);
        for(PetWindow w:windows)w.position();
    }
    boolean canShow(){return MainActivity.visibleActivities==0&&!AppUpdates.installerOpen&&!WallpaperReturn.pickerOpen&&settings.enabled()&&((PowerManager)getSystemService(POWER_SERVICE)).isInteractive()&&!((KeyguardManager)getSystemService(KEYGUARD_SERVICE)).isKeyguardLocked()&&(settings.overlay()||(settings.homeTouch()&&HomeSceneStore.touchEligible()&&HomeVisibility.isHome(this)));}
    void refreshVisibility(){
        handler.removeCallbacks(frame);if(!running||scene==null)return;
        if(!Settings.canDrawOverlays(this)){stopSelf();return;}
        if(canShow()){
            for(PetWindow v:windows)if(!v.attached)try{v.position();manager.addView(v,v.params);v.attached=true;}catch(SecurityException e){stopSelf();return;}
            displaying=!windows.isEmpty();previous=SystemClock.uptimeMillis();handler.post(frame);
        }else{hideWindows();watchHome();}
    }
    void watchHome(){if(running&&settings.homeTouch()&&!settings.overlay()&&HomeSceneStore.visible)handler.postDelayed(frame,150);}
    void hideWindows(){
        displaying=false;if(menuDialog!=null){menuDialog.dismiss();menuDialog=null;}
        for(PetWindow v:windows){v.touch.cancel();if(v.attached){manager.removeView(v);v.attached=false;}}
    }
    void redraw(){for(PetWindow v:windows)if(v.attached){int x=v.params.x,y=v.params.y,w=v.params.width,h=v.params.height;v.position();if(x!=v.params.x||y!=v.params.y||w!=v.params.width||h!=v.params.height)manager.updateViewLayout(v,v.params);v.invalidate();}}
    final Runnable frame=new Runnable(){@Override public void run(){
        if(!running||scene==null)return;
        if(!Settings.canDrawOverlays(CatOverlayService.this)){stopSelf();return;}
        if(!canShow()){hideWindows();watchHome();return;}
        // A visibility/app-op watcher may resume without a wallpaper callback.
        // Reattach the native windows before advancing the shared scene again.
        if(!displaying){refreshVisibility();return;}
        ensureScene();long now=SystemClock.uptimeMillis();scene.tick(now-previous);previous=now;
        try{redraw();}catch(SecurityException e){stopSelf();return;}
        renderedFrames++;handler.postDelayed(this,scene.animating()?33:100);
    }};
    final class PetWindow extends View {
        final int actor;
        final WindowManager.LayoutParams params;
        final PetTouch touch;
        boolean attached;
        @android.annotation.SuppressLint("RtlHardcoded")
        PetWindow(Context context,int actor){
            super(context);this.actor=actor;
            params=new WindowManager.LayoutParams(Math.round(160*density),Math.round(160*density),WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
            params.gravity=Gravity.TOP|Gravity.LEFT;if(Build.VERSION.SDK_INT>=30)params.setFitInsetsTypes(0);
            touch=new PetTouch(context,()->scene,index->showMenu(),CatOverlayService.this::redraw);
            setContentDescription("고양이. 끌어서 이동, 길게 눌러 종료 메뉴.");position();
        }
        void showMenu(){menuDialog=PetMenus.show(CatOverlayService.this,true,touch);}
        void position(){android.graphics.RectF b=scene.actors[actor].bounds(true);float w=b.width()+12,h=b.height()+12;params.width=Math.round(w*density);params.height=Math.round(h*density);params.x=Math.round(Math.max(0,Math.min(scene.width-w,b.left-6))*density);params.y=Math.round(Math.max(0,Math.min(scene.height-h,b.top-6))*density);}
        @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);canvas.save();canvas.scale(density,density);canvas.translate(-params.x/density,-params.y/density);scene.actors[actor].draw(canvas);canvas.restore();}
        @Override public boolean onTouchEvent(MotionEvent event){boolean result=touch.event(event.getActionMasked(),event.getPointerCount(),event.getRawX()/density,event.getRawY()/density,event.getEventTime());if(event.getActionMasked()==MotionEvent.ACTION_UP)performClick();return result;}
        @Override public boolean performClick(){super.performClick();return true;}
    }
    @Override public void onSharedPreferenceChanged(SharedPreferences prefs,String key){
        if(!running)return;
        if(!settings.enabled()||(!settings.overlay()&&!settings.homeTouch())){stopSelf();return;}
        if(!key.startsWith("position.")){ensureScene();refreshVisibility();}
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);if(running&&scene!=null){resizeScene();refreshVisibility();}}
    @Override protected void dump(FileDescriptor fd,PrintWriter out,String[] args){super.dump(fd,out,args);out.println("cat_layer_running="+running+" displaying="+displaying+" cross_apps="+settings.overlay()+" home_touch="+settings.homeTouch()+" enabled="+settings.enabled()+" frames="+renderedFrames);if(scene!=null)for(int i=0;i<scene.actors.length;i++)if(scene.actors[i]!=null)out.println("actor="+i+" x="+scene.actors[i].x+" y="+scene.actors[i].footY()+" state="+scene.actors[i].action);}
    @Override public void onDestroy(){
        running=false;handler.removeCallbacksAndMessages(null);hideWindows();windows.clear();HomeSceneStore.save();
        HomeSceneStore.unlisten(homeChanged);
        if(settings!=null){settings.prefs.unregisterOnSharedPreferenceChangeListener(this);settings.prefs.edit().putBoolean("overlay",false).putBoolean("touch_home",false).apply();}
        if(receiverRegistered)unregisterReceiver(screen);super.onDestroy();
    }
}
