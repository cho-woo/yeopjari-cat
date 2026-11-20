package com.valuewander.yeopjaricat;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Set;
import java.util.function.LongConsumer;

final class UpdateClient {
    interface Network{InputStream open(String address)throws IOException;}
    final Context context;final Network network;
    UpdateClient(Context context){this(context,UpdateClient::https);}
    UpdateClient(Context context,Network network){this.context=context.getApplicationContext();this.network=network;}
    static InputStream https(String address)throws IOException{
        for(int redirect=0;redirect<6;redirect++){
            if(!UpdatePolicy.trustedHttps(address))throw new IOException("Untrusted update address");
            HttpURLConnection connection=(HttpURLConnection)new URL(address).openConnection();connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(15000);connection.setReadTimeout(30000);connection.setRequestProperty("User-Agent","Valuewander-YeopjariCat-Android");
            connection.setRequestProperty("Accept",address.startsWith("https://api.github.com/")?"application/vnd.github+json":"application/octet-stream");
            try{int status=connection.getResponseCode();
                if(status>=300&&status<400){String next=connection.getHeaderField("Location");if(next==null)throw new IOException("Missing redirect");address=new URL(new URL(address),next).toString();connection.disconnect();continue;}
                if(status!=200)throw new IOException("Update server unavailable: "+status);
                return new FilterInputStream(connection.getInputStream()){@Override public void close()throws IOException{try{super.close();}finally{connection.disconnect();}}};
            }catch(IOException failure){connection.disconnect();throw failure;}
        }throw new IOException("Too many redirects");
    }
    UpdatePolicy.Release latest(String installedName)throws Exception{
        ByteArrayOutputStream text=new ByteArrayOutputStream();
        try(InputStream in=network.open(UpdatePolicy.API)){byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1){if(text.size()+n>4*1024*1024)throw new IOException("Update response too large");text.write(buffer,0,n);}}
        return select(new JSONArray(text.toString(StandardCharsets.UTF_8.name())),installedName);
    }
    static UpdatePolicy.Release select(JSONArray releases,String installedName)throws Exception{
        UpdatePolicy.Version current=UpdatePolicy.version(installedName);if(current==null)throw new IOException("Unknown installed channel");
        UpdatePolicy.Release best=null;
        for(int i=0;i<releases.length();i++){
            JSONObject entry=releases.getJSONObject(i);if(entry.optBoolean("draft"))continue;
            UpdatePolicy.Version version=UpdatePolicy.tag(entry.optString("tag_name"));if(version==null||version.compareTo(current)<=0||(best!=null&&version.compareTo(best.version)<=0))continue;
            JSONArray assets=entry.optJSONArray("assets");if(assets==null)continue;
            for(int j=0;j<assets.length();j++){JSONObject asset=assets.getJSONObject(j);if(!UpdatePolicy.filename(version).equals(asset.optString("name")))continue;
                try{best=new UpdatePolicy.Release(version,asset.optString("browser_download_url"),asset.optString("digest"),asset.optLong("size"));}catch(IllegalArgumentException invalid){/* Never install an unverified or foreign asset. */}
            }
        }return best;
    }
    static JSONObject json(UpdatePolicy.Release r)throws Exception{return new JSONObject().put("tag",r.version.tag()).put("url",r.url).put("digest","sha256:"+r.sha256).put("bytes",r.bytes);}
    static UpdatePolicy.Release json(String value)throws Exception{JSONObject j=new JSONObject(value);return new UpdatePolicy.Release(UpdatePolicy.tag(j.getString("tag")),j.getString("url"),j.getString("digest"),j.getLong("bytes"));}
    File file(){return new File(context.getCacheDir(),"updates/update.apk");}
    File download(UpdatePolicy.Release release,LongConsumer progress)throws Exception{
        File result=file(),part=new File(result.getParentFile(),"update.download.apk");if(!result.getParentFile().isDirectory()&&!result.getParentFile().mkdirs())throw new IOException("No update directory");
        try{
            try(InputStream in=network.open(release.url);FileOutputStream out=new FileOutputStream(part)){UpdatePolicy.copyVerified(in,out,release.bytes,release.sha256,progress);out.getFD().sync();}
            verifyArchive(part,release);if(!part.renameTo(result))throw new IOException("Cannot stage update");return result;
        }finally{if(part.exists()&&!part.delete())part.deleteOnExit();}
    }
    void verifyReady(UpdatePolicy.Release release)throws Exception{
        try(InputStream in=new FileInputStream(file())){UpdatePolicy.copyVerified(in,new OutputStream(){@Override public void write(int b){}@Override public void write(byte[] b,int offset,int count){}},release.bytes,release.sha256,n->{});}
        verifyArchive(file(),release);
    }
    @SuppressWarnings("deprecation") void verifyArchive(File file,UpdatePolicy.Release release)throws Exception{
        PackageManager manager=context.getPackageManager();int flags=Build.VERSION.SDK_INT>=28?PackageManager.GET_SIGNING_CERTIFICATES:PackageManager.GET_SIGNATURES;
        PackageInfo incoming=manager.getPackageArchiveInfo(file.getAbsolutePath(),flags),installed=manager.getPackageInfo(context.getPackageName(),flags);
        if(incoming==null||!context.getPackageName().equals(incoming.packageName)||!release.version.name().equals(incoming.versionName))throw new IOException("Wrong application package");
        long next=Build.VERSION.SDK_INT>=28?incoming.getLongVersionCode():incoming.versionCode,current=Build.VERSION.SDK_INT>=28?installed.getLongVersionCode():installed.versionCode;
        if(next<=current)throw new IOException("Update must be newer");
        if(incoming.applicationInfo!=null&&incoming.applicationInfo.minSdkVersion>Build.VERSION.SDK_INT)throw new IOException("Update requires a newer Android version");
        Signature[] a=Build.VERSION.SDK_INT>=28?(incoming.signingInfo==null?null:incoming.signingInfo.getApkContentsSigners()):incoming.signatures;
        Signature[] b=Build.VERSION.SDK_INT>=28?(installed.signingInfo==null?null:installed.signingInfo.getApkContentsSigners()):installed.signatures;
        if(a==null||b==null||a.length==0||!certificates(a).equals(certificates(b)))throw new IOException("Update signer differs");
    }
    static Set<String> certificates(Signature[] signatures)throws Exception{Set<String> result=new HashSet<>();for(Signature s:signatures)result.add(UpdatePolicy.hex(MessageDigest.getInstance("SHA-256").digest(s.toByteArray())));return result;}
}
