package com.valuewander.yeopjaricat;

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
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import java.util.ArrayList;

public final class CatOverlayService extends Service implements SharedPreferences.OnSharedPreferenceChangeListener {
    static volatile boolean running;
    static volatile long renderedFrames;
    static final String STOP="com.valuewander.yeopjaricat.STOP_OVERLAY";
    final Handler handler=new Handler(Looper.getMainLooper());
    final ArrayList<PetWindow> windows=new ArrayList<>();
    WindowManager manager;
    PetScene scene;
    PetSettings settings;
    float density;
    long previous;
    boolean receiverRegistered;
    final BroadcastReceiver screen=new BroadcastReceiver(){@Override public void onReceive(Context c,Intent i){refreshVisibility();}};
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onCreate(){
        super.onCreate();manager=(WindowManager)getSystemService(WINDOW_SERVICE);density=getResources().getDisplayMetrics().density;
        settings=new PetSettings(this);settings.prefs.registerOnSharedPreferenceChangeListener(this);
        NotificationManager notifications=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        notifications.createNotificationChannel(new NotificationChannel("pet_overlay","다른 앱 위의 고양이",NotificationManager.IMPORTANCE_LOW));
        IntentFilter filter=new IntentFilter();filter.addAction(Intent.ACTION_SCREEN_OFF);filter.addAction(Intent.ACTION_SCREEN_ON);filter.addAction(Intent.ACTION_USER_PRESENT);
        if(Build.VERSION.SDK_INT>=33)registerReceiver(screen,filter,Context.RECEIVER_NOT_EXPORTED);else registerReceiver(screen,filter);
        receiverRegistered=true;
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        if(intent==null||STOP.equals(intent.getAction())||!Settings.canDrawOverlays(this)){stopSelf();return START_NOT_STICKY;}
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CatOverlayService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notice=new Notification.Builder(this,"pet_overlay").setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("옆자리 고양이가 함께 있어요").setContentText("다른 앱 위 표시 중 · 언제든 숨길 수 있어요")
            .setContentIntent(open).setOngoing(true).addAction(new Notification.Action.Builder(null,"고양이 숨기기",stop).build()).build();
        try {
            if(Build.VERSION.SDK_INT>=34)startForeground(100,notice,android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);else startForeground(100,notice);
            running=true;settings.prefs.edit().putBoolean("overlay",true).apply();reload();refreshVisibility();
        } catch(SecurityException|IllegalStateException e){stopSelf();}
        return START_NOT_STICKY;
    }
    void reload(){
        handler.removeCallbacks(frame);removeWindows();if(scene!=null)scene.close();
        scene=new PetScene(this,()->{for(PetWindow w:windows)w.invalidate();});
        android.util.DisplayMetrics metrics=getResources().getDisplayMetrics();
        scene.resize(metrics.widthPixels/density,metrics.heightPixels/density,48);
        for(int i=0;i<scene.actors.length;i++)if(scene.actors[i]!=null)windows.add(new PetWindow(this,i));
        previous=SystemClock.uptimeMillis();
    }
    boolean canShow(){return ((PowerManager)getSystemService(POWER_SERVICE)).isInteractive()&&!((KeyguardManager)getSystemService(KEYGUARD_SERVICE)).isKeyguardLocked();}
    void refreshVisibility(){
        handler.removeCallbacks(frame);if(!running||scene==null)return;
        if(!Settings.canDrawOverlays(this)){stopSelf();return;}
        if(canShow()) {
            for(PetWindow view:windows)if(!view.attached){try{manager.addView(view,view.params);view.attached=true;}catch(SecurityException e){stopSelf();return;}}
            previous=SystemClock.uptimeMillis();handler.post(frame);
        }else removeWindows();
    }
    void removeWindows(){for(PetWindow view:windows)if(view.attached){manager.removeView(view);view.attached=false;}}
    final Runnable frame=new Runnable(){public void run(){
        if(!Settings.canDrawOverlays(CatOverlayService.this)){stopSelf();return;}
        if(!running||scene==null||!canShow()){refreshVisibility();return;}
        long now=SystemClock.uptimeMillis();scene.tick(now-previous);previous=now;
        for(PetWindow view:windows)if(view.attached){int x=view.params.x,y=view.params.y;view.position();try{if(x!=view.params.x||y!=view.params.y)manager.updateViewLayout(view,view.params);}catch(SecurityException e){stopSelf();return;}view.invalidate();}
        renderedFrames++;handler.postDelayed(this,scene.animating()?33:250);
    }};
    final class PetWindow extends View {
        final int actor;
        final WindowManager.LayoutParams params;
        boolean attached;
        @android.annotation.SuppressLint("RtlHardcoded") // Scene coordinates keep a physical left origin in RTL.
        PetWindow(Context context,int actor){
            super(context);this.actor=actor;
            params=new WindowManager.LayoutParams(Math.round(160*density),Math.round(160*density),WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,PixelFormat.TRANSLUCENT);
            params.gravity=Gravity.TOP|Gravity.LEFT;setContentDescription("옆자리 고양이. 톡 누르면 반응해요.");position();
        }
        void position(){params.x=Math.round((scene.actors[actor].x-80)*density);params.y=Math.round((scene.floor-148)*density);}
        @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);canvas.save();canvas.scale(density,density);canvas.translate(80-scene.actors[actor].x,148-scene.floor);scene.actors[actor].draw(canvas);canvas.restore();}
        @Override public boolean onTouchEvent(MotionEvent event){if(event.getAction()==MotionEvent.ACTION_UP){scene.actors[actor].request("punch");performClick();}return true;}
        @Override public boolean performClick(){super.performClick();return true;}
    }
    @Override public void onSharedPreferenceChanged(SharedPreferences prefs,String key){if(!"overlay".equals(key)&&running){reload();refreshVisibility();}}
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);if(running){reload();refreshVisibility();}}
    @Override public void onDestroy(){running=false;handler.removeCallbacksAndMessages(null);removeWindows();if(scene!=null)scene.close();if(settings!=null){settings.prefs.unregisterOnSharedPreferenceChangeListener(this);settings.prefs.edit().putBoolean("overlay",false).apply();}if(receiverRegistered)unregisterReceiver(screen);super.onDestroy();}
}
