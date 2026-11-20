package com.valuewander.yeopjaricat;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;

/** Owns the play gesture stream. The launcher never receives a drag made here. */
public final class PlayActivity extends Activity {
    SceneView play;
    final android.content.SharedPreferences.OnSharedPreferenceChangeListener power=(prefs,key)->{
        if("pets_enabled".equals(key)&&!prefs.getBoolean(key,true))finish();
    };

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        PetSettings settings=new PetSettings(this);
        if(!settings.enabled()){
            startActivity(new Intent(this,MainActivity.class));finish();return;
        }
        settings.prefs.registerOnSharedPreferenceChangeListener(power);
        FrameLayout root=new FrameLayout(this);
        play=new SceneView(this,true);root.addView(play,new FrameLayout.LayoutParams(-1,-1));
        Button done=new Button(this);done.setAllCaps(false);done.setText("놀이 끝내기");
        done.setOnClickListener(v->finish());
        FrameLayout.LayoutParams placement=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.END);
        int gap=Math.round(getResources().getDisplayMetrics().density*12);
        placement.topMargin=gap;placement.setMarginEnd(gap);root.addView(done,placement);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            int top,right;
            if(android.os.Build.VERSION.SDK_INT>=30){
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                top=bars.top;right=bars.right;
            }else{top=insets.getSystemWindowInsetTop();right=insets.getSystemWindowInsetRight();}
            placement.topMargin=gap+top;placement.rightMargin=gap+right;done.setLayoutParams(placement);
            return insets;
        });
        setContentView(root);
    }
    @Override protected void onStart(){super.onStart();MainActivity.visibleActivities++;HomeSceneStore.notifyLayer(this);}
    @Override protected void onResume(){
        super.onResume();if(play==null)return;
        HomeSceneStore.inPlay=true;
        if(HomeSceneStore.get(this)!=play.scene)play.reload();
        play.resize();play.start();
    }
    @Override protected void onPause(){
        if(play!=null){play.stop();play.touch.cancel();play.scene.savePositions();HomeSceneStore.inPlay=false;}
        super.onPause();
    }
    @Override protected void onDestroy(){
        new PetSettings(this).prefs.unregisterOnSharedPreferenceChangeListener(power);super.onDestroy();
    }
    @Override protected void onStop(){MainActivity.visibleActivities--;HomeSceneStore.notifyLayer(this);super.onStop();}
}
