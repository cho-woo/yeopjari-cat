package com.valuewander.yeopjaricat;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Toast;

final class UpdatePanel {
    private final MainActivity activity;
    UpdatePanel(MainActivity activity,LinearLayout parent,Bundle saved){
        this.activity=activity;
        LinearLayout card=activity.card(parent,"앱 정보");
        Button store=activity.button("Google Play에서 업데이트 확인",false);
        activity.add(card,store,8);
        store.setOnClickListener(v->openListing());
        Button privacy=activity.button("개인정보처리방침",false);
        activity.add(card,privacy,8);
        privacy.setOnClickListener(v->new AlertDialog.Builder(activity)
            .setTitle("옆자리 고양이 개인정보처리방침").setMessage(R.string.play_privacy)
            .setPositiveButton("닫기",null).show());
    }
    private void openListing(){
        String id=activity.getPackageName();
        try{
            activity.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("market://details?id="+id)).setPackage("com.android.vending"));
        }catch(ActivityNotFoundException missingStore){
            try{activity.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("https://play.google.com/store/apps/details?id="+id)));}
            catch(ActivityNotFoundException missingBrowser){Toast.makeText(activity,"Google Play를 열 수 없어요.",Toast.LENGTH_SHORT).show();}
        }
    }
    String versionLabel(){
        try{return activity.getPackageManager().getPackageInfo(activity.getPackageName(),0).versionName;}
        catch(android.content.pm.PackageManager.NameNotFoundException impossible){throw new IllegalStateException(impossible);}
    }
    void resume(){}
    void pause(){}
    void save(Bundle saved){}
    void close(){}
}
