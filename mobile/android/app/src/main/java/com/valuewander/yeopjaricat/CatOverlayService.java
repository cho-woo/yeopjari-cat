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
    final ArrayList<ObjectWindow> objects=new ArrayList<>();
    WindowManager manager;
    PetScene scene;
    PetSettings settings;
    AlertDialog menuDialog;
    float density;
    final PetFrameLoop frames=new PetFrameLoop(this::drawFrame);
    final Runnable visibilityPoll=this::refreshVisibility;
    final java.util.concurrent.ExecutorService visibilityWorker=java.util.concurrent.Executors.newSingleThreadExecutor();
    long visibilityGeneration;
    boolean checkingHome,destroyed;
    boolean receiverRegistered;
    final Runnable homeChanged=()->{ensureScene();refreshVisibility();};
    final BroadcastReceiver screen=new BroadcastReceiver(){@Override public void onReceive(Context c,Intent i){
        if(Intent.ACTION_SCREEN_OFF.equals(i.getAction())){visibilityGeneration++;handler.removeCallbacks(visibilityPoll);hideWindows();return;}
        ensureScene();refreshVisibility();
    }};
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
        if(intent==null||!Settings.canDrawOverlays(this)||!settings.enabled()||!settings.overlay()){stopSelf();return START_NOT_STICKY;}
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CatOverlayService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notice=new Notification.Builder(this,"pet_overlay").setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("옆자리 고양이 터치 조작").setContentText(settings.overlay()?"다른 앱 위 표시 켜짐":"홈 화면에 고양이 표시 중")
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
        handler.removeCallbacks(visibilityPoll);hideWindows();windows.clear();objects.clear();
        scene=selected;
        resizeScene();
        for(int i=0;i<scene.actors.length;i++)if(scene.actors[i]!=null){objects.add(new ObjectWindow(i));windows.add(new PetWindow(this,i));}
    }
    @SuppressWarnings("deprecation") void resizeScene(){
        DisplayMetrics metrics=new DisplayMetrics();manager.getDefaultDisplay().getRealMetrics(metrics);density=metrics.density;
        float height=metrics.heightPixels/density;
        float bottom=height*(1-Math.max(30,Math.min(88,settings.prefs.getInt("home_floor_percent",58)))/100f);
        scene.resize(metrics.widthPixels/density,height,bottom);
        for(PetWindow w:windows)w.position();
    }
    boolean screenUnlocked(){
        return ((PowerManager)getSystemService(POWER_SERVICE)).isInteractive()
            && !((KeyguardManager)getSystemService(KEYGUARD_SERVICE)).isKeyguardLocked();
    }

    boolean available(){
        return MainActivity.visibleActivities==0 && !AppUpdates.installerOpen && !WallpaperReturn.pickerOpen && settings.enabled();
    }

    void refreshVisibility(){
        handler.removeCallbacks(visibilityPoll);
        long generation=++visibilityGeneration;
        if(destroyed||!running||scene==null)return;
        if(!available()){applyVisibility(false);return;}
        if(checkingHome)return;
        boolean overlayEnabled=settings.overlay();
        checkingHome=true;
        // Binder permission and keyguard checks run off the drawing thread.
        visibilityWorker.execute(()->{
            boolean allowed=Settings.canDrawOverlays(this);
            boolean visible=allowed && screenUnlocked() && overlayEnabled;
            handler.post(()->{
                checkingHome=false;
                if(destroyed||!running)return;
                if(generation!=visibilityGeneration){refreshVisibility();return;}
                if(!allowed){stopSelf();return;}
                applyVisibility(visible && available());
            });
        });
    }
    void applyVisibility(boolean show){
        if(show){
            if(!displaying)AppUpdates.get(this).check(false);
            for(ObjectWindow v:objects)if(!v.attached){try{manager.addView(v,v.params);v.attached=true;}catch(SecurityException e){stopSelf();return;}}
            for(PetWindow v:windows)if(!v.attached)try{v.position();manager.addView(v,v.params);v.attached=true;}catch(SecurityException e){stopSelf();return;}
            if(!displaying){displaying=!windows.isEmpty();if(displaying)frames.start();}
            handler.postDelayed(visibilityPoll,150);
        }else hideWindows();
    }
    void hideWindows(){
        frames.stop();displaying=false;if(menuDialog!=null){menuDialog.dismiss();menuDialog=null;}
        for(ObjectWindow v:objects)if(v.attached){manager.removeView(v);v.attached=false;}
        for(PetWindow v:windows){v.touch.cancel();if(v.attached){manager.removeView(v);v.attached=false;}}
    }
    void redraw(){for(ObjectWindow v:objects)if(v.attached)v.update();for(PetWindow v:windows)if(v.attached)v.update();}
    boolean drawFrame(double elapsed){
        if(!displaying||scene==null)return false;
        scene.tick(elapsed);
        try{redraw();}catch(SecurityException e){frames.stop();stopSelf();return false;}
        renderedFrames++;return scene.animating();
    }
    final class ObjectWindow extends View {
        final int index;
        final WindowManager.LayoutParams params;
        boolean attached;
        @android.annotation.SuppressLint("RtlHardcoded")
        ObjectWindow(int index){
            super(CatOverlayService.this);this.index=index;
            params=new WindowManager.LayoutParams(1,1,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE|
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
            params.gravity=Gravity.TOP|Gravity.LEFT;params.alpha=.72f;
            if(Build.VERSION.SDK_INT>=30)params.setFitInsetsTypes(0);setVisibility(GONE);
        }
        void update(){
            PlayObject prop=scene.actors[index].object;
            if(prop.kind.isEmpty()){if(getVisibility()!=GONE)setVisibility(GONE);return;}
            android.graphics.RectF b=prop.bounds(scene.size/110);
            int x=Math.max(0,(int)Math.floor(b.left*density)),y=Math.max(0,(int)Math.floor(b.top*density));
            int w=Math.max(1,(int)Math.ceil(b.right*density)-x),h=Math.max(1,(int)Math.ceil(b.bottom*density)-y);
            if(x!=params.x||y!=params.y||w!=params.width||h!=params.height){
                params.x=x;params.y=y;params.width=w;params.height=h;manager.updateViewLayout(this,params);
            }
            if(getVisibility()!=VISIBLE)setVisibility(VISIBLE);invalidate();
        }
        @Override protected void onDraw(Canvas canvas){int sc=canvas.save();canvas.scale(density,density);
            canvas.translate(-params.x/density,-params.y/density);scene.actors[index].object.draw(canvas,scene.size/110);canvas.restoreToCount(sc);}
    }
    final class PetWindow extends View {
        final int actor;
        final WindowManager.LayoutParams params;
        final PetTouch touch;
        boolean attached;
        @android.annotation.SuppressLint("RtlHardcoded")
        PetWindow(Context context,int actor){
            super(context);this.actor=actor;
            params=new WindowManager.LayoutParams(Math.round(160*density),Math.round(160*density),WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_SCALED|WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
            params.gravity=Gravity.TOP|Gravity.LEFT;if(Build.VERSION.SDK_INT>=30)params.setFitInsetsTypes(0);
            touch=new PetTouch(context,()->scene,index->showMenu(),()->{redraw();frames.wake();});
            setContentDescription("고양이. 끌어서 이동, 길게 눌러 놀이 또는 종료.");position();
        }
        void showMenu(){menuDialog=PetMenus.show(CatOverlayService.this,true,touch);}
        boolean wasMoving=true,measuredForPursuit;
        boolean usePursuitBuffer(PetScene.Actor cat){
            return !cat.busy()||cat.fingerArmed||"finger".equals(cat.owner);
        }
        float lastX,lastY,lastSize;
        MotionLibrary.Clip lastPose;
        int lastFrame,lastFacing;
        void update(){
            PetScene.Actor cat=scene.actors[actor];
            if(measuredForPursuit!=usePursuitBuffer(cat))requestLayout();
            boolean moving=cat.busy()||cat.controlled||cat.carryMs>0;
            float y=cat.footY();
            MotionLibrary.Clip pose=cat.clip!=null?cat.clip:cat.idle;
            int frame=cat.currentFrame(),facing=cat.clip!=null?cat.facing:cat.idleFacing;
            // A stationary authored hold needs just one buffer. Keep submitting every
            // moving, blended or newly registered pose, including the final idle pose.
            if(!isLayoutRequested() && cat.previousPose==null && moving==wasMoving && cat.x==lastX && y==lastY
                    && scene.size==lastSize && pose==lastPose && frame==lastFrame && facing==lastFacing)return;
            wasMoving=moving;lastX=cat.x;lastY=y;lastSize=scene.size;
            lastPose=cat.previousPose==null?pose:null;lastFrame=frame;lastFacing=facing;
            int x=params.x,top=params.y,w=params.width,h=params.height;position();
            if(x!=params.x||top!=params.y||w!=params.width||h!=params.height)manager.updateViewLayout(this,params);
            invalidate();
        }
        void position(){
            PetScene.Actor cat=scene.actors[actor];
            android.graphics.RectF b=cat.bounds(true);
            // Keep the whole clip's touch-window size. During a chase only its
            // visible pose (including crossfade) has to fit before a window move.
            android.graphics.RectF visible="finger".equals(cat.owner)?cat.bounds():b;
            float w=b.width()+12,h=b.height()+12;
            int wide=Math.round(w*density),high=Math.round(h*density);
            // Move the sprite inside its existing six-dp padding first. Repositioning
            // a launcher surface costs a WindowManager transaction and layout.
            if(!attached||params.width!=wide||Math.max(0,visible.left)*density<params.x
                    ||Math.min(scene.width,visible.right)*density>params.x+wide){
                params.x=Math.round(Math.max(0,Math.min(scene.width-w,b.left-6))*density);
            }
            if(!attached||params.height!=high||Math.max(0,visible.top)*density<params.y
                    ||Math.min(scene.height,visible.bottom)*density>params.y+high){
                params.y=Math.round(Math.max(0,Math.min(scene.height-h,b.top-6))*density);
            }
            params.width=wide;params.height=high;
        }
        @Override protected void onMeasure(int widthSpec,int heightSpec){
            // FLAG_SCALED lets the compositor fit this backing buffer to the real
            // touch window. Keep that window tight while consecutive poses change size.
            PetScene.Actor cat=scene.actors[actor];
            measuredForPursuit=usePursuitBuffer(cat);
            if(!measuredForPursuit){
                super.onMeasure(widthSpec,heightSpec);return;
            }
            setMeasuredDimension(Math.max(1,Math.round(scene.size*1.8f*density)),
                    Math.max(1,Math.round(scene.size*1.4f*density)));
        }
        @Override protected void onDraw(Canvas canvas){
            super.onDraw(canvas);canvas.save();
            canvas.scale(density*getMeasuredWidth()/params.width,density*getMeasuredHeight()/params.height);
            canvas.translate(-params.x/density,-params.y/density);scene.actors[actor].draw(canvas);
            canvas.restore();
        }
        @Override public boolean onTouchEvent(MotionEvent event){boolean result=touch.event(event.getActionMasked(),event.getPointerCount(),event.getRawX()/density,event.getRawY()/density,event.getEventTime());if(result)frames.wake();if(event.getActionMasked()==MotionEvent.ACTION_UP)performClick();return result;}
        @Override public boolean performClick(){super.performClick();return true;}
    }
    @Override public void onSharedPreferenceChanged(SharedPreferences prefs,String key){
        if(!running)return;
        if(!settings.enabled()||!settings.overlay()){stopSelf();return;}
        if(!key.startsWith("position.")){ensureScene();refreshVisibility();}
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);if(running&&scene!=null){resizeScene();refreshVisibility();}}
    @Override protected void dump(FileDescriptor fd,PrintWriter out,String[] args){super.dump(fd,out,args);out.println("cat_layer_running="+running+" displaying="+displaying+" cross_apps="+settings.overlay()+" home_touch=false"+" enabled="+settings.enabled()+" frames="+renderedFrames);if(scene!=null)for(int i=0;i<scene.actors.length;i++)if(scene.actors[i]!=null)out.println("actor="+i+" x="+scene.actors[i].x+" y="+scene.actors[i].footY()+" state="+scene.actors[i].action+" owner="+scene.actors[i].owner+" armed="+scene.actors[i].fingerArmed+" jumps="+scene.actors[i].landings+" pair="+scene.socialCount+" hits="+scene.actors[i].ballHits+" hitX="+scene.actors[i].bounds().centerX()+" hitY="+scene.actors[i].bounds().centerY());}
    @Override public void onDestroy(){
        destroyed=true;visibilityWorker.shutdownNow();running=false;handler.removeCallbacksAndMessages(null);hideWindows();windows.clear();objects.clear();HomeSceneStore.save();
        HomeSceneStore.unlisten(homeChanged);
        if(settings!=null){settings.prefs.unregisterOnSharedPreferenceChangeListener(this);settings.prefs.edit().putBoolean("overlay",false).putBoolean("touch_home",false).apply();}
        if(receiverRegistered)unregisterReceiver(screen);super.onDestroy();
    }
}
