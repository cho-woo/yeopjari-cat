package com.valuewander.yeopjaricat;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.WallpaperManager;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

public final class MainActivity extends Activity {
    PetSettings settings;
    SceneView preview;
    Switch overlay;
    boolean updatingSwitch,permissionPending;
    static final int INK=0xff302b40,MUTED=0xff756e80,PURPLE=0xff7560bb;
    int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);settings=new PetSettings(this);permissionPending=saved!=null&&saved.getBoolean("pending_overlay");
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(0xfffaf7f2);
        LinearLayout content=column();content.setPadding(dp(24),dp(18),dp(24),dp(30));scroll.addView(content);
        scroll.setOnApplyWindowInsetsListener((v,insets)->{
            int top,bottom;
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());top=bars.top;bottom=bars.bottom;}
            else{top=insets.getSystemWindowInsetTop();bottom=insets.getSystemWindowInsetBottom();}
            content.setPadding(dp(24),dp(18)+top,dp(24),dp(30)+bottom);return insets;
        });
        content.addView(text("VALUEWANDER  /  MOBILE PREVIEW",10,MUTED));
        TextView title=text("손안의 작은\n옆자리 고양이",31,INK);title.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));title.setLineSpacing(dp(2),1);add(content,title,14);
        add(content,text("매일의 홈 화면에, 귀여운 옆자리 하나.",14,MUTED),10);
        preview=new SceneView(this);preview.setBackground(round(0xffece3f9,26));preview.setClipToOutline(true);
        LinearLayout.LayoutParams previewParams=new LinearLayout.LayoutParams(-1,dp(225));previewParams.topMargin=dp(22);content.addView(preview,previewParams);
        Button apply=button("홈 화면에 데려오기",true);apply.setContentDescription("홈 화면에 데려오기. 움직이는 배경화면 선택 화면을 엽니다.");add(content,apply,14);
        apply.setOnClickListener(v->{
            Intent intent=new Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,new ComponentName(this,CatWallpaperService.class));
            try{startActivity(intent);}catch(android.content.ActivityNotFoundException e){Toast.makeText(this,"이 기기에서는 움직이는 배경화면을 선택할 수 없어요.",Toast.LENGTH_LONG).show();}
        });
        add(content,text("고양이는 아이콘 뒤에서 지내요. 적용할 때 ‘홈 화면’을 선택해 주세요. 다른 앱 위에는 기본으로 표시하지 않아요.",12,MUTED),10);
        LinearLayout cats=card(content,"함께 지낼 고양이");
        cats.addView(coatChoices(settings.coat(),coat->{settings.prefs.edit().putInt("coat",coat).apply();preview.reload();}));
        Switch companion=new Switch(this);companion.setText("두 마리 함께 지내기");companion.setTextColor(INK);companion.setTextSize(14);companion.setMinHeight(dp(52));companion.setChecked(settings.prefs.getBoolean("companion",false));cats.addView(companion);
        companion.setOnCheckedChangeListener((b,checked)->{settings.prefs.edit().putBoolean("companion",checked).apply();preview.reload();});
        TextView second=text("두 번째 고양이",12,MUTED);add(cats,second,6);
        cats.addView(coatChoices(settings.companionCoat(),coat->{settings.prefs.edit().putInt("companion_coat",coat).apply();preview.reload();}));
        LinearLayout play=card(content,"지금 같이 놀기");
        LinearLayout row=new LinearLayout(this);row.setOrientation(LinearLayout.HORIZONTAL);play.addView(row);
        String[] labels={"살금살금","그루밍","꾹꾹이"},actions={"walk","groom","knead"};
        for(int i=0;i<labels.length;i++){final String action=actions[i];Button b=button(labels[i],false);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(48),1);lp.setMarginEnd(dp(4));row.addView(b,lp);b.setOnClickListener(v->preview.scene.command(action));}
        add(play,text("고양이를 톡 누르면 앞발로 반응해요. 행동이 끝난 뒤에는 충분히 쉬어요.",12,MUTED),10);
        LinearLayout appearance=card(content,"내 홈 화면답게");
        Button photo=button("내 사진을 배경으로 고르기",false);appearance.addView(photo);photo.setOnClickListener(v->startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION),100));
        Button reset=button("라일락 기본 배경",false);add(appearance,reset,6);reset.setOnClickListener(v->{settings.prefs.edit().remove("photo").apply();preview.reload();});
        add(appearance,text("고양이 크기",13,INK),14);
        SeekBar size=new SeekBar(this);size.setMax(40);size.setProgress(settings.size()-56);size.setContentDescription("고양이 크기");appearance.addView(size);
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int value,boolean fromUser){}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){settings.prefs.edit().putInt("size",56+s.getProgress()).apply();preview.reload();}});
        add(appearance,text("기존 배경 사진을 자동으로 읽지 않아요. 같은 사진을 골라 고양이와 함께 사용할 수 있어요.",12,MUTED),8);
        add(appearance,text("홈 화면에서 지낼 높이",13,INK),14);
        SeekBar position=new SeekBar(this);position.setMax(58);position.setProgress(settings.prefs.getInt("home_floor_percent",58)-30);position.setContentDescription("홈 화면 고양이 높이");appearance.addView(position);
        position.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int value,boolean fromUser){}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){settings.prefs.edit().putInt("home_floor_percent",30+s.getProgress()).apply();}});
        add(appearance,text("아이콘이나 검색창과 겹치면 고양이가 지낼 높이를 조절해 주세요.",12,MUTED),8);
        LinearLayout optional=card(content,"다른 앱 위에서도 만나기");
        overlay=new Switch(this);overlay.setText("다른 앱 위에 고양이 표시");overlay.setTextColor(INK);overlay.setTextSize(14);overlay.setMinHeight(dp(52));optional.addView(overlay);
        add(optional,text("선택 기능 · 기본 꺼짐\n켜면 다른 앱을 사용하는 동안에도 작은 고양이 창이 떠요. 별도 권한이 필요하며, 알림에서 바로 숨길 수 있어요. 고양이 창 부분의 터치는 고양이가 받아요.",12,MUTED),6);
        overlay.setOnCheckedChangeListener((b,enabled)->{if(updatingSwitch)return;if(enabled)enableOverlay();else{permissionPending=false;stopService(new Intent(this,CatOverlayService.class));settings.prefs.edit().putBoolean("overlay",false).apply();}});
        add(content,text("PREVIEW 0.1  ·  기기 안에만 저장\nPC의 성장 기록은 아직 동기화하지 않아요.",11,MUTED),24);
        setContentView(scroll);
    }
    interface CoatChanged{void accept(int coat);}
    View coatChoices(int selected,CoatChanged changed){
        RadioGroup group=new RadioGroup(this);group.setOrientation(RadioGroup.VERTICAL);
        int[] coats={0,8,10};String[] names={"크림치즈냥","턱시도냥","눈꽃냥"};
        for(int i=0;i<coats.length;i++){RadioButton radio=new RadioButton(this);radio.setId(1000+coats[i]);radio.setText(names[i]);radio.setTextColor(INK);radio.setTextSize(14);radio.setMinHeight(dp(46));group.addView(radio);radio.setChecked(coats[i]==selected);}
        group.setOnCheckedChangeListener((g,id)->changed.accept(id-1000));return group;
    }
    void enableOverlay(){
        if(!Settings.canDrawOverlays(this)){
            updatingSwitch=true;overlay.setChecked(false);updatingSwitch=false;
            new AlertDialog.Builder(this).setTitle("다른 앱 위에서 만나려면").setMessage("다음 화면에서 옆자리 고양이의 ‘다른 앱 위에 표시’를 허용해 주세요. 홈 화면 배경화면에는 이 권한이 필요하지 않아요.")
                .setPositiveButton("권한 설정 열기",(d,w)->{permissionPending=true;startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())));})
                .setNegativeButton("홈 화면에서만 만나기",(d,w)->permissionPending=false).show();return;
        }
        permissionPending=false;
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},200);return;}
        startOverlay();
    }
    void startOverlay(){try{startForegroundService(new Intent(this,CatOverlayService.class));getWindow().getDecorView().postDelayed(this::syncOverlay,350);}catch(SecurityException|IllegalStateException e){Toast.makeText(this,"지금은 다른 앱 위 표시를 시작할 수 없어요. 다시 켜 주세요.",Toast.LENGTH_LONG).show();syncOverlay();}}
    void syncOverlay(){updatingSwitch=true;overlay.setChecked(CatOverlayService.running);updatingSwitch=false;}
    @Override protected void onResume(){super.onResume();if(preview!=null){preview.start();syncOverlay();}if(permissionPending){permissionPending=false;if(Settings.canDrawOverlays(this))enableOverlay();}}
    @Override protected void onPause(){if(preview!=null)preview.stop();super.onPause();}
    @Override protected void onSaveInstanceState(Bundle b){b.putBoolean("pending_overlay",permissionPending);super.onSaveInstanceState(b);}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==200)startOverlay();}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==100&&result==RESULT_OK&&data!=null&&data.getData()!=null){Uri uri=data.getData();try{getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException ignored){}settings.prefs.edit().putString("photo",uri.toString()).apply();preview.reload();}}
    LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    LinearLayout card(LinearLayout parent,String title){LinearLayout c=column();c.setPadding(dp(18),dp(18),dp(18),dp(18));c.setBackground(round(Color.WHITE,22));add(parent,c,18);TextView t=text(title,17,INK);t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);c.addView(t);return c;}
    TextView text(String value,int size,int color){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setLineSpacing(dp(3),1);return t;}
    Button button(String label,boolean primary){Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(14);b.setTextColor(primary?Color.WHITE:PURPLE);b.setMinHeight(dp(primary?54:48));b.setGravity(Gravity.CENTER);b.setBackground(round(primary?PURPLE:0xfff2edf8,16));return b;}
    GradientDrawable round(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    void add(LinearLayout parent,View child,int top){LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(top);parent.addView(child,lp);}
}
