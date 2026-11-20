package com.valuewander.yeopjaricat;

import android.app.WallpaperInfo;
import android.app.WallpaperManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Only explicit photo selections and visible live components are retained. Never reads system image files. */
final class WallpaperHistory {
    static WallpaperHistory instance;
    static synchronized WallpaperHistory get(Context c){if(instance==null)instance=new WallpaperHistory(c);return instance;}
    static final class Snapshot {
        final String kind,component,file;final int id;
        Snapshot(String kind,String component,String file,int id){this.kind=kind;this.component=component;this.file=file;this.id=id;}
    }
    final Context context;final WallpaperManager manager;final SharedPreferences prefs;
    final Handler main=new Handler(Looper.getMainLooper());final ExecutorService worker=Executors.newSingleThreadExecutor();
    final ArrayList<Runnable> listeners=new ArrayList<>();
    boolean busy;String notice="";
    WallpaperHistory(Context c){context=c.getApplicationContext();manager=WallpaperManager.getInstance(context);prefs=context.getSharedPreferences("wallpaper_history",Context.MODE_PRIVATE);cleanup();}
    void listen(Runnable r){listeners.add(r);}void unlisten(Runnable r){listeners.remove(r);}
    void changed(){for(Runnable r:new ArrayList<>(listeners))r.run();}
    int id(){try{return manager.getWallpaperId(WallpaperManager.FLAG_SYSTEM);}catch(RuntimeException denied){return -1;}}
    @SuppressWarnings("deprecation") WallpaperInfo info(){try{return Build.VERSION.SDK_INT>=34?manager.getWallpaperInfo(WallpaperManager.FLAG_SYSTEM):manager.getWallpaperInfo();}catch(SecurityException denied){return null;}}
    boolean isCat(){WallpaperInfo info=info();return info!=null&&new ComponentName(context,CatWallpaperService.class).equals(info.getComponent());}
    Snapshot read(String prefix){return new Snapshot(prefs.getString(prefix+"kind","none"),prefs.getString(prefix+"component",""),prefs.getString(prefix+"file",""),prefs.getInt(prefix+"id",-1));}
    void write(SharedPreferences.Editor e,String prefix,Snapshot s){e.putString(prefix+"kind",s.kind).putString(prefix+"component",s.component).putString(prefix+"file",s.file).putInt(prefix+"id",s.id);}
    String phase(){return prefs.getString("phase","");}
    File file(String name){return name.matches("snap-[a-f0-9-]{36}\\.img")?new File(new File(context.getNoBackupFilesDir(),"wallpaper-history"),name):null;}
    boolean available(){Snapshot s=read("saved_");return s.kind.equals("default")||(s.kind.equals("live")&&ComponentName.unflattenFromString(s.component)!=null)||(s.kind.equals("image")&&file(s.file)!=null&&file(s.file).isFile());}
    Snapshot beforeApply(){
        int current=id();WallpaperInfo info=info();
        if(info!=null)return new Snapshot(new ComponentName(context,CatWallpaperService.class).equals(info.getComponent())?"keep":"live",info.getComponent().flattenToString(),"",current);
        if(current>0&&current==prefs.getInt("known_default_id",-2))return new Snapshot("default","","",current);
        Snapshot saved=read("saved_");if(current>0&&current==saved.id&&saved.kind.equals("image")&&available())return saved;
        return null;
    }
    void stage(Snapshot s,String phase){SharedPreferences.Editor e=prefs.edit();write(e,"pending_",s);e.putString("phase",phase).putBoolean("enabled_before",new PetSettings(context).enabled()).apply();changed();}
    void phase(String value){prefs.edit().putString("phase",value).apply();changed();}
    void cancel(){prefs.edit().remove("phase").remove("pending_kind").remove("pending_component").remove("pending_file").remove("pending_id").remove("photo_action").apply();cleanup();changed();}
    void photo(String action){stage(new Snapshot("none","","",id()),"photo");prefs.edit().putString("photo_action",action).apply();}
    void receivePhoto(Uri uri){
        if(uri==null||!"content".equals(uri.getScheme())){notice="사진을 다시 선택해 주세요.";cancel();return;}
        String action=prefs.getString("photo_action","backup");int original=read("pending_").id;busy=true;phase("copy");
        worker.execute(()->{
            File target=file("snap-"+UUID.randomUUID()+".img");File part=new File(target.getPath()+".part");
            try{
                if(!target.getParentFile().isDirectory()&&!target.getParentFile().mkdirs())throw new IOException("No backup directory");
                try(InputStream in=context.getContentResolver().openInputStream(uri);FileOutputStream out=new FileOutputStream(part)){
                    if(in==null)throw new IOException("Missing image");byte[] buffer=new byte[32768];long total=0;int n;
                    while((n=in.read(buffer))!=-1){total+=n;if(total>50L*1024*1024)throw new IOException("Image exceeds 50MiB");out.write(buffer,0,n);}out.getFD().sync();
                }
                BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeFile(part.getPath(),bounds);
                if(bounds.outWidth<=0||bounds.outHeight<=0||!part.renameTo(target))throw new IOException("Invalid image");
                main.post(()->{busy=false;Snapshot s=new Snapshot("image","",target.getName(),original);if(action.equals("restore"))restoreImage(s);else stage(s,"ready");});
            }catch(Exception failure){part.delete();target.delete();main.post(()->{busy=false;notice="사진을 보관하지 못했어요. 50MB 이하의 사진을 다시 선택해 주세요.";cancel();});}
        });
    }
    void picking(){prefs.edit().putString("phase","picking").apply();PetLifecycle.resume(context);changed();}
    void reconcile(){
        if(busy)return;String phase=phase();if(phase.equals("picking")){
            if(isCat()){Snapshot pending=read("pending_");SharedPreferences.Editor e=prefs.edit();write(e,"saved_",pending);e.apply();notice=pending.kind.equals("none")?"고양이를 데려왔어요. 이전 사진은 보관하지 않았어요.":"이전 배경을 보관하고 고양이를 데려왔어요.";}
            else new PetSettings(context).prefs.edit().putBoolean("pets_enabled",prefs.getBoolean("enabled_before",true)).apply();
            cancel();
        }else if(phase.equals("live_return")||phase.equals("settings")){
            if(id()!=read("pending_").id&&!isCat()){PetLifecycle.stop(context);notice="고양이 배경을 해제했어요.";}cancel();
        }else if(phase.equals("copy")){notice="사진 보관이 중단됐어요. 다시 선택해 주세요.";cancel();}
        else if(phase.equals("restoring")){
            if(id()!=read("pending_").id&&!isCat()){Snapshot s=read("pending_");SharedPreferences.Editor e=prefs.edit();write(e,"saved_",new Snapshot(s.kind,s.component,s.file,id()));e.apply();PetLifecycle.stop(context);notice="보관한 사진으로 돌아왔어요.";}cancel();
        }
    }
    void restoreImage(Snapshot s){
        stage(s,"restoring");busy=true;changed();worker.execute(()->{
            try{File image=file(s.file);if(image==null)throw new IOException("Invalid backup");int restored;
                try(InputStream in=new FileInputStream(image)){restored=manager.setStream(in,null,false,WallpaperManager.FLAG_SYSTEM);}
                main.post(()->{SharedPreferences.Editor e=prefs.edit();write(e,"saved_",new Snapshot("image","",s.file,restored));e.apply();busy=false;PetLifecycle.stop(context);notice="보관한 배경 사진으로 돌아왔어요.";cancel();});
            }catch(Exception failure){main.post(()->{busy=false;notice="배경을 되돌리지 못했어요. 사진을 다시 선택하거나 휴대폰 배경 설정을 열어 주세요.";cancel();});}
        });
    }
    void restoreDefault(){
        busy=true;changed();worker.execute(()->{try{manager.clear(WallpaperManager.FLAG_SYSTEM);int current=id();main.post(()->{prefs.edit().putInt("known_default_id",current).apply();busy=false;PetLifecycle.stop(context);notice="휴대폰 기본 배경으로 해제했어요.";cancel();});}
            catch(Exception denied){main.post(()->{busy=false;notice="기본 배경으로 바꾸지 못했어요. 휴대폰 배경 설정에서 변경해 주세요.";changed();});}});
    }
    void cleanup(){
        File directory=new File(context.getNoBackupFilesDir(),"wallpaper-history");File[] files=directory.listFiles();if(files==null)return;
        String saved=read("saved_").file,pending=read("pending_").file;
        for(File file:files)if(file.getName().matches("snap-[a-f0-9-]{36}\\.img(\\.part)?")&&!file.getName().equals(saved)&&!file.getName().equals(pending))file.delete();
    }
}
