package com.valuewander.yeopjaricat;

import android.app.AlertDialog;
import android.content.Context;
import android.view.WindowManager;

final class PetMenus {
    static AlertDialog show(Context context,boolean overlay,PetTouch touch){
        AlertDialog dialog=new AlertDialog.Builder(context).setTitle("옆자리 고양이")
            .setItems(new String[]{"종료하기","취소"},(d,which)->{if(which==0)PetLifecycle.stop(context);})
            .setOnDismissListener(d->touch.releaseMenu()).create();
        if(overlay&&dialog.getWindow()!=null)dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
        dialog.show();return dialog;
    }
}
