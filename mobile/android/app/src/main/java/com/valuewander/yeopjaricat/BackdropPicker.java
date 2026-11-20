package com.valuewander.yeopjaricat;

import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.EnumMap;

final class BackdropPicker {
    final MainActivity activity;
    final EnumMap<PlayBackdrop,Tile> tiles=new EnumMap<>(PlayBackdrop.class);
    final Button savedPhoto,choosePhoto;

    BackdropPicker(MainActivity activity,LinearLayout content){
        this.activity=activity;
        TextView title=activity.text("놀이 배경",17,MainActivity.INK);
        title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);activity.add(content,title,16);
        LinearLayout row=new LinearLayout(activity);activity.add(content,row,8);
        for(PlayBackdrop design:PlayBackdrop.values()){
            if(design==PlayBackdrop.PHOTO)continue;
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);
            if(!tiles.isEmpty())lp.setMarginStart(activity.dp(6));
            Tile tile=new Tile(design);tiles.put(design,tile);row.addView(tile,lp);
        }
        LinearLayout photos=new LinearLayout(activity);activity.add(content,photos,4);
        savedPhoto=activity.button("내 사진 다시 사용",false);
        photos.addView(savedPhoto,new LinearLayout.LayoutParams(0,-2,1));
        savedPhoto.setOnClickListener(v->select(PlayBackdrop.PHOTO));
        choosePhoto=activity.button("배경 사진 고르기",false);
        photos.addView(choosePhoto,new LinearLayout.LayoutParams(0,-2,1));
        choosePhoto.setOnClickListener(v->activity.wallpaperReturn.choosePetPhoto(false));
        refresh();
    }
    private void select(PlayBackdrop design){activity.settings.put(PlayBackdrop.PREF,design.key);}
    void refresh(){
        PlayBackdrop selected=PlayBackdrop.selected(activity.settings);
        for(Tile tile:tiles.values())tile.mark(tile.design==selected);
        savedPhoto.setVisibility(!activity.settings.photo().isEmpty()&&selected!=PlayBackdrop.PHOTO?View.VISIBLE:View.GONE);
        choosePhoto.setText(selected==PlayBackdrop.PHOTO?"내 사진 사용 중 · 사진 변경":"배경 사진 고르기");
    }
    private final class Tile extends LinearLayout {
        final PlayBackdrop design;
        final TextView check;
        Tile(PlayBackdrop design){
            super(activity);this.design=design;
            setOrientation(VERTICAL);setGravity(Gravity.CENTER);int pad=activity.dp(3);setPadding(pad,pad,pad,pad);
            FrameLayout art=new FrameLayout(activity);
            art.setBackground(activity.round(0xffe8e2ed,10));art.setClipToOutline(true);
            addView(art,new LayoutParams(-1,activity.dp(54)));
            ImageView picture=new ImageView(activity);picture.setScaleType(ImageView.ScaleType.CENTER_CROP);
            picture.setImageResource(design.thumbnail);art.addView(picture,new FrameLayout.LayoutParams(-1,-1));
            check=activity.text("✓",13,0xffffffff);check.setGravity(Gravity.CENTER);
            check.setBackground(activity.round(MainActivity.PURPLE,12));
            FrameLayout.LayoutParams badge=new FrameLayout.LayoutParams(activity.dp(20),activity.dp(20),Gravity.TOP|Gravity.END);
            badge.topMargin=badge.rightMargin=activity.dp(3);art.addView(check,badge);
            TextView label=activity.text(design.label,12,MainActivity.INK);label.setGravity(Gravity.CENTER);
            addView(label,new LayoutParams(-1,activity.dp(31)));
            art.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
            label.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            setContentDescription(design.label);setFocusable(true);setOnClickListener(v->select(design));
        }
        void mark(boolean active){
            if(getBackground()!=null&&isSelected()==active)return;
            setSelected(active);check.setVisibility(active?View.VISIBLE:View.GONE);
            GradientDrawable border=activity.round(active?0xfff4f0fc:0xffffffff,13);
            border.setStroke(activity.dp(active?2:1),active?MainActivity.PURPLE:0xffe2dbe6);setBackground(border);
        }
        @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info){
            super.onInitializeAccessibilityNodeInfo(info);info.setClassName("android.widget.RadioButton");
            info.setCheckable(true);info.setChecked(isSelected());
        }
    }
}
