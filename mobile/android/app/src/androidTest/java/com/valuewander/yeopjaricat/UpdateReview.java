package com.valuewander.yeopjaricat;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;

/** Dependency injection lives only in this separate test APK; no fixture route ships in the app. */
final class UpdateReview {
    final MotionSmoke test;final Context context;final File folder,fixtures;
    final JSONArray results=new JSONArray();
    UpdateReview(MotionSmoke test,File folder){this.test=test;context=test.getTargetContext();this.folder=folder;fixtures=new File(context.getExternalFilesDir(null),"update-fixtures");}
    void check(boolean value,String detail)throws Exception{test.check(value,detail);results.put(new JSONObject().put(detail,true));}
    String hash(File file)throws Exception{MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)digest.update(b,0,n);}return UpdatePolicy.hex(digest.digest());}
    UpdatePolicy.Release release(File file,String name)throws Exception{UpdatePolicy.Version v=UpdatePolicy.version(name);return new UpdatePolicy.Release(v,UpdatePolicy.url(v),"sha256:"+hash(file),file.length());}
    JSONObject entry(UpdatePolicy.Release r)throws Exception{return new JSONObject().put("tag_name",r.version.tag()).put("draft",false).put("assets",new JSONArray().put(new JSONObject().put("name",UpdatePolicy.filename(r.version)).put("browser_download_url",r.url).put("digest","sha256:"+r.sha256).put("size",r.bytes)));}
    UpdateClient fake(File apk,UpdatePolicy.Release r,JSONArray feed){return new UpdateClient(context,address->{if(address.equals(UpdatePolicy.API))return new ByteArrayInputStream(feed.toString().getBytes(StandardCharsets.UTF_8));if(address.equals(r.url))return new FileInputStream(apk);throw new IOException("Unexpected request");});}
    interface Attempt{void run()throws Exception;}
    void rejects(String name,Attempt attempt)throws Exception{try{attempt.run();}catch(IOException|IllegalArgumentException denied){check(true,name);return;}throw new AssertionError("Rejected update was accepted: "+name);}
    void run(String mode)throws Exception{
        File future=new File(fixtures,"future.apk");UpdatePolicy.Release next=release(future,"0.3.1-preview");JSONArray feed=new JSONArray().put(new JSONObject().put("tag_name","v99.0.0").put("assets",new JSONArray())).put(entry(next).put("draft",true)).put(entry(next));
        if(mode.equals("update-install")){installFlow(future,next,feed);return;}
        check(UpdateClient.select(feed,"0.3.0-preview").version.name().equals("0.3.1-preview"),"android_channel_ignores_windows_and_drafts");
        check(UpdateClient.select(feed,"0.3.1-preview")==null,"equal_version_not_offered");
        JSONObject invalid=entry(next);invalid.getJSONArray("assets").getJSONObject(0).put("digest","");check(UpdateClient.select(new JSONArray().put(invalid),"0.3.0-preview")==null,"missing_digest_not_offered");
        JSONObject foreign=entry(next);foreign.getJSONArray("assets").getJSONObject(0).put("browser_download_url","https://example.com/other.apk");check(UpdateClient.select(new JSONArray().put(foreign),"0.3.0-preview")==null,"foreign_download_not_offered");
        UpdateClient client=fake(future,next,feed);client.download(next,n->{});client.verifyReady(next);check(true,"correct_signed_newer_apk_downloaded_and_verified");
        File wrong=new File(fixtures,"wrong-key.apk");UpdatePolicy.Release wrongRelease=release(wrong,"0.3.1-preview");rejects("different_signer_rejected",()->fake(wrong,wrongRelease,feed).download(wrongRelease,n->{}));
        File current=new File(fixtures,"current.apk");UpdatePolicy.Release currentRelease=release(current,"0.3.0-preview");rejects("same_version_archive_rejected",()->client.verifyArchive(current,currentRelease));
        rejects("foreign_package_rejected",()->client.verifyArchive(new File(fixtures,"foreign.apk"),next));
        UpdateClient corrupt=new UpdateClient(context,address->new FilterInputStream(new FileInputStream(future)){boolean changed;@Override public int read(byte[] b,int offset,int length)throws IOException{int n=super.read(b,offset,length);if(!changed&&n>0){b[offset]^=1;changed=true;}return n;}});
        rejects("corrupted_bytes_rejected",()->corrupt.download(next,n->{}));client.verifyReady(next);check(true,"failed_transfer_preserves_verified_staged_apk");
        rejects("provider_write_rejected",()->new UpdateFileProvider().openFile(UpdateFileProvider.uri(context),"w"));rejects("provider_path_traversal_rejected",()->new UpdateFileProvider().check(Uri.parse("content://"+context.getPackageName()+".updates/../other")));
        // Real public HTTPS API and APK redirects; installed v0.3 must reject the older public APK.
        UpdateClient live=new UpdateClient(context);UpdatePolicy.Release publicRelease=live.latest("0.0.0-preview");check(publicRelease!=null,"real_public_android_release_found");
        try{live.download(publicRelease,n->{});throw new AssertionError("Public old/same APK installed");}catch(IOException expected){check("Update must be newer".equals(expected.getMessage()),"real_https_apk_transfer_verified_then_downgrade_rejected");}
        context.getSharedPreferences("app_updates",Context.MODE_PRIVATE).edit().clear().putString("release",UpdateClient.json(next).toString()).putBoolean("ready",true).apply();
        AppUpdates restored=new AppUpdates(context,client);test.waitFor(()->restored.state==AppUpdates.State.READY,"Staged update did not restore");check(true,"ready_update_survives_controller_recreation");restored.worker.shutdown();
        context.getSharedPreferences("app_updates",Context.MODE_PRIVATE).edit().clear().apply();
        AppUpdates offline=new AppUpdates(context,new UpdateClient(context,address->{throw new IOException("Offline");}));test.runOnMainSync(()->offline.check(true));test.waitFor(()->offline.state==AppUpdates.State.ERROR,"Offline check must not say latest");check(true,"offline_check_is_retryable_error");offline.worker.shutdown();
        context.getSharedPreferences("app_updates",Context.MODE_PRIVATE).edit().clear().apply();
        test.runOnMainSync(()->{AppUpdates.instance=new AppUpdates(context,new UpdateClient(context));});
        test.activity=(MainActivity)test.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        test.waitFor(()->test.activity.updatePanel.updates.state==AppUpdates.State.LATEST,"Current build did not show latest status");
        test.runOnMainSync(()->{android.view.View button=test.activity.updatePanel.button;button.requestRectangleOnScreen(new android.graphics.Rect(0,0,button.getWidth(),button.getHeight()),true);});Thread.sleep(250);test.shot("updater-current");
        JSONObject report=new JSONObject().put("passed",true).put("scope","Final v0.3 APK on Android15 AVD; native upgrade confirmation checked separately").put("results",results);
        Files.write(new File(folder,"update-report.json").toPath(),report.toString(2).getBytes(StandardCharsets.UTF_8));Bundle result=new Bundle();result.putString("stream",report.toString());test.finish(MotionSmoke.RESULT_OK,result);
    }
    void installFlow(File future,UpdatePolicy.Release next,JSONArray feed)throws Exception{
        context.getSharedPreferences("app_updates",Context.MODE_PRIVATE).edit().clear().apply();
        test.runOnMainSync(()->{
            new PetSettings(context).prefs.edit().putBoolean("pets_enabled",true).putInt("coat",8).putInt("companion_coat",10).putBoolean("companion",true).putString("upgrade_marker","keep-v03-settings").putFloat("position.home.0.y",.45f).putFloat("position.home.1.y",.53f).putBoolean("overlay",true).apply();
            AppUpdates.instance=new AppUpdates(context,fake(future,next,feed));
        });
        test.activity=(MainActivity)test.startActivitySync(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        // External system UI driver owns UiAutomation during this flow.
        test.waitFor(()->AppUpdates.instance.state==AppUpdates.State.AVAILABLE,"Update offer missing");Thread.sleep(500);
        Bundle status=new Bundle();status.putString("stream","READY_FOR_UPDATE_FLOW");test.sendStatus(1,status);
        // The external ADB driver cancels once, then confirms the real system installer.
        // Successful package replacement intentionally ends this instrumentation process.
        Thread.sleep(240000);throw new AssertionError("External install driver did not complete");
    }
}
