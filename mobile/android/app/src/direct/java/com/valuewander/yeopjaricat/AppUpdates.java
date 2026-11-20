package com.valuewander.yeopjaricat;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Update traffic contains public release requests only; pet settings are never uploaded. */
final class AppUpdates {
    static final String OPEN="com.valuewander.yeopjaricat.UPDATE";
    static volatile boolean installerOpen;
    static AppUpdates instance;
    enum State{IDLE,CHECKING,LATEST,AVAILABLE,DOWNLOADING,READY,ERROR}
    static void checkWhileVisible(Context context){get(context).check(false);}
    static synchronized AppUpdates get(Context context){if(instance==null)instance=new AppUpdates(context,new UpdateClient(context));return instance;}
    final Context context;final UpdateClient client;final SharedPreferences prefs;
    final Handler main=new Handler(Looper.getMainLooper());
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    final ArrayList<Runnable> listeners=new ArrayList<>();
    final String currentName;
    volatile State state=State.IDLE;volatile UpdatePolicy.Release release;volatile int percent;
    AppUpdates(Context context,UpdateClient client){
        this.context=context.getApplicationContext();this.client=client;prefs=context.getSharedPreferences("app_updates",Context.MODE_PRIVATE);
        String installed;try{installed=context.getPackageManager().getPackageInfo(context.getPackageName(),0).versionName;}catch(Exception unavailable){installed="0.0.0-preview";}currentName=installed;
        try{UpdatePolicy.Release cached=UpdateClient.json(prefs.getString("release",""));
            if(cached.version.compareTo(UpdatePolicy.version(currentName))>0){release=cached;state=State.AVAILABLE;
                if(prefs.getBoolean("ready",false))worker.execute(()->{try{client.verifyReady(cached);main.post(()->{if(release==cached){state=State.READY;changed();}});}catch(Exception invalid){prefs.edit().putBoolean("ready",false).apply();}});
            }else prefs.edit().remove("release").remove("ready").apply();
        }catch(Exception absent){prefs.edit().remove("release").remove("ready").apply();}
    }
    void listen(Runnable listener){listeners.add(listener);}
    void unlisten(Runnable listener){listeners.remove(listener);}
    void changed(){for(Runnable listener:new ArrayList<>(listeners))listener.run();}
    boolean busy(){return state==State.CHECKING||state==State.DOWNLOADING;}
    void check(boolean force){
        if(Looper.myLooper()!=Looper.getMainLooper()){main.post(()->check(force));return;}
        if(busy())return;long now=System.currentTimeMillis(),last=prefs.getLong("last_check",0);
        if(!force&&last<=now&&now-last<6*60*60*1000L)return;
        prefs.edit().putLong("last_check",now).apply();State previous=state;state=State.CHECKING;changed();
        worker.execute(()->{
            try{UpdatePolicy.Release found=client.latest(currentName);main.post(()->{
                boolean ready=found!=null&&release!=null&&release.sha256.equals(found.sha256)&&prefs.getBoolean("ready",false)&&client.file().isFile();
                release=found;
                try{if(found==null)prefs.edit().remove("release").putBoolean("ready",false).apply();else prefs.edit().putString("release",UpdateClient.json(found).toString()).putBoolean("ready",ready).apply();}catch(Exception invalid){release=null;}
                state=release==null?State.LATEST:(ready?State.READY:State.AVAILABLE);changed();if(release!=null)notifyAvailable(false);
            });}catch(Exception unavailable){main.post(()->{state=release!=null?(previous==State.READY?State.READY:State.AVAILABLE):State.ERROR;changed();});}
        });
    }
    void download(){
        if(release==null||busy())return;UpdatePolicy.Release selected=release;state=State.DOWNLOADING;percent=0;changed();
        worker.execute(()->{try{
            final int[] last={-1};client.download(selected,n->{int value=(int)(n*100/selected.bytes);if(value!=last[0]){last[0]=value;main.post(()->{percent=value;changed();});}});
            prefs.edit().putString("release",UpdateClient.json(selected).toString()).putBoolean("ready",true).apply();
            main.post(()->{state=State.READY;changed();notifyAvailable(true);});
        }catch(Exception invalid){prefs.edit().putBoolean("ready",false).apply();main.post(()->{state=State.AVAILABLE;changed();android.widget.Toast.makeText(context,"업데이트를 받지 못했어요. 연결을 확인하고 다시 눌러 주세요.",android.widget.Toast.LENGTH_LONG).show();});}});
    }
    void verifyForInstall(Runnable ready,Runnable failed){
        UpdatePolicy.Release selected=release;if(selected==null){failed.run();return;}
        worker.execute(()->{try{client.verifyReady(selected);main.post(ready);}catch(Exception invalid){prefs.edit().putBoolean("ready",false).apply();main.post(()->{state=State.AVAILABLE;changed();failed.run();});}});
    }
    void notifyAvailable(boolean ready){
        if(release==null)return;String version=release.version.name();if(!ready&&version.equals(prefs.getString("notified","")))return;
        NotificationManager manager=(NotificationManager)context.getSystemService(Context.NOTIFICATION_SERVICE);if(!manager.areNotificationsEnabled())return;
        if(android.os.Build.VERSION.SDK_INT>=33&&context.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED)return;
        manager.createNotificationChannel(new NotificationChannel("pet_updates","앱 업데이트",NotificationManager.IMPORTANCE_DEFAULT));
        Intent intent=new Intent(context,MainActivity.class).setAction(OPEN).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent open=PendingIntent.getActivity(context,301,intent,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notice=new Notification.Builder(context,"pet_updates").setSmallIcon(R.drawable.ic_notification).setContentTitle("고양이: 업데이트해줘!")
            .setContentText(ready?"새 버전 설치 준비가 끝났어요. 앱에서 확인해 주세요.":"새 버전 "+release.version.number()+"이 나왔어요. 앱에서 업데이트할 수 있어요.")
            .setContentIntent(open).setAutoCancel(true).setOnlyAlertOnce(true).build();
        try{manager.notify(101,notice);prefs.edit().putString("notified",version).apply();}catch(SecurityException denied){/* The in-app update card remains available without notifications. */}
    }
    static void installer(Context context,boolean open){installerOpen=open;HomeSceneStore.notifyLayer(context);}
}
