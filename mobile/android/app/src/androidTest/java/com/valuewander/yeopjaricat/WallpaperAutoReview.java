package com.valuewander.yeopjaricat;

import android.app.WallpaperManager;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** One UiAutomation owner; accessibility clicks avoid the AVD's stalled gesture monitors. */
final class WallpaperAutoReview {
    final WallpaperReview review;final MotionSmoke test;final WallpaperHistory history;
    final JSONArray results=new JSONArray();int osWaits;
    WallpaperAutoReview(WallpaperReview r){review=r;test=r.test;history=WallpaperHistory.get(r.context);}
    AccessibilityNodeInfo node(String label){
        AccessibilityNodeInfo root=test.getUiAutomation().getRootInActiveWindow();if(root==null)return null;
        for(AccessibilityNodeInfo n:root.findAccessibilityNodeInfosByText(label))if(n.getText()!=null&&n.getText().toString().equals(label)&&n.isVisibleToUser())return n;
        return null;
    }
    boolean action(AccessibilityNodeInfo n){while(n!=null){if(n.isClickable()&&n.isEnabled())return n.performAction(AccessibilityNodeInfo.ACTION_CLICK);n=n.getParent();}return false;}
    void await(java.util.function.BooleanSupplier condition,String message)throws Exception{
        long end=SystemClock.uptimeMillis()+45000;
        while(SystemClock.uptimeMillis()<end){
            if(condition.getAsBoolean())return;
            // Only dismiss unrelated system ANRs on this disposable AVD. Never
            // hide a cat-app ANR or count an obscured screenshot as verification.
            if(node("System UI isn't responding")!=null||node("Process system isn't responding")!=null){AccessibilityNodeInfo wait=node("Wait");if(wait!=null&&action(wait))osWaits++;}
            Thread.sleep(150);
        }throw new AssertionError(message);
    }
    void click(String label)throws Exception{await(()->{AccessibilityNodeInfo n=node(label);return n!=null&&action(n);},"Missing actual UI control "+label);Thread.sleep(250);}
    void shot(String name)throws Exception{
        await(()->node("홈 화면에 데려오기")!=null,"App not visible for screenshot");Thread.sleep(500);
        Bitmap b=test.getUiAutomation().takeScreenshot();test.check(b!=null,"Screenshot missing");
        try(FileOutputStream out=new FileOutputStream(new File(review.directory,name+".png"))){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();
    }
    void check(boolean value,String label)throws Exception{test.check(value,label);results.put(new JSONObject().put(label,true));Bundle progress=new Bundle();progress.putString("stream","PASS "+label+"\n");test.sendStatus(1,progress);}
    int lock(){return review.manager.getWallpaperId(WallpaperManager.FLAG_LOCK);}
    int separateLock()throws Exception{review.redLock();return lock();}
    int color(){android.app.WallpaperColors c=review.manager.getWallpaperColors(WallpaperManager.FLAG_SYSTEM);return c==null||c.getPrimaryColor()==null?0:c.getPrimaryColor().toArgb();}
    void picker()throws Exception{
        int before=history.id();click("Set wallpaper");await(()->history.id()!=before||node("Home screen")!=null,"Native destination missing");
        if(history.id()==before)click("Home screen");
        await(()->node("홈 화면에 데려오기")!=null&&history.phase().isEmpty(),"Native picker did not return to the app");
    }
    void run()throws Exception{
        int original=history.id(),blue=color();shot("01-original");
        click("홈 화면에 데려오기");click("이전 사진 보관");await(()->node("Set wallpaper")!=null,"Photo backup did not open native preview");
        test.getUiAutomation().performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK);
        await(()->history.phase().isEmpty(),"Cancel did not reconcile");check(history.id()==original&&!history.available(),"cancel_preserves_original_and_backup");
        click("홈 화면에 데려오기");click("이전 사진 보관");picker();await(()->history.isCat()&&history.read("saved_").kind.equals("image"),"Photo backup not committed");
        String file=history.read("saved_").file;int locked=separateLock();shot("02-photo-backed-up");click("홈 화면에 데려오기");check(file.equals(history.read("saved_").file),"reapplying_cat_keeps_original_backup");
        click("이전 배경으로 돌아가기");click("보관한 이전 사진으로 되돌리기");await(()->!history.isCat()&&!history.busy&&color()==blue,"Photo not restored");
        check(!new PetSettings(review.context).enabled()&&lock()==locked,"photo_restore_stops_cats_and_preserves_lock");shot("03-photo-restored");
        WallpaperHistory reloaded=new WallpaperHistory(review.context);check(reloaded.available()&&reloaded.read("saved_").file.equals(file),"private_backup_survives_store_recreation");reloaded.worker.shutdown();
        test.runOnMainSync(()->test.activity.startActivityForResult(new Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,new ComponentName("com.valuewander.yeopjaricat.test",FixtureWallpaperService.class.getName())),499));picker();
        check(history.info()!=null&&history.info().getComponent().getClassName().contains("FixtureWallpaperService"),"independent_live_wallpaper_selected");
        click("홈 화면에 데려오기");await(()->node("Set wallpaper")!=null,"Live capture not opened");test.getUiAutomation().performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK);
        await(()->history.phase().isEmpty(),"Live cancel not reconciled");check(file.equals(history.read("saved_").file)&&!new PetSettings(review.context).enabled(),"live_cancel_keeps_photo_backup_and_disabled_state");
        click("홈 화면에 데려오기");picker();check(history.read("saved_").kind.equals("live"),"previous_live_component_saved");
        click("이전 배경으로 돌아가기");click("이전 움직이는 배경으로 되돌리기");picker();
        check(history.info()!=null&&history.info().getComponent().getClassName().contains("FixtureWallpaperService")&&!new PetSettings(review.context).enabled(),"previous_live_restored_through_native_picker");shot("04-live-restored");
        click("홈 화면에 데려오기");picker();locked=separateLock();test.runOnMainSync(()->{history.prefs.edit().clear().apply();history.notice="";history.cleanup();history.changed();});
        click("이전 배경으로 돌아가기");check(node("보관한 이전 사진으로 되돌리기")==null,"legacy_menu_does_not_claim_missing_photo");click("휴대폰 기본 배경으로 해제");await(()->!history.isCat()&&!history.busy,"Default return failed");
        check(!new PetSettings(review.context).enabled()&&lock()==locked,"legacy_default_return_preserves_lock_and_stops_cats");
        click("홈 화면에 데려오기");picker();check(history.read("saved_").kind.equals("default"),"known_default_is_retained");locked=separateLock();
        click("이전 배경으로 돌아가기");click("보관한 기본 배경으로 되돌리기");await(()->!history.isCat()&&!history.busy,"Known default return failed");check(lock()==locked,"known_default_round_trip_preserves_lock");
        click("홈 화면에 데려오기");picker();locked=separateLock();test.runOnMainSync(()->{history.prefs.edit().clear().apply();history.notice="";history.cleanup();history.changed();});
        click("이전 배경으로 돌아가기");click("이전 사진을 골라서 되돌리기");await(()->!history.isCat()&&!history.busy&&history.read("saved_").kind.equals("image")&&color()==blue,"Manual legacy photo return failed");
        check(!new PetSettings(review.context).enabled()&&lock()==locked&&history.available(),"manual_legacy_photo_return_preserves_lock_and_backup");shot("05-manual-photo-restored");
        JSONObject report=new JSONObject().put("passed",true).put("scope","Android15 AVD, one UiAutomation owner clicking actual app and native-picker controls; synthetic document-picker result, real WallpaperManager; independent Lock seeded after OS picker for direct restore checks").put("unrelated_system_anrs_dismissed",osWaits).put("results",results);
        Files.write(new File(review.directory,"wallpaper-return-report.json").toPath(),report.toString(2).getBytes(StandardCharsets.UTF_8));Bundle done=new Bundle();done.putString("stream",report.toString());test.finish(MotionSmoke.RESULT_OK,done);
    }
}
