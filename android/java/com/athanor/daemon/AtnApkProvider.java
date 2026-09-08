package com.athanor.daemon;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.UriMatcher;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Serves staged tunnel APK for ACTION_INSTALL_PACKAGE fallback (DEC-0048).
 * No AndroidX FileProvider — lab APK is packed without support libs.
 */
public final class AtnApkProvider extends ContentProvider {
    public static final String AUTHORITY = "com.athanor.daemon.update";
    private static final int CODE_APK = 1;
    private static final UriMatcher MATCHER = new UriMatcher(UriMatcher.NO_MATCH);

    static {
        MATCHER.addURI(AUTHORITY, "apk", CODE_APK);
    }

    public static Uri apkUri() {
        return Uri.parse("content://" + AUTHORITY + "/apk");
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode)
            throws FileNotFoundException {
        if (MATCHER.match(uri) != CODE_APK) {
            throw new FileNotFoundException("unsupported uri");
        }
        Context ctx = getContext();
        if (ctx == null) {
            throw new FileNotFoundException("no context");
        }
        File f = new File(ctx.getFilesDir(), "atn-update.apk");
        if (!f.isFile()) {
            throw new FileNotFoundException("staged apk missing");
        }
        int flags = ParcelFileDescriptor.MODE_READ_ONLY;
        return ParcelFileDescriptor.open(f, flags);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        if (MATCHER.match(uri) == CODE_APK) {
            return "application/vnd.android.package-archive";
        }
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }
}
