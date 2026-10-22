package com.opengoalandroid.jak1;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.InterruptedIOException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The update dialogs, shared by the launcher (daily check, installer results) and Settings
 * ("Check now"): offer, permission, backup + download with progress, then Android's installer.
 * (AI-assisted)
 */
final class UpdateFlow {
    private UpdateFlow() {}

    /** A release waiting for "install unknown apps" to be allowed in Android's settings. */
    private static Versions.Release waiting;
    /**
     * An offer the player has not answered yet. Turning the phone rebuilds the screen and takes the
     * dialog with it, so resume() shows it again until a button (or Back) answers it.
     */
    private static Versions.Release offered;
    private static boolean offeredManually;
    private static AlertDialog offerDialog;
    /** The launcher or Settings screen currently in front, where a late answer is shown. */
    private static java.lang.ref.WeakReference<Activity> front = new java.lang.ref.WeakReference<>(null);

    /** Call from onResume: shows an unanswered offer again, or continues once the permission was granted. */
    static void resume(Activity activity) {
        front = new java.lang.ref.WeakReference<>(activity);
        if (waiting != null && activity.getPackageManager().canRequestPackageInstalls()) {
            Versions.Release release = waiting;
            waiting = null;
            start(activity, release);
        } else if (offered != null && (offerDialog == null || !offerDialog.isShowing())) {
            offer(activity, offered, offeredManually);
        }
    }

    /** manual: the player asked, so also report "up to date" and errors. */
    static void check(Activity activity, boolean manual) {
        if (manual) Toast.makeText(activity, R.string.update_checking, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                Versions.Release release = Updater.check(activity);
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) {
                        // The screen was rebuilt (phone turned) while checking: show it on the new one.
                        if (release != null && (manual || !Updater.skipped(activity, release))) {
                            offered = release;
                            offeredManually = manual;
                            Activity now = front.get();
                            if (now != null && now != activity && !now.isFinishing() && !now.isDestroyed()) resume(now);
                        }
                        return;
                    }
                    if (release == null) {
                        if (manual) error(activity, activity.getString(R.string.update_none, BuildConfig.VERSION_NAME));
                    } else if (manual || !Updater.skipped(activity, release)) {
                        offer(activity, release, manual);
                    }
                });
            } catch (Exception failure) {
                android.util.Log.w("OpenGOAL", "Update check failed", failure);
                if (manual) activity.runOnUiThread(() -> error(activity, activity.getString(Updater.isNetworkError(failure)
                        ? R.string.update_offline : R.string.update_check_failed, String.valueOf(failure.getMessage()))));
            }
        }, "Update check").start();
    }

    private static void offer(Activity activity, Versions.Release release, boolean manual) {
        String notes = Updater.notes(release);
        String message = (notes.isEmpty() ? "" : notes + "\n\n") + activity.getString(R.string.update_keeps_data);
        offered = release;
        offeredManually = manual;
        AlertDialog.Builder dialog = new AlertDialog.Builder(activity)
                .setTitle(activity.getString(release.prerelease ? R.string.update_available_alpha : R.string.update_available, release.version))
                .setMessage(message)
                .setPositiveButton(R.string.update_now, (d, w) -> { offered = null; start(activity, release); })
                .setNegativeButton(R.string.not_now, (d, w) -> offered = null)
                .setOnCancelListener(d -> offered = null);
        if (!manual) dialog.setNeutralButton(R.string.update_skip, (d, w) -> { offered = null; Updater.skip(activity, release.version); });
        offerDialog = dialog.show();
    }

    /** Backs up saves and settings, downloads the APK, then hands it to Android's installer. */
    private static void start(Activity activity, Versions.Release release) {
        if (LauncherStore.installing(activity)) { error(activity, activity.getString(R.string.already_preparing)); return; }
        if (gameRunning(activity)) { error(activity, activity.getString(R.string.update_close_game)); return; }
        if (!activity.getPackageManager().canRequestPackageInstalls()) {
            new AlertDialog.Builder(activity).setTitle(R.string.update_permission_title)
                    .setMessage(R.string.update_permission_body)
                    .setNegativeButton(R.string.cancel, null)
                    .setPositiveButton(R.string.update_open_settings, (d, w) -> {
                        waiting = release;
                        try {
                            activity.startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                    Uri.parse("package:" + activity.getPackageName())));
                        } catch (ActivityNotFoundException missing) {
                            waiting = null;
                            error(activity, activity.getString(R.string.update_permission_body));
                        }
                    }).show();
            return;
        }
        ProgressBar bar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        TextView status = UiKit.label(activity, activity.getString(R.string.update_backing_up), 13, UiKit.MUTED, false);
        LinearLayout box = UiKit.column(activity);
        box.setPadding(UiKit.dp(activity, 24), UiKit.dp(activity, 12), UiKit.dp(activity, 24), 0);
        box.addView(bar, new LinearLayout.LayoutParams(-1, -2));
        UiKit.margin(activity, box, status, 8);
        AtomicBoolean cancelled = new AtomicBoolean();
        // Rebuilding the screen mid-download would lose the progress dialog.
        int orientation = activity.getRequestedOrientation();
        activity.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED);
        Runnable restore = () -> activity.setRequestedOrientation(orientation);
        AlertDialog progress = new AlertDialog.Builder(activity)
                .setTitle(activity.getString(R.string.update_downloading, release.version)).setView(box).setCancelable(false)
                .setNegativeButton(R.string.cancel, (d, w) -> cancelled.set(true)).show();
        new Thread(() -> {
            try {
                Backups.snapshot(activity, Backups.UPDATE);
                File apk = Updater.download(activity, release, (done, total) -> activity.runOnUiThread(() -> {
                    if (total > 0) bar.setProgress((int) (done * 1000 / total));
                    else bar.setIndeterminate(true);
                    status.setText(activity.getString(R.string.update_progress, done / 1_000_000, Math.max(0, total) / 1_000_000));
                }), cancelled);
                activity.runOnUiThread(() -> {
                    close(progress);
                    restore.run();
                    try { Updater.install(activity, apk); }
                    catch (Exception failure) { error(activity, activity.getString(R.string.update_failed, String.valueOf(failure.getMessage()))); }
                });
            } catch (InterruptedIOException stopped) {
                activity.runOnUiThread(() -> { close(progress); restore.run(); });
                Updater.discardDownloads(activity);
            } catch (Exception failure) {
                android.util.Log.w("OpenGOAL", "Update failed", failure);
                Updater.discardDownloads(activity);
                activity.runOnUiThread(() -> {
                    close(progress);
                    restore.run();
                    if (activity.isDestroyed()) return;
                    error(activity, activity.getString(Updater.isNetworkError(failure) ? R.string.update_offline : R.string.update_failed,
                            String.valueOf(failure.getMessage())));
                });
            }
        }, "Download update").start();
    }

    /** Android's installer reports to the launcher: ask the player to confirm, or explain a failure. */
    static void handleInstallStatus(Activity activity, Intent intent) {
        if (intent == null || !Updater.ACTION_INSTALL_STATUS.equals(intent.getAction())) return;
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, Integer.MIN_VALUE);
        intent.setAction(null);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) activity.startActivity(confirm);
        } else if (status != PackageInstaller.STATUS_SUCCESS && status != PackageInstaller.STATUS_FAILURE_ABORTED) {
            String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            error(activity, activity.getString(R.string.update_failed, message == null ? String.valueOf(status) : message));
        }
    }

    /** Whether a game process is running (its files must not change underneath it). */
    static boolean gameRunning(Activity activity) {
        java.util.List<ActivityManager.RunningAppProcessInfo> processes =
                activity.getSystemService(ActivityManager.class).getRunningAppProcesses();
        if (processes == null) return false;
        for (ActivityManager.RunningAppProcessInfo process : processes)
            if ((activity.getPackageName() + ":game").equals(process.processName)) return true;
        return false;
    }

    private static void close(AlertDialog dialog) {
        try { dialog.dismiss(); } catch (IllegalArgumentException detached) { /* its screen is already gone */ }
    }

    private static void error(Activity activity, String text) {
        new AlertDialog.Builder(activity).setTitle(R.string.app_name).setMessage(text).setPositiveButton(R.string.ok, null).show();
    }
}
