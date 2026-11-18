package com.valuewander.yeopjaricat;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URI;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.function.LongConsumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** The public Android channel is separate from the Windows /releases/latest feed. */
final class UpdatePolicy {
    static final String API="https://api.github.com/repos/cho-woo/yeopjari-cat/releases?per_page=50";
    static final long MAX_APK=200L*1024*1024;
    static final class Version implements Comparable<Version> {
        final int major,minor,patch;
        Version(int major,int minor,int patch){this.major=major;this.minor=minor;this.patch=patch;}
        String number(){return major+"."+minor+"."+patch;}
        String name(){return number()+"-preview";}
        String tag(){return "v"+number()+"-android-preview";}
        @Override public int compareTo(Version other){int n=Integer.compare(major,other.major);if(n==0)n=Integer.compare(minor,other.minor);return n==0?Integer.compare(patch,other.patch):n;}
    }
    static Version version(String name){
        Matcher m=Pattern.compile("(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})-preview").matcher(name);
        return m.matches()?new Version(Integer.parseInt(m.group(1)),Integer.parseInt(m.group(2)),Integer.parseInt(m.group(3))):null;
    }
    static Version tag(String tag){if(!tag.startsWith("v")||!tag.endsWith("-android-preview"))return null;return version(tag.substring(1,tag.length()-16)+"-preview");}
    static String filename(Version version){return "YeopjariCat-Android-"+version.name()+".apk";}
    static String url(Version version){return "https://github.com/cho-woo/yeopjari-cat/releases/download/"+version.tag()+"/"+filename(version);}
    static final class Release {
        final Version version;final String url,sha256;final long bytes;
        Release(Version version,String url,String digest,long bytes){
            if(version==null||!UpdatePolicy.url(version).equals(url)||!digest.matches("sha256:[0-9a-fA-F]{64}")||bytes<=0||bytes>MAX_APK)throw new IllegalArgumentException("Invalid Android release");
            this.version=version;this.url=url;this.sha256=digest.substring(7).toLowerCase(Locale.ROOT);this.bytes=bytes;
        }
    }
    static boolean trustedHttps(String address){
        try{URI uri=new URI(address);String host=uri.getHost();return "https".equals(uri.getScheme())&&uri.getUserInfo()==null&&(uri.getPort()==-1||uri.getPort()==443)&&
            ("api.github.com".equals(host)||"github.com".equals(host)||"release-assets.githubusercontent.com".equals(host)||"objects.githubusercontent.com".equals(host)||"github-releases.githubusercontent.com".equals(host));}
        catch(Exception invalid){return false;}
    }
    static String hex(byte[] bytes){StringBuilder b=new StringBuilder();for(byte n:bytes)b.append(String.format(Locale.ROOT,"%02x",n&255));return b.toString();}
    static void copyVerified(InputStream in,OutputStream out,long bytes,String expected,LongConsumer progress)throws IOException{
        if(bytes<=0||bytes>MAX_APK)throw new IOException("Invalid APK size");
        try{MessageDigest digest=MessageDigest.getInstance("SHA-256");byte[] buffer=new byte[32768];long count=0;int length;
            while((length=in.read(buffer))!=-1){if(count+length>bytes)throw new IOException("APK exceeds expected size");out.write(buffer,0,length);digest.update(buffer,0,length);count+=length;progress.accept(count);}
            if(count!=bytes||!hex(digest.digest()).equals(expected))throw new IOException("APK integrity mismatch");
        }catch(java.security.NoSuchAlgorithmException unavailable){throw new IOException(unavailable);}
    }
}
