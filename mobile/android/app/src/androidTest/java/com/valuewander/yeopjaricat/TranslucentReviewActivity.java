package com.valuewander.yeopjaricat;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.TextView;

/** Not included in the downloadable app. Tests Home classification when wallpaper is still visible. */
public final class TranslucentReviewActivity extends Activity {
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER);
        TextView label=new TextView(this);label.setText("QA · 다른 앱이 열려 있어요");label.setTextSize(22);
        label.setGravity(Gravity.CENTER);label.setTextColor(0xff302b40);label.setBackgroundColor(0xbbeeeeee);setContentView(label);
    }
}
