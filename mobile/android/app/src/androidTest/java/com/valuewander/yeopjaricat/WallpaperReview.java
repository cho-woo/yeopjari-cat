package com.valuewander.yeopjaricat;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.WallpaperColors;
import android.app.WallpaperInfo;
import android.app.WallpaperManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

final class WallpaperReview {
    final MotionSmoke test;final Context context;final File directory;
    final WallpaperManager manager;long commandId;boolean recreated;
    WallpaperReview(MotionSmoke t){test=t;context=t.getTargetContext();directory=new File(context.getExternalFilesDir(null),"wallpaper-review");directory.mkdirs();manager=WallpaperManager.getInstance(context);}
    void blue()throws Exception{Bitmap b=Bitmap.createBitmap(128,128,Bitmap.Config.ARGB_8888);b.eraseColor(FixtureWallpaperProvider.BLUE);manager.setBitmap(b,null,false,WallpaperManager.FLAG_SYSTEM);b.recycle();}
    void redLock()throws Exception{Bitmap b=Bitmap.createBitmap(128,128,Bitmap.Config.ARGB_8888);b.eraseColor(0xffad4555);manager.setBitmap(b,null,false,WallpaperManager.FLAG_LOCK);b.recycle();}
    void state()throws Exception{
        final JSONObject[] result={null};test.runOnMainSync(()->{try{WallpaperHistory h=WallpaperHistory.get(context);WallpaperHistory.Snapshot s=h.read("saved_");WallpaperInfo i=h.info();
            int color=0;if(Build.VERSION.SDK_INT>=27){WallpaperColors c=manager.getWallpaperColors(WallpaperManager.FLAG_SYSTEM);if(c!=null&&c.getPrimaryColor()!=null)color=c.getPrimaryColor().toArgb();}
            File f=h.file(s.file);result[0]=new JSONObject().put("id",h.id()).put("lock_id",manager.getWallpaperId(WallpaperManager.FLAG_LOCK)).put("cat",h.isCat()).put("component",i==null?"":i.getComponent().flattenToString()).put("phase",h.phase()).put("busy",h.busy).put("saved_kind",s.kind).put("saved_file",s.file).put("backup_present",h.available()).put("backup_bytes",f!=null&&f.isFile()?f.length():0).put("color",color).put("enabled",new PetSettings(context).enabled()).put("command",commandId).put("recreated_backup",recreated);
        }catch(Exception failure){throw new RuntimeException(failure);}});
        File temp=new File(directory,"state.tmp");Files.write(temp.toPath(),result[0].toString().getBytes(StandardCharsets.UTF_8));temp.renameTo(new File(directory,"state.json"));
    }
    void run()throws Exception{
        test.runOnMainSync(()->{new PetSettings(context).prefs.edit().putBoolean("overlay",false).putBoolean("touch_home",false).putBoolean("pets_enabled",true).apply();context.getSharedPreferences("wallpaper_history",Context.MODE_PRIVATE).edit().clear().apply();WallpaperHistory.instance=new WallpaperHistory(context);});
        blue();redLock();
        IntentFilter photos=new IntentFilter(Intent.ACTION_OPEN_DOCUMENT);photos.addCategory(Intent.CATEGORY_OPENABLE);photos.addDataType("image/*");
        Instrumentation.ActivityMonitor monitor=test.addMonitor(photos,new Instrumentation.ActivityResult(Activity.RESULT_OK,new Intent().setData(Uri.parse("content://com.valuewander.yeopjaricat.test.wallpaper-fixture/blue")).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)),true);
        test.activity=(MainActivity)test.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));state();
        if(test.arguments.getString("mode","").equals("wallpaper-return-auto")){new WallpaperAutoReview(this).run();test.removeMonitor(monitor);return;}
        Bundle ready=new Bundle();ready.putString("stream","READY_FOR_WALLPAPER_RETURN");test.sendStatus(1,ready);
        File command=new File(directory,"command.json");command.delete();
        for(int tick=0;tick<6000;tick++){
            if(command.isFile()){
                JSONObject input=new JSONObject(new String(Files.readAllBytes(command.toPath()),StandardCharsets.UTF_8));long id=input.getLong("id");
                if(id!=commandId){commandId=id;String action=input.getString("action");
                    if(action.equals("forget"))test.runOnMainSync(()->{WallpaperHistory h=WallpaperHistory.get(context);h.prefs.edit().clear().apply();h.notice="";h.cleanup();h.changed();});
                    if(action.equals("seed"))blue();
                    if(action.equals("lock"))redLock();
                    if(action.equals("recreate")){WallpaperHistory copy=new WallpaperHistory(context);recreated=copy.available()&&copy.read("saved_").file.equals(WallpaperHistory.get(context).read("saved_").file);copy.worker.shutdown();}
                    if(action.equals("finish")){test.removeMonitor(monitor);state();Bundle done=new Bundle();done.putString("stream","WALLPAPER_RETURN_DRIVER_FINISHED");test.finish(MotionSmoke.RESULT_OK,done);return;}
                }
            }
            state();Thread.sleep(200);
        }throw new AssertionError("Wallpaper return driver timed out");
    }
}
