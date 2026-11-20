package com.valuewander.yeopjaricat;

import android.app.AlertDialog;
import android.content.Context;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.Toast;

final class PetMenus {
    static AlertDialog show(Context context,boolean overlay,PetTouch touch){
        String[] options=touch.scene.actors[1]==null?new String[]{"종료하기","손가락 놀이","행동시키기","취소"}:
            new String[]{"종료하기","손가락 놀이","행동시키기","두 마리 함께 놀기","취소"};
        AlertDialog dialog=new AlertDialog.Builder(context).setTitle("옆자리 고양이")
            .setItems(options,null).setOnDismissListener(d->touch.releaseMenu()).create();
        if(overlay && dialog.getWindow()!=null)dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
        dialog.show();
        ListView list=dialog.getListView();
        list.setOnItemClickListener((parent,view,index,id)->{
            String choice=options[index];
            if(choice.equals("행동시키기")||choice.equals("두 마리 함께 놀기")){
                boolean pair=choice.equals("두 마리 함께 놀기");
                dialog.setTitle(choice);
                String[] items=pair?MobileActions.socialLabels:MobileActions.labels.values().toArray(new String[0]);
                list.setAdapter(new ArrayAdapter<>(dialog.getContext(),android.R.layout.simple_list_item_1,items));
                list.setOnItemClickListener((p,v,n,i)->{
                    if(pair)touch.scene.social(MobileActions.social[n],touch.selectedActor);
                    else touch.actor.request(MobileActions.all[n]);
                    dialog.dismiss();
                });
            }else{
                dialog.dismiss();
                if(choice.equals("종료하기"))PetLifecycle.stop(context);
                if(choice.equals("손가락 놀이")){
                    touch.actor.armFinger();
                    Toast.makeText(context,"이 고양이에서 시작해 손가락을 끌어 보세요.",Toast.LENGTH_SHORT).show();
                }
            }
        });
        return dialog;
    }
    static void choosePreview(MainActivity activity,boolean pair){
        String[] labels=pair?MobileActions.socialLabels:MobileActions.labels.values().toArray(new String[0]);
        new AlertDialog.Builder(activity).setTitle(pair?"두 마리 함께 놀기":"행동 고르기")
            .setItems(labels,(d,i)->{if(pair)activity.preview.scene.social(MobileActions.social[i],0);
                else activity.preview.scene.command(MobileActions.all[i]);}).show();
    }
}
