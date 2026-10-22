package com.opengoalandroid.jak1;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONArray;

/**
 * Updates Precursor from its GitHub releases (BuildConfig.UPDATE_REPO). The stable channel follows
 * normal releases; the alpha channel also takes those marked "pre-release". The APK is downloaded to
 * the cache, checked to be Precursor with a higher version, and handed to Android's installer, which
 * also requires the same signing key, so an update keeps all data. Only api.github.com and GitHub's
 * download servers are contacted, and only when checking or downloading. (AI-assisted)
 */
final class Updater {
    private Updater() {}

    static final String ACTION_INSTALL_STATUS = "io.github.thebardockgames.precursor.INSTALL_STATUS";
    private static final String ALPHA = "updateAlpha", AUTO = "updateAuto", LAST_CHECK = "updateLastCheck",
            SKIPPED = "updateSkipped", LAST_RUN = "lastRunVersionCode";
    private static final long CHECK_EVERY_MS = 20L * 60 * 60 * 1000;

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(GameOptionsActivity.PREFS, Context.MODE_PRIVATE);
    }

    static boolean alphaChannel(Context context) { return prefs(context).getBoolean(ALPHA, false); }
    static void setAlphaChannel(Context context, boolean alpha) { prefs(context).edit().putBoolean(ALPHA, alpha).apply(); }
    static boolean autoCheck(Context context) { return prefs(context).getBoolean(AUTO, true); }
    static void setAutoCheck(Context context, boolean auto) { prefs(context).edit().putBoolean(AUTO, auto).apply(); }
    static void skip(Context context, String version) { prefs(context).edit().putString(SKIPPED, version).apply(); }
    static boolean skipped(Context context, Versions.Release release) { return release.version.equals(prefs(context).getString(SKIPPED, "")); }

    /** True once a day while automatic checks are on. */
    static boolean dueForCheck(Context context) {
        return BuildConfig.OPENGOAL_LAUNCHER && autoCheck(context)
                && System.currentTimeMillis() - prefs(context).getLong(LAST_CHECK, 0) > CHECK_EVERY_MS;
    }

    /** The version code the player last acknowledged ("Precursor was updated"), 0 before the first run. */
    static int lastRun(Context context) { return prefs(context).getInt(LAST_RUN, 0); }

    static void markRun(Context context) { prefs(context).edit().putInt(LAST_RUN, BuildConfig.VERSION_CODE).apply(); }

    /** Synchronous: call off the UI thread. Null when Precursor is up to date in this channel. */
    static Versions.Release check(Context context) throws Exception {
        String url = "https://api.github.com/repos/" + BuildConfig.UPDATE_REPO + "/releases?per_page=30";
        // Debug builds only: a local copy of the releases list, to try updates without publishing.
        if (BuildConfig.DEBUG) url = prefs(context).getString("updateTestUrl", url);
        HttpURLConnection connection = open(url);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try {
            int status = connection.getResponseCode();
            if (status != 200) throw new IOException("GitHub HTTP " + status);
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) { InstallFiles.copy(input, body); }
            prefs(context).edit().putLong(LAST_CHECK, System.currentTimeMillis()).apply();
            return Versions.newest(new JSONArray(body.toString("UTF-8")), BuildConfig.VERSION_NAME, alphaChannel(context));
        } finally {
            connection.disconnect();
        }
    }

    interface Progress { void update(long done, long total); }

    /** Synchronous download to the cache. Stops with an InterruptedIOException when cancelled. */
    static File download(Context context, Versions.Release release, Progress progress, AtomicBoolean cancelled) throws Exception {
        File dir = new File(context.getCacheDir(), "updates");
        InstallFiles.removeTree(context.getCacheDir(), dir);
        InstallFiles.mkdir(dir);
        File apk = new File(dir, "precursor-" + release.version.replaceAll("[^A-Za-z0-9.-]", "") + ".apk");
        HttpURLConnection connection = open(release.apkUrl);
        try {
            int status = connection.getResponseCode();
            if (status != 200) throw new IOException("HTTP " + status);
            long total = release.size > 0 ? release.size : connection.getContentLengthLong();
            try (InputStream input = connection.getInputStream(); FileOutputStream output = new FileOutputStream(apk)) {
                byte[] buffer = new byte[256 * 1024];
                long done = 0;
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (cancelled.get()) throw new InterruptedIOException("cancelled");
                    output.write(buffer, 0, count);
                    done += count;
                    progress.update(done, total);
                }
                output.getFD().sync();
                if (total > 0 && done != total) throw new IOException("incomplete download");
            }
        } finally {
            connection.disconnect();
        }
        PackageInfo info = context.getPackageManager().getPackageArchiveInfo(apk.getPath(), 0);
        if (info == null || !context.getPackageName().equals(info.packageName)
                || info.getLongVersionCode() <= BuildConfig.VERSION_CODE)
            throw new IOException(context.getString(R.string.update_wrong_apk));
        return apk;
    }

    /** Leftover downloads from an update that was installed or abandoned. */
    static void discardDownloads(Context context) {
        try { InstallFiles.removeTree(context.getCacheDir(), new File(context.getCacheDir(), "updates")); }
        catch (IOException ignored) { }
    }

    /**
     * Hands the APK to Android's installer. The result arrives at LauncherActivity as
     * ACTION_INSTALL_STATUS (Android first asks the player to confirm).
     */
    static void install(Activity activity, File apk) throws IOException {
        PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(activity.getPackageName());
        params.setSize(apk.length());
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream input = new FileInputStream(apk);
                 OutputStream output = session.openWrite("precursor.apk", 0, apk.length())) {
                InstallFiles.copy(input, output);
                session.fsync(output);
            }
            Intent status = new Intent(activity, LauncherActivity.class).setAction(ACTION_INSTALL_STATUS)
                    .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            // Mutable: the installer adds the status extras.
            PendingIntent pending = PendingIntent.getActivity(activity, id, status,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
            session.commit(pending.getIntentSender());
        } catch (IOException | RuntimeException error) {
            installer.abandonSession(id);
            throw error;
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(15000);
        connection.setReadTimeout(30000);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent", "Precursor/" + BuildConfig.VERSION_NAME + " (Android)");
        return connection;
    }

    /** Release notes as plain text, short enough for a dialog. */
    static String notes(Versions.Release release) {
        String text = release.notes == null ? "" : release.notes
                .replaceAll("(?m)^#+\\s*", "").replaceAll("\\*\\*|__|`", "").replace("\r", "").trim();
        if (text.length() > 1500) text = text.substring(0, 1500).trim() + "…";
        return text;
    }

    static boolean isNetworkError(Throwable error) {
        return error instanceof java.net.UnknownHostException || error instanceof java.net.SocketTimeoutException
                || error instanceof java.net.ConnectException || error instanceof javax.net.ssl.SSLException;
    }
}
