package com.valuewander.yeopjaricat;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;

/** A single private, verified APK. No arbitrary paths or write access are exposed. */
public final class UpdateFileProvider extends ContentProvider {
    static Uri uri(android.content.Context c){return Uri.parse("content://"+c.getPackageName()+".updates/apk");}
    @Override public boolean onCreate(){return true;}
    void check(Uri uri){if(!"/apk".equals(uri.getPath())||uri.getQuery()!=null)throw new IllegalArgumentException("Unknown update URI");}
    @Override public String getType(Uri uri){check(uri);return "application/vnd.android.package-archive";}
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException{check(uri);if(!"r".equals(mode))throw new FileNotFoundException("Read only");return ParcelFileDescriptor.open(new File(getContext().getCacheDir(),"updates/update.apk"),ParcelFileDescriptor.MODE_READ_ONLY);}
    @Override public Cursor query(Uri uri,String[] projection,String selection,String[] args,String sort){check(uri);String[] columns=projection==null?new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE}:projection;MatrixCursor result=new MatrixCursor(columns);File file=new File(getContext().getCacheDir(),"updates/update.apk");Object[] row=new Object[columns.length];for(int i=0;i<columns.length;i++)row[i]=OpenableColumns.DISPLAY_NAME.equals(columns[i])?"YeopjariCat-update.apk":OpenableColumns.SIZE.equals(columns[i])?file.length():null;result.addRow(row);return result;}
    @Override public Uri insert(Uri uri,ContentValues value){throw new UnsupportedOperationException();}
    @Override public int delete(Uri uri,String where,String[] args){throw new UnsupportedOperationException();}
    @Override public int update(Uri uri,ContentValues values,String where,String[] args){throw new UnsupportedOperationException();}
}
