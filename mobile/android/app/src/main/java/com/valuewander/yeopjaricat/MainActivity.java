package com.valuewander.yeopjaricat;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
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
    static volatile int visibleActivities;
    PetSettings settings;
    SceneView preview;
    Switch overlay;
    BackdropPicker backdropPicker;
    Button power,homePlay;
    UpdatePanel updatePanel;
    WallpaperReturn wallpaperReturn;
    boolean updatingSwitch,permissionPending;
    final android.content.SharedPreferences.OnSharedPreferenceChangeListener preferences=(p,key)->{
        if(power==null)return;
        if(PlayBackdrop.PREF.equals(key)||"photo".equals(key)){
            preview.backdrop.refresh();preview.scene.fitBackdropGround(false);backdropPicker.refresh();wallpaperReturn.refresh();
        }
        syncOverlay();preview.invalidate();
    };
    static final int INK=0xff302b40,MUTED=0xff756e80,PURPLE=0xff7560bb;
    int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);settings=new PetSettings(this);permissionPending=saved!=null&&saved.getBoolean("pending_cross_app");
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
        int contentWidth=getResources().getConfiguration().screenWidthDp-48;
        int availableHeight=getResources().getConfiguration().screenHeightDp;
        int previewHeight=Math.max(145,Math.min(Math.round(availableHeight*.37f),Math.max(225,Math.min(380,Math.round(contentWidth*.63f)))));
        LinearLayout.LayoutParams previewParams=new LinearLayout.LayoutParams(-1,dp(previewHeight));previewParams.topMargin=dp(22);content.addView(preview,previewParams);
        backdropPicker=new BackdropPicker(this,content);
        power=button("홈 화면에 적용",true);add(content,power,10);
        add(content,text("‘홈 화면’을 선택하면 아이콘 뒤에서 고양이가 놀아요. 고양이를 종료하면 배경만 남아요.",13,MUTED),8);
        wallpaperReturn=new WallpaperReturn(this,content,power,saved);
        power.setOnClickListener(v->{
            if(wallpaperReturn.history.isCat()){
                if(settings.enabled())PetLifecycle.stop(this);else PetLifecycle.resume(this);
            }else wallpaperReturn.applySelectedPhoto();
            syncOverlay();
        });
        homePlay=button("홈에서 손가락 놀이",false);add(content,homePlay,8);
        homePlay.setOnClickListener(v->{
            if(!wallpaperReturn.history.isCat() || !settings.enabled()){
                Toast.makeText(this,"먼저 홈 화면에 고양이 배경을 적용해 주세요.",Toast.LENGTH_SHORT).show();return;
            }
            HomeSceneStore.get(this).actors[0].armFinger();
            startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));
        });
        LinearLayout optional=column();add(content,optional,10);
        overlay=new Switch(this);overlay.setText("다른 앱 위에 고양이 표시");overlay.setTextColor(INK);overlay.setTextSize(14);overlay.setMinHeight(dp(52));optional.addView(overlay);
        overlay.setOnCheckedChangeListener((b,enabled)->{if(updatingSwitch)return;if(enabled)enableOverlay();else{permissionPending=false;settings.put("overlay",false);reconcileLayer();}});
        updatePanel=new UpdatePanel(this,content,saved);
        LinearLayout cats=card(content,"함께 지낼 고양이");
        cats.addView(coatChoices(1000,settings.coat(),coat->{settings.prefs.edit().putInt("coat",coat).apply();preview.reload();}));
        Switch companion=new Switch(this);companion.setText("두 마리 함께 지내기");companion.setTextColor(INK);companion.setTextSize(14);companion.setMinHeight(dp(52));companion.setChecked(settings.prefs.getBoolean("companion",false));cats.addView(companion);
        TextView second=text("두 번째 고양이",12,MUTED);add(cats,second,6);
        cats.addView(coatChoices(2000,settings.companionCoat(),coat->{settings.prefs.edit().putInt("companion_coat",coat).apply();preview.reload();}));
        LinearLayout play=card(content,"지금 같이 놀기");
        LinearLayout row=new LinearLayout(this);row.setOrientation(LinearLayout.HORIZONTAL);play.addView(row);
        String[] labels={"살금살금","그루밍","꾹꾹이"},actions={"walk","groom","knead"};
        for(int i=0;i<labels.length;i++){final String action=actions[i];Button b=button(labels[i],false);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(48),1);lp.setMarginEnd(dp(4));row.addView(b,lp);b.setOnClickListener(v->preview.scene.command(action));}
        Button actionsButton=button("모든 행동 보기",false);add(play,actionsButton,8);
        actionsButton.setOnClickListener(v->PetMenus.choosePreview(this,false));
        Button pairButton=button("두 마리 함께 놀기",false);add(play,pairButton,8);
        pairButton.setEnabled(settings.companion());companion.setOnCheckedChangeListener((b,checked)->{
            settings.put("companion",checked);preview.reload();pairButton.setEnabled(checked);});
        pairButton.setOnClickListener(v->PetMenus.choosePreview(this,true));
        add(play,text("미리보기의 빈 곳에 손가락을 대고 움직이면 가까운 고양이가 쫓아와요.",12,MUTED),10);
        LinearLayout appearance=card(content,"내 홈 화면답게");
        add(appearance,text("고양이 크기",13,INK),14);
        SeekBar size=new SeekBar(this);size.setMax(40);size.setProgress(settings.size()-56);size.setContentDescription("고양이 크기");appearance.addView(size);
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int value,boolean fromUser){}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){settings.prefs.edit().putInt("size",56+s.getProgress()).apply();preview.reload();}});
        add(appearance,text("홈 화면에서 지낼 높이",13,INK),14);
        SeekBar position=new SeekBar(this);position.setMax(58);position.setProgress(settings.prefs.getInt("home_floor_percent",58)-30);position.setContentDescription("홈 화면 고양이 높이");appearance.addView(position);
        position.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int value,boolean fromUser){}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){settings.prefs.edit().putInt("home_floor_percent",30+s.getProgress()).apply();}});
        add(appearance,text("아이콘이나 검색창과 겹치면 고양이가 지낼 높이를 조절해 주세요.",12,MUTED),8);
        add(content,text("PREVIEW "+updatePanel.updates.currentName.replace("-preview","")+"  ·  설정은 기기 안에만 저장\nPC의 성장 기록은 아직 동기화하지 않아요.",11,MUTED),24);
        settings.prefs.registerOnSharedPreferenceChangeListener(preferences);
        setContentView(scroll);
    }
    interface CoatChanged{void accept(int coat);}
    View coatChoices(int idBase,int selected,CoatChanged changed){
        RadioGroup group=new RadioGroup(this);group.setOrientation(RadioGroup.VERTICAL);
        int[] coats={0,8,10};String[] names={"크림치즈냥","턱시도냥","눈꽃냥"};
        for(int i=0;i<coats.length;i++){RadioButton radio=new RadioButton(this);radio.setId(idBase+coats[i]);radio.setText(names[i]);radio.setTextColor(INK);radio.setTextSize(14);radio.setMinHeight(dp(46));group.addView(radio);radio.setChecked(coats[i]==selected);}
        group.setOnCheckedChangeListener((g,id)->changed.accept(id-idBase));return group;
    }
    void enableOverlay(){
        if(!Settings.canDrawOverlays(this)){
            syncOverlay();
            new AlertDialog.Builder(this).setTitle("다른 앱 위에 고양이 표시").setMessage("이 선택 기능은 Android의 ‘다른 앱 위에 표시’ 권한이 필요해요. 홈 배경화면은 이 권한 없이 사용할 수 있어요.")
                .setPositiveButton("권한 설정 열기",(d,w)->{permissionPending=true;startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())));})
                .setNegativeButton("취소",null).show();return;
        }
        permissionPending=false;
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},200);return;}
        startMode();
    }
    void startMode(){
        settings.prefs.edit().putBoolean("pets_enabled",true)
            .putBoolean("overlay",true).apply();
        reconcileLayer();
    }
    void reconcileLayer(){try{if(settings.enabled()&&settings.overlay()&&Settings.canDrawOverlays(this))startForegroundService(new Intent(this,CatOverlayService.class));else stopService(new Intent(this,CatOverlayService.class));getWindow().getDecorView().postDelayed(this::syncOverlay,350);}catch(SecurityException|IllegalStateException e){Toast.makeText(this,"지금은 고양이 터치 조작을 시작할 수 없어요. 다시 켜 주세요.",Toast.LENGTH_LONG).show();syncOverlay();}}

    void syncOverlay(){
        if(overlay==null)return;
        updatingSwitch=true;
        overlay.setChecked(settings.overlay());
        boolean home=wallpaperReturn!=null && wallpaperReturn.history.isCat();
        if(homePlay!=null)homePlay.setVisibility(home&&settings.enabled()?View.VISIBLE:View.GONE);
        power.setText(home?(settings.enabled()?"고양이 종료하기":"고양이 다시 켜기"):(PlayBackdrop.selected(settings)==PlayBackdrop.PHOTO?"내 사진으로 고양이 배경 적용":"홈 화면에 적용"));
        updatingSwitch=false;
    }
    @Override protected void onStart(){super.onStart();visibleActivities++;HomeSceneStore.notifyLayer(this);}
    @Override protected void onStop(){visibleActivities=Math.max(0,visibleActivities-1);HomeSceneStore.notifyLayer(this);super.onStop();}
    @Override protected void onResume(){super.onResume();if(preview!=null){preview.start();syncOverlay();}if(wallpaperReturn!=null)wallpaperReturn.resume();if(permissionPending){permissionPending=false;if(Settings.canDrawOverlays(this))enableOverlay();else syncOverlay();}if(settings.enabled()&&settings.overlay()&&!CatOverlayService.running&&Settings.canDrawOverlays(this))reconcileLayer();if(updatePanel!=null)updatePanel.resume();}
    @Override protected void onPause(){if(wallpaperReturn!=null)wallpaperReturn.pause();if(updatePanel!=null)updatePanel.pause();if(preview!=null)preview.stop();super.onPause();}
    @Override protected void onSaveInstanceState(Bundle b){b.putBoolean("pending_cross_app",permissionPending);if(updatePanel!=null)updatePanel.save(b);if(wallpaperReturn!=null)wallpaperReturn.save(b);super.onSaveInstanceState(b);}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==200)startMode();}
    @Override protected void onDestroy(){if(wallpaperReturn!=null)wallpaperReturn.close();if(updatePanel!=null)updatePanel.close();settings.prefs.unregisterOnSharedPreferenceChangeListener(preferences);super.onDestroy();}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(wallpaperReturn!=null)wallpaperReturn.result(request,result,data);}

    LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    LinearLayout card(LinearLayout parent,String title){LinearLayout c=column();c.setPadding(dp(18),dp(18),dp(18),dp(18));c.setBackground(round(Color.WHITE,22));add(parent,c,18);TextView t=text(title,17,INK);t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);c.addView(t);return c;}
    TextView text(String value,int size,int color){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setLineSpacing(dp(3),1);return t;}
    Button button(String label,boolean primary){Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(14);b.setTextColor(primary?Color.WHITE:PURPLE);b.setMinHeight(dp(primary?54:48));b.setGravity(Gravity.CENTER);b.setBackground(round(primary?PURPLE:0xfff2edf8,16));return b;}
    GradientDrawable round(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    void add(LinearLayout parent,View child,int top){LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(top);parent.addView(child,lp);}
}
