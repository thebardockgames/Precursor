package com.opengoalandroid.jak1;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;

/**
 * Read-only access to the bug-report zips in cache/reports, so they can be shared with any app
 * (Discord, mail, Drive) through a temporary URI grant. No AndroidX dependency. (AI-assisted)
 */
public final class ReportProvider extends ContentProvider {
    static Uri uriFor(android.content.Context context, File report) {
        return Uri.parse("content://" + context.getPackageName() + ".reports/" + Uri.encode(report.getName()));
    }

    private File fileFor(Uri uri) throws FileNotFoundException {
        String name = uri.getLastPathSegment();
        if (name == null || !name.matches("precursor-reporte-[0-9-]+\\.zip")) throw new FileNotFoundException(String.valueOf(uri));
        File file = new File(ReportBuilder.reportsDir(getContext()), name);
        if (!file.isFile()) throw new FileNotFoundException(name);
        return file;
    }

    @Override public boolean onCreate() { return true; }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new SecurityException("Solo lectura");
        return ParcelFileDescriptor.open(fileFor(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public String getType(Uri uri) { return "application/zip"; }

    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        File file;
        try { file = fileFor(uri); } catch (FileNotFoundException missing) { return null; }
        MatrixCursor cursor = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        cursor.addRow(new Object[]{file.getName(), file.length()});
        return cursor;
    }

    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }
}
