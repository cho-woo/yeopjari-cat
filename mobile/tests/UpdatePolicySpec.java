package com.valuewander.yeopjaricat;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.security.MessageDigest;

public final class UpdatePolicySpec {
    interface Attempt{void run()throws Exception;}
    static int passed;
    static void check(boolean result){if(!result)throw new AssertionError();passed++;}
    static void reject(Attempt attempt)throws Exception{try{attempt.run();}catch(IllegalArgumentException|java.io.IOException expected){passed++;return;}throw new AssertionError("Untrusted update accepted");}
    public static void main(String[] args)throws Exception{
        for(String v:new String[]{"0.1.0-preview","0.3.0-preview","1.10.100-preview","999999.2.3-preview"})check(UpdatePolicy.version(v)!=null&&UpdatePolicy.tag(UpdatePolicy.version(v).tag()).name().equals(v));
        for(String v:new String[]{"v2.0.41","v2.0.41-android","0.3.0","v0.3.0-android-preview.evil","v00.3.0-android-preview","v1000000.3.0-android-preview",""})check(UpdatePolicy.tag(v)==null);
        check(UpdatePolicy.version("0.10.0-preview").compareTo(UpdatePolicy.version("0.9.99-preview"))>0);
        check(UpdatePolicy.version("1.0.0-preview").compareTo(UpdatePolicy.version("0.99.99-preview"))>0);
        check(UpdatePolicy.version("0.3.0-preview").compareTo(UpdatePolicy.version("0.3.0-preview"))==0);
        for(String url:new String[]{"https://api.github.com/x","https://github.com/x","https://release-assets.githubusercontent.com/x?token=test","https://objects.githubusercontent.com/x"})check(UpdatePolicy.trustedHttps(url));
        for(String url:new String[]{"http://github.com/x","https://github.com.evil.test/x","https://github.com@evil.test/x","https://evil.test@github.com/x","file:///data/x","https://github.com:444/x","https://github.com./x","https://example.com/x"})check(!UpdatePolicy.trustedHttps(url));
        UpdatePolicy.Version version=UpdatePolicy.version("0.3.0-preview");String url=UpdatePolicy.url(version),digest="sha256:"+"a".repeat(64);
        check(new UpdatePolicy.Release(version,url,digest,24451579).bytes==24451579);
        reject(()->new UpdatePolicy.Release(version,url+"?other=1",digest,1));reject(()->new UpdatePolicy.Release(version,url,"",1));reject(()->new UpdatePolicy.Release(version,url,digest,0));reject(()->new UpdatePolicy.Release(version,url,digest,UpdatePolicy.MAX_APK+1));
        for(int size:new int[]{1,2,32767,32768,32769,65536}){
            byte[] bytes=new byte[size];for(int i=0;i<size;i++)bytes[i]=(byte)(i*37);
            String sha=UpdatePolicy.hex(MessageDigest.getInstance("SHA-256").digest(bytes));ByteArrayOutputStream out=new ByteArrayOutputStream();
            UpdatePolicy.copyVerified(new ByteArrayInputStream(bytes),out,size,sha,n->{});check(java.util.Arrays.equals(bytes,out.toByteArray()));
            reject(()->UpdatePolicy.copyVerified(new ByteArrayInputStream(bytes),new ByteArrayOutputStream(),size+1,sha,n->{}));
            reject(()->UpdatePolicy.copyVerified(new ByteArrayInputStream(bytes),new ByteArrayOutputStream(),size,"0".repeat(64),n->{}));
            if(size>1)reject(()->UpdatePolicy.copyVerified(new ByteArrayInputStream(bytes),new ByteArrayOutputStream(),size-1,sha,n->{}));
        }
        System.out.println("Update policy: "+passed+" channel, version, URL, size and integrity cases passed");
    }
}
