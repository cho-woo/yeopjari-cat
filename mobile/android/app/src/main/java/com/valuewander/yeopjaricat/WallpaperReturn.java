package com.valuewander.yeopjaricat;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.WallpaperManager;
import android.content.ComponentName;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.ArrayList;

final class WallpaperReturn {
    static volatile boolean pickerOpen;
    static final int PHOTO=401,APPLY=402,RETURN=403,SETTINGS=404;
    final MainActivity activity;final WallpaperHistory history;final Button apply,restore;final TextView status;
    final Runnable listener=this::refresh;
    boolean resumed,closing;AlertDialog dialog;String dialogKind="";
    WallpaperReturn(MainActivity a,LinearLayout parent,Button apply,Bundle saved){
        activity=a;this.apply=apply;history=WallpaperHistory.get(a);
        if(saved!=null)dialogKind=saved.getString("wallpaper_dialog","");
        restore=a.button("이전 배경으로 돌아가기",false);a.add(parent,restore,8);restore.setOnClickListener(v->chooseRestore());
        status=a.text("",12,MainActivity.MUTED);a.add(parent,status,8);history.listen(listener);refresh();
    }
    void refresh(){
        // Older installations may still have the cat wallpaper or a retained backup.
        // Only an explicit restore action may change the wallpaper.
        boolean legacy=history.isCat()||history.available()||!history.notice.isEmpty();
        restore.setVisibility(legacy?android.view.View.VISIBLE:android.view.View.GONE);
        status.setVisibility(restore.getVisibility());
        apply.setEnabled(!history.busy);
        restore.setEnabled(!history.busy);
        status.setText(history.busy?"배경 사진을 처리하고 있어요…":
            !history.notice.isEmpty()?history.notice:
            history.isCat()?"이전 버전의 고양이 배경을 사용 중이에요. 원래 배경으로 바꾸려면 위 버튼을 눌러 주세요.":
            "이전에 보관한 배경을 복원할 수 있어요.");
    }
    void chooseRestore(){
        ArrayList<String> labels=new ArrayList<>();ArrayList<Runnable> actions=new ArrayList<>();WallpaperHistory.Snapshot saved=history.read("saved_");
        if(history.available()){
            labels.add(saved.kind.equals("image")?"보관한 이전 사진으로 되돌리기":saved.kind.equals("live")?"이전 움직이는 배경으로 되돌리기":"보관한 기본 배경으로 되돌리기");
            actions.add(()->{if(saved.kind.equals("image"))history.restoreImage(saved);else if(saved.kind.equals("default"))history.restoreDefault();else{history.stage(new WallpaperHistory.Snapshot("live",saved.component,"",history.id()),"live_return");launch(new Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,ComponentName.unflattenFromString(saved.component)),RETURN);}});
        }
        String appPhoto=new PetSettings(activity).photo();
        if(!appPhoto.isEmpty()){labels.add("앱에서 쓰던 배경 사진으로 되돌리기");actions.add(()->{history.photo("restore");history.receivePhoto(Uri.parse(appPhoto));});}
        labels.add("이전 사진을 골라서 되돌리기");actions.add(()->selectPhoto("restore"));
        labels.add("휴대폰 기본 배경으로 해제");actions.add(history::restoreDefault);
        labels.add("휴대폰 배경 선택 화면 열기");actions.add(()->{history.stage(new WallpaperHistory.Snapshot("none","","",history.id()),"settings");launch(new Intent(Intent.ACTION_SET_WALLPAPER),SETTINGS);});
        dialog=new AlertDialog.Builder(activity).setTitle("어떤 배경으로 돌아갈까요?").setItems(labels.toArray(new String[0]),(d,which)->{dialogKind="";actions.get(which).run();}).setNegativeButton("취소",null).show();
        track(dialog,"restore");
    }
    void track(AlertDialog current,String kind){dialogKind=kind;current.setOnDismissListener(d->{if(!closing&&dialog==current)dialogKind="";});}
    void save(Bundle saved){saved.putString("wallpaper_dialog",dialogKind);}
    void selectPhoto(String action){dialogKind="";history.photo(action);launch(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),PHOTO);}
    void launch(Intent intent,int request){
        pickerOpen=true;HomeSceneStore.notifyLayer(activity);
        try{activity.startActivityForResult(intent,request);}catch(android.content.ActivityNotFoundException|SecurityException missing){
            if(request==SETTINGS){try{activity.startActivityForResult(new Intent(Settings.ACTION_SETTINGS),SETTINGS);return;}catch(android.content.ActivityNotFoundException ignored){}}
            pickerOpen=false;history.notice="이 기기에서는 해당 화면을 열 수 없어요. 휴대폰의 배경화면 및 스타일 설정에서 변경해 주세요.";
            if(request==APPLY)new PetSettings(activity).prefs.edit().putBoolean("pets_enabled",history.prefs.getBoolean("enabled_before",true)).apply();history.cancel();HomeSceneStore.notifyLayer(activity);
        }
    }
    void result(int request,int result,Intent data){
        if(request==PHOTO){pickerOpen=false;HomeSceneStore.notifyLayer(activity);if(result==Activity.RESULT_OK&&data!=null&&data.getData()!=null)history.receivePhoto(data.getData());else history.cancel();}
    }
    void resume(){resumed=true;pickerOpen=false;HomeSceneStore.notifyLayer(activity);history.reconcile();if(history.phase().equals("ready"))history.cancel();refresh();if(history.phase().isEmpty()&&!history.busy&&(dialog==null||!dialog.isShowing())){String pending=dialogKind;dialogKind="";if(pending.equals("restore"))chooseRestore();}}
    void pause(){resumed=false;}
    void close(){closing=true;history.unlisten(listener);if(dialog!=null)dialog.dismiss();}
}
