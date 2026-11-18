package com.valuewander.yeopjaricat;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

final class UpdatePanel {
    final MainActivity activity;final AppUpdates updates;final TextView status;final Button button;
    final Runnable listener=this::render;
    boolean resumed,installWhenReady,waitingPermission,verifying;
    String prompted="";
    AlertDialog dialog;
    UpdatePanel(MainActivity activity,LinearLayout parent,Bundle saved){
        this.activity=activity;updates=AppUpdates.get(activity);
        if(saved!=null){installWhenReady=saved.getBoolean("update_install");waitingPermission=saved.getBoolean("update_permission");prompted=saved.getString("update_prompted","");}
        LinearLayout card=activity.card(parent,"앱 업데이트 · "+updates.currentName);
        status=activity.text("새 버전을 확인할 수 있어요.",13,MainActivity.MUTED);activity.add(card,status,8);
        button=activity.button("업데이트 확인",false);activity.add(card,button,10);
        activity.add(card,activity.text("새 버전은 앱에서 받고, 휴대폰의 설치 화면에서 확인해 주세요.",12,MainActivity.MUTED),8);
        button.setOnClickListener(v->{if(updates.state==AppUpdates.State.READY)requestInstall();else if(updates.state==AppUpdates.State.AVAILABLE)startDownload();else updates.check(true);});
        updates.listen(listener);render();
    }
    void startDownload(){installWhenReady=true;updates.download();}
    void render(){
        String label="업데이트 확인",message="새 버전을 확인할 수 있어요.";
        switch(updates.state){
            case CHECKING:message="새 버전 확인 중…";break;
            case LATEST:message="최신 버전이에요.";label="다시 확인";break;
            case ERROR:message="새 버전을 확인하지 못했어요. 연결을 확인해 주세요.";label="다시 확인";break;
            case AVAILABLE:message="새 버전 "+updates.release.version.number()+"이 나왔어요.";label="업데이트하기";break;
            case DOWNLOADING:message="업데이트 받는 중 · "+updates.percent+"%";label="받는 중";break;
            case READY:message="새 버전 설치 준비가 끝났어요.";label="업데이트 설치";break;
            default:break;
        }
        status.setText(message);button.setText(label);button.setEnabled(!updates.busy()&&!verifying);
        if(resumed&&installWhenReady&&updates.state==AppUpdates.State.READY){requestInstall();return;}
        if(resumed&&updates.state==AppUpdates.State.AVAILABLE){
            String version=updates.release.version.name();boolean snoozed=version.equals(updates.prefs.getString("snoozed",""))&&System.currentTimeMillis()-updates.prefs.getLong("snoozed_at",0)<24*60*60*1000L;
            if(!version.equals(prompted)&&!snoozed){prompted=version;
                dialog=new AlertDialog.Builder(activity).setTitle("새 버전이 나왔어요").setMessage("옆자리 고양이 "+updates.release.version.number()+"으로 업데이트할까요? 파일은 앱에서 받아요. 설정과 고양이는 그대로 이어져요.")
                    .setPositiveButton("업데이트하기",(d,w)->startDownload()).setNegativeButton("나중에",(d,w)->updates.prefs.edit().putString("snoozed",version).putLong("snoozed_at",System.currentTimeMillis()).apply()).show();
            }
        }
    }
    void requestInstall(){
        installWhenReady=false;if(verifying)return;verifying=true;render();
        updates.verifyForInstall(()->{verifying=false;if(!resumed||activity.isFinishing()){installWhenReady=true;return;}render();openInstaller();},()->{verifying=false;installWhenReady=false;render();Toast.makeText(activity,"설치 파일을 다시 받아 주세요.",Toast.LENGTH_LONG).show();});
    }
    void openInstaller(){
        if(!activity.getPackageManager().canRequestPackageInstalls()){
            dialog=new AlertDialog.Builder(activity).setTitle("앱에서 업데이트하려면").setMessage("다음 화면에서 옆자리 고양이의 ‘이 출처 허용’을 한 번 켜 주세요. 돌아오면 업데이트 설치 화면으로 이어져요.")
                .setPositiveButton("권한 설정 열기",(d,w)->{try{waitingPermission=true;AppUpdates.installer(activity,true);activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+activity.getPackageName())),300);}catch(android.content.ActivityNotFoundException missing){waitingPermission=false;AppUpdates.installer(activity,false);Toast.makeText(activity,"휴대폰 설정에서 옆자리 고양이의 앱 설치를 허용해 주세요.",Toast.LENGTH_LONG).show();}})
                .setNegativeButton("취소",(d,w)->render()).show();return;
        }
        Uri uri=UpdateFileProvider.uri(activity);
        Intent intent=new Intent(Intent.ACTION_VIEW).setDataAndType(uri,"application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.setClipData(ClipData.newRawUri("옆자리 고양이 업데이트",uri));
        try{AppUpdates.installer(activity,true);activity.startActivityForResult(intent,302);}catch(android.content.ActivityNotFoundException|SecurityException missing){AppUpdates.installer(activity,false);Toast.makeText(activity,"휴대폰의 앱 설치 설정을 확인해 주세요.",Toast.LENGTH_LONG).show();}
    }
    void resume(){
        resumed=true;AppUpdates.installer(activity,false);
        if(waitingPermission){waitingPermission=false;if(activity.getPackageManager().canRequestPackageInstalls())requestInstall();else Toast.makeText(activity,"설치를 허용한 뒤 ‘업데이트 설치’를 눌러 주세요.",Toast.LENGTH_LONG).show();}
        updates.check(false);render();
    }
    void pause(){resumed=false;}
    void save(Bundle saved){saved.putBoolean("update_install",installWhenReady);saved.putBoolean("update_permission",waitingPermission);saved.putString("update_prompted",prompted);}
    void close(){updates.unlisten(listener);if(dialog!=null){dialog.dismiss();dialog=null;}}
}
