package studio.oi.musiceditor;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.List;

public final class OiUpdateContentProvider extends ContentProvider
{
    private static final String APK_MIME_TYPE = "application/vnd.android.package-archive";

    @Override
    public boolean onCreate()
    {
        return true;
    }

    @Override
    public String getType (Uri uri)
    {
        resolveFileOrThrow (uri);
        return APK_MIME_TYPE;
    }

    @Override
    public Cursor query (Uri uri, String[] projection, String selection,
                         String[] selectionArgs, String sortOrder)
    {
        final File file = resolveFileOrThrow (uri);
        final String[] columns = projection != null ? projection
            : new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
        final MatrixCursor cursor = new MatrixCursor (columns, 1);
        final MatrixCursor.RowBuilder row = cursor.newRow();
        for (String column : columns)
        {
            if (OpenableColumns.DISPLAY_NAME.equals (column))
                row.add (file.getName());
            else if (OpenableColumns.SIZE.equals (column))
                row.add (file.length());
            else
                row.add (null);
        }
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openFile (Uri uri, String mode) throws FileNotFoundException
    {
        if (! "r".equals (mode))
            throw new FileNotFoundException ("Update packages are read-only");

        return ParcelFileDescriptor.open (resolveFile (uri),
                                          ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert (Uri uri, ContentValues values)
    {
        throw new UnsupportedOperationException ("Update packages are read-only");
    }

    @Override
    public int delete (Uri uri, String selection, String[] selectionArgs)
    {
        return 0;
    }

    @Override
    public int update (Uri uri, ContentValues values, String selection,
                       String[] selectionArgs)
    {
        return 0;
    }

    private File resolveFileOrThrow (Uri uri)
    {
        try
        {
            return resolveFile (uri);
        }
        catch (FileNotFoundException exception)
        {
            throw new IllegalArgumentException (exception.getMessage(), exception);
        }
    }

    private File resolveFile (Uri uri) throws FileNotFoundException
    {
        if (getContext() == null)
            throw new FileNotFoundException ("Provider context is unavailable");

        final List<String> segments = uri.getPathSegments();
        if (segments.size() != 2 || ! "updates".equals (segments.get (0)))
            throw new FileNotFoundException ("Invalid update URI");

        try
        {
            final File root = new File (getContext().getApplicationInfo().dataDir,
                                        ".temp/updates").getCanonicalFile();
            final File file = new File (root, segments.get (1)).getCanonicalFile();
            if (! root.equals (file.getParentFile())
                || ! file.getName().endsWith (".apk")
                || ! file.isFile())
                throw new FileNotFoundException ("Update package was not found");
            return file;
        }
        catch (IOException exception)
        {
            throw new FileNotFoundException ("Invalid update package path");
        }
    }
}
