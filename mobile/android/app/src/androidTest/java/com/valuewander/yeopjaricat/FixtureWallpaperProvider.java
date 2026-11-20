package com.valuewander.yeopjaricat;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;

/** Synthetic blue test pixels only. This provider is absent from the release APK. */
public final class FixtureWallpaperProvider extends ContentProvider {
    static final int BLUE=0xff3b76af;
    File image;
    @Override public boolean onCreate(){try{image=new File(getContext().getCacheDir(),"review-blue.png");Bitmap b=Bitmap.createBitmap(128,128,Bitmap.Config.ARGB_8888);b.eraseColor(BLUE);try(FileOutputStream out=new FileOutputStream(image)){b.compress(Bitmap.CompressFormat.PNG,100,out);}b.recycle();return true;}catch(Exception failure){return false;}}
    @Override public String getType(Uri uri){return "image/png";}
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException{if(!"/blue".equals(uri.getPath())||!"r".equals(mode))throw new FileNotFoundException();return ParcelFileDescriptor.open(image,ParcelFileDescriptor.MODE_READ_ONLY);}
    @Override public Cursor query(Uri u,String[] p,String s,String[] a,String sort){MatrixCursor c=new MatrixCursor(new String[]{"_display_name","_size"});c.addRow(new Object[]{"Review blue.png",image.length()});return c;}
    @Override public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
    @Override public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
    @Override public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
}
