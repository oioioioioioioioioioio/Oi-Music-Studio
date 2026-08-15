package studio.oi.musiceditor;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import com.rmsl.juce.JuceActivity;

import java.io.File;
import java.io.IOException;

public final class OiJuceActivity extends JuceActivity
{
    private static final String UPDATE_AUTHORITY = "studio.oi.musiceditor.updateprovider";
    private static final String APK_MIME_TYPE = "application/vnd.android.package-archive";

    private String pendingUpdateApkPath;

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        if (Build.VERSION.SDK_INT >= 28)
        {
            final WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode
                = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes (attributes);
        }

        enterImmersiveMode();
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        enterImmersiveMode();

        if (pendingUpdateApkPath != null
            && (Build.VERSION.SDK_INT < 26
                || getPackageManager().canRequestPackageInstalls()))
            launchPendingUpdate();
    }

    @Override
    public void onWindowFocusChanged (boolean hasFocus)
    {
        super.onWindowFocusChanged (hasFocus);
        if (hasFocus)
            enterImmersiveMode();
    }

    public boolean installDownloadedApk (String apkPath)
    {
        final File packageFile = verifyUpdatePackage (apkPath);
        if (packageFile == null)
            return false;

        pendingUpdateApkPath = packageFile.getAbsolutePath();
        if (Build.VERSION.SDK_INT >= 26
            && ! getPackageManager().canRequestPackageInstalls())
        {
            try
            {
                final Intent settingsIntent = new Intent (
                    Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse ("package:" + getPackageName()));
                startActivity (settingsIntent);
                return true;
            }
            catch (ActivityNotFoundException ignored)
            {
                pendingUpdateApkPath = null;
                return false;
            }
        }

        return launchPendingUpdate();
    }

    private boolean launchPendingUpdate()
    {
        final File packageFile = verifyUpdatePackage (pendingUpdateApkPath);
        if (packageFile == null)
        {
            pendingUpdateApkPath = null;
            return false;
        }

        final Uri packageUri = new Uri.Builder()
            .scheme ("content")
            .authority (UPDATE_AUTHORITY)
            .appendPath ("updates")
            .appendPath (packageFile.getName())
            .build();
        final Intent installIntent = new Intent (Intent.ACTION_VIEW);
        installIntent.setDataAndType (packageUri, APK_MIME_TYPE);
        installIntent.setClipData (ClipData.newRawUri ("0i-Studio update", packageUri));
        installIntent.addFlags (Intent.FLAG_GRANT_READ_URI_PERMISSION);

        try
        {
            pendingUpdateApkPath = null;
            startActivity (installIntent);
            return true;
        }
        catch (ActivityNotFoundException | SecurityException ignored)
        {
            return false;
        }
    }

    @SuppressWarnings ("deprecation")
    private File verifyUpdatePackage (String apkPath)
    {
        if (apkPath == null || apkPath.isEmpty())
            return null;

        try
        {
            final File updateRoot = new File (getApplicationInfo().dataDir,
                                              ".temp/updates").getCanonicalFile();
            final File packageFile = new File (apkPath).getCanonicalFile();
            if (! updateRoot.equals (packageFile.getParentFile())
                || ! packageFile.getName().endsWith (".apk")
                || ! packageFile.isFile()
                || packageFile.length() <= 0)
                return null;

            final PackageManager packageManager = getPackageManager();
            final int flags = Build.VERSION.SDK_INT >= 28
                ? PackageManager.GET_SIGNING_CERTIFICATES
                : PackageManager.GET_SIGNATURES;
            final PackageInfo candidate = packageManager.getPackageArchiveInfo (
                packageFile.getAbsolutePath(), flags);
            final PackageInfo installed = packageManager.getPackageInfo (
                getPackageName(), flags);
            if (candidate == null || ! getPackageName().equals (candidate.packageName))
                return null;

            final long candidateVersion = Build.VERSION.SDK_INT >= 28
                ? candidate.getLongVersionCode() : candidate.versionCode;
            final long installedVersion = Build.VERSION.SDK_INT >= 28
                ? installed.getLongVersionCode() : installed.versionCode;
            if (candidateVersion <= installedVersion)
                return null;

            return signaturesMatch (candidate, installed) ? packageFile : null;
        }
        catch (IOException | PackageManager.NameNotFoundException ignored)
        {
            return null;
        }
    }

    @SuppressWarnings ("deprecation")
    private static Signature[] signaturesFor (PackageInfo packageInfo)
    {
        if (Build.VERSION.SDK_INT >= 28 && packageInfo.signingInfo != null)
            return packageInfo.signingInfo.hasMultipleSigners()
                ? packageInfo.signingInfo.getApkContentsSigners()
                : packageInfo.signingInfo.getSigningCertificateHistory();

        return packageInfo.signatures != null ? packageInfo.signatures : new Signature[0];
    }

    private static boolean signaturesMatch (PackageInfo candidate, PackageInfo installed)
    {
        final Signature[] candidateSignatures = signaturesFor (candidate);
        final Signature[] installedSignatures = signaturesFor (installed);
        if (candidateSignatures.length == 0 || installedSignatures.length == 0)
            return false;

        for (Signature candidateSignature : candidateSignatures)
            for (Signature installedSignature : installedSignatures)
                if (candidateSignature.equals (installedSignature))
                    return true;

        return false;
    }

    @SuppressWarnings ("deprecation")
    private void enterImmersiveMode()
    {
        final Window window = getWindow();
        final View decorView = window.getDecorView();

        if (Build.VERSION.SDK_INT >= 30)
        {
            window.setDecorFitsSystemWindows (false);
            final WindowInsetsController controller = decorView.getWindowInsetsController();
            if (controller != null)
            {
                controller.hide (WindowInsets.Type.statusBars()
                                 | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior (
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            return;
        }

        decorView.setSystemUiVisibility (
              View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
