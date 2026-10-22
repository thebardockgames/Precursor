package com.opengoalandroid.jak1;

import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.system.Os;
import org.json.JSONObject;
import java.io.*;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;

/** One installation per fresh process, with a foreground notification and durable state. (AI-assisted) */
public final class InstallService extends Service {
    static final String CANCEL = "com.opengoalandroid.CANCEL_INSTALL";
    static final String REMOVE = "com.opengoalandroid.REMOVE_INSTALL";
    private boolean removing;
    private static final String CHANNEL = "installation";
    private static final int NOTIFICATION = 7;
    private boolean started;
    private volatile boolean ending;
    private String game = "";
    private RandomAccessFile lockFile;
    private FileLock lock;
    private PowerManager.WakeLock wakeLock;
    private native String nativeProbe(int descriptor, String expectedGame) throws IOException;
    private native String nativeInstall(String root, String iso, String log, String expectedGame) throws IOException;

    @Override public IBinder onBind(Intent intent) { return null; }

    /** Notifications and messages in Precursor's language. (AI-assisted) */
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(Language.wrap(base)); }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && CANCEL.equals(intent.getAction())) {
            if (removing) return START_NOT_STICKY;
            end("cancelled", getString(R.string.install_cancelled));
            return START_NOT_STICKY;
        }
        if (started) return START_NOT_STICKY;
        started = true;
        removing = intent != null && REMOVE.equals(intent.getAction());
        try {
            NotificationManager manager = getSystemService(NotificationManager.class);
            manager.createNotificationChannel(new NotificationChannel(CHANNEL,
                    getString(R.string.channel_installation), NotificationManager.IMPORTANCE_LOW));
            startForeground(NOTIFICATION, notification(getString(R.string.install_starting)));
            if (!BuildConfig.OPENGOAL_LAUNCHER || intent == null || (!removing && intent.getData() == null))
                throw new IOException(getString(R.string.install_no_iso));
            lockFile = new RandomAccessFile(new File(getFilesDir(), "install.lock"), "rw");
            lock = lockFile.getChannel().tryLock();
            if (lock == null) throw new IOException(getString(R.string.install_busy));
            if (removing) {
                game = intent.getStringExtra("game");
                new Thread(this::removeInstallation, "Remove installation").start();
                return START_NOT_STICKY;
            }
            game = intent.getStringExtra("game");
            if (!InstallFiles.validGame(game)) throw new IOException(getString(R.string.install_pick_game));
            wakeLock = getSystemService(PowerManager.class).newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK, getPackageName() + ":install");
            wakeLock.acquire(6 * 60 * 60 * 1000L);
            update("running", -1, getString(R.string.install_preparing), -1);
            Uri uri = intent.getData();
            // The compiler needs considerably more stack than a Java thread's default.
            new Thread(null, () -> install(uri), "OpenGOAL installer", 16 * 1024 * 1024L).start();
        } catch (Exception error) {
            end("error", message(this, error));
        }
        return START_NOT_STICKY;
    }

    private Notification notification(String text) {
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, LauncherActivity.class),
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        PendingIntent cancel = PendingIntent.getService(this, 1,
                new Intent(this, InstallService.class).setAction(CANCEL),
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle(getString(removing ? R.string.notify_removing : R.string.notify_preparing)).setContentText(text)
                .setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true);
        if (!removing) builder.addAction(new Notification.Action.Builder(null, getString(R.string.cancel), cancel).build());
        return builder.build();
    }

    /** stage: InstallProgress's, -1 before the stages start and for final states. */
    private synchronized void update(String state, int stage, String text, int percent) throws Exception {
        if (ending) return;
        LauncherStore.write(LauncherStore.job(this), new JSONObject()
                .put("state", state).put("stage", stage).put("message", text).put("game", game)
                .put("operation", removing ? "remove" : "install")
                .put("percent", percent).put("updated", System.currentTimeMillis()));
        getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(text));
    }

    // Called synchronously on the native worker, never from the SDL game process.
    public void onNativeProgress(String detectedGame, String text) throws Exception {
        game = detectedGame;
        update("running", InstallProgress.stage(text), nativeText(this, text), -1);
    }

    private static String message(Context context, Throwable error) {
        String text = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
        // The ISO reader reports in English. (AI-assisted)
        if (text.contains("ISO 9660") || text.contains("primary volume"))
            return context.getString(R.string.install_not_ps2_iso);
        if (text.contains("Truncated") || text.contains("truncated"))
            return context.getString(R.string.install_iso_damaged);
        return nativeText(context, text);
    }

    /** The native installer (jni/installer.cpp) reports in Spanish: its messages in Precursor's language. */
    private static final String[][] NATIVE_TEXTS = {
            {"Comprobando el juego y la edición de la ISO", "install_checking_game"},
            {"Extrayendo y verificando la ISO", "install_extracting"},
            {"Preparando escenarios, texturas y audio", "install_levels"},
            {"Compilando el juego para el teléfono", "install_compiling"},
            {"No se pudo acceder a la ISO seleccionada", "install_iso_access"},
            {"No se pudo abrir la ISO", "install_iso_open_failed"},
            {"No se pudo leer la ISO", "install_iso_read"},
            {"La ISO está incompleta o no es válida", "install_iso_invalid"},
            {"La ISO no contiene un juego Jak reconocido", "install_not_jak"},
            {"Esta edición de la ISO no está soportada", "install_edition_unsupported"},
            {"La verificación de la ISO falló", "install_verify_failed"},
            {"No se pudieron preparar los recursos del juego", "install_assets_failed"},
            {"La compilación del juego falló", "install_compile_failed"},
            {"Faltan archivos compilados del juego", "install_missing_compiled"},
            {"Faltan los escenarios del juego", "install_missing_levels"}};

    private static String nativeText(Context context, String text) {
        for (String[] known : NATIVE_TEXTS) {
            if (!text.startsWith(known[0])) continue;
            int id = context.getResources().getIdentifier(known[1], "string", context.getPackageName());
            if (id != 0) return context.getString(id);
        }
        return text;
    }

    /** The game runs in its own process; its files must not change underneath it. */
    private void requireGameClosed(int closeMessage) throws IOException {
        java.util.List<ActivityManager.RunningAppProcessInfo> processes =
                getSystemService(ActivityManager.class).getRunningAppProcesses();
        if (processes == null) throw new IOException(getString(R.string.install_cannot_check_game));
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if ((getPackageName() + ":game").equals(process.processName))
                throw new IOException(getString(closeMessage));
        }
    }

    private File prepareTools() throws Exception {
        String hash;
        try (InputStream input = getAssets().open("toolchain.sha256");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            InstallFiles.copy(input, output);
            hash = output.toString("UTF-8").trim();
        }
        if (!hash.matches("[a-f0-9]{64}")) throw new IOException(getString(R.string.install_tools_invalid));
        File tools = new File(getFilesDir(), "tools");
        InstallFiles.mkdir(tools);
        File ready = new File(tools, hash);
        if (!new File(ready, ".complete").isFile()) {
            File temporary = new File(tools, "unpacking");
            InstallFiles.removeTree(tools, temporary);
            update("running", 0, getString(R.string.install_tools), -1);
            InstallFiles.unzip(getAssets().open("toolchain.zip"), temporary);
            // This marker is written only after all archive entries and CRCs were read.
            try (FileOutputStream out = new FileOutputStream(new File(temporary, ".complete"))) {
                out.write(hash.getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (ready.exists()) throw new IOException(getString(R.string.install_tools_incomplete));
            if (!temporary.renameTo(ready)) throw new IOException(getString(R.string.install_tools_save_failed));
        }
        return ready;
    }

    private long isoSize(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getLong(0);
        } catch (Exception ignored) { }
        return -1;
    }

    private void copyIso(Uri uri, File target) throws Exception {
        long expected = isoSize(uri);
        if (expected >= 0 && expected < 1_000_000_000L) throw new IOException(getString(R.string.install_iso_invalid));
        // Working ISO, extracted inputs, converted levels and final packages coexist.
        long required = expected > 0 ? expected * 3 + 3_000_000_000L : 20_000_000_000L;
        if (target.getParentFile().getUsableSpace() < required) {
            throw new IOException(getString(R.string.install_space, (int) ((required + 999_999_999L) / 1_000_000_000L)));
        }
        update("running", 0, getString(R.string.install_copying), expected > 0 ? 0 : -1);
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(target)) {
            if (input == null) throw new IOException(getString(R.string.install_iso_open_failed));
            byte[] buffer = new byte[1024 * 1024];
            long copied = 0, last = 0;
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                copied += count;
                long now = SystemClock.elapsedRealtime();
                if (now - last > 500) {
                    last = now;
                    update("running", 0, getString(R.string.install_copying_mb, (int) (copied / 1_000_000L)),
                            expected > 0 ? (int) Math.min(100, copied * 100 / expected) : -1);
                }
            }
            output.getFD().sync();
            if (expected >= 0 && copied != expected) throw new IOException(getString(R.string.install_iso_copy_incomplete));
        }
    }

    private void install(Uri uri) {
        try {
            requireGameClosed(R.string.install_close_game_install);
            // Saves and settings are never touched by an installation; keep a copy anyway.
            if (LauncherStore.installed(this, game) != null) backup(Backups.PREPARE);
            File work = new File(getFilesDir(), "install-work");
            InstallFiles.removeTree(getFilesDir(), work);
            InstallFiles.mkdir(work);
            try (FileOutputStream log = new FileOutputStream(LauncherStore.log(this))) {
                log.write("Precursor installer, powered by OpenGOAL (AI-assisted)\n".getBytes(StandardCharsets.UTF_8));
            }
            update("running", -1, getString(R.string.install_checking_iso, title(game)), -1);
            System.loadLibrary("opengoal_installer");
            try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r")) {
                if (descriptor == null) throw new IOException(getString(R.string.install_iso_open_failed));
                String detected = nativeProbe(descriptor.getFd(), game);
                try (PrintWriter log = new PrintWriter(new FileOutputStream(LauncherStore.log(this), true))) {
                    log.println(detected == null ? "El proveedor requiere una copia local para comprobar la ISO." :
                            "ISO comprobada: " + title(detected) + ". Coincide con el juego seleccionado.");
                }
            }
            File tools = prepareTools();
            File data = new File(work, "data");
            InstallFiles.mkdir(data);
            for (String name : new String[]{"goal_src", "decompiler", "game", "custom_assets"})
                Os.symlink(new File(tools, name).getAbsolutePath(), new File(data, name).getAbsolutePath());
            File iso = new File(work, "input.iso");
            copyIso(uri, iso);
            game = nativeInstall(data.getAbsolutePath(), iso.getAbsolutePath(),
                    LauncherStore.log(this).getAbsolutePath(), game);
            if (!InstallFiles.validGame(game)) throw new IOException(getString(R.string.install_unknown_game));
            update("running", 4, getString(R.string.install_finishing), -1);
            // Keep runtime outputs and metadata. Original ISO remains untouched in its provider.
            File extracted = new File(data, "iso_data/" + game);
            File metadata = new File(work, "buildinfo.json");
            java.nio.file.Files.copy(new File(extracted, "buildinfo.json").toPath(), metadata.toPath());
            InstallFiles.removeTree(data, new File(data, "iso_data"));
            InstallFiles.mkdir(extracted);
            java.nio.file.Files.move(metadata.toPath(), new File(extracted, "buildinfo.json").toPath());
            InstallFiles.removeTree(data, new File(data, "decompiler_out"));
            InstallFiles.removeTree(data, new File(data, "out/" + game + "/obj"));
            InstallFiles.removeTree(data, new File(data, "install-config"));
            update("running", 4, getString(R.string.install_saving), -1);
            LauncherStore.publishInstallation(this, game, data, tools.getName(), BuildConfig.GAME_DATA_VERSION);
            end("ready", getString(R.string.install_ready, title(game)));
        } catch (Throwable error) {
            android.util.Log.e("OpenGOALInstaller", "Installation failed", error);
            try (PrintWriter log = new PrintWriter(new FileOutputStream(LauncherStore.log(this), true))) {
                error.printStackTrace(log);
            } catch (IOException ignored) { }
            end("error", message(this, error));
        } finally {
            try { getContentResolver().releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (Exception ignored) { }
        }
    }

    private void backup(String reason) {
        try { Backups.snapshot(this, reason); }
        catch (IOException error) { android.util.Log.w("OpenGOALInstaller", "Backup failed", error); }
    }

    static String title(String game) {
        return "jak1".equals(game) ? "Jak and Daxter" : "jak2".equals(game) ? "Jak II" : "Jak 3";
    }

    private void removeInstallation() {
        try {
            if (!InstallFiles.validGame(game)) throw new IOException(getString(R.string.install_invalid_game));
            requireGameClosed(R.string.install_close_game_remove);
            backup(Backups.REMOVE);
            update("running", -1, getString(R.string.install_removing, title(game)), -1);
            LauncherStore.removeInstallation(this, game);
            end("removed", getString(R.string.install_removed, title(game)));
        } catch (Exception error) { end("error", message(this, error)); }
    }

    private synchronized void end(String state, String text) {
        if (ending) return;
        try { update(state, -1, text, -1); }
        catch (Exception error) { android.util.Log.e("OpenGOALInstaller", "Cannot save state", error); }
        ending = true;
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
        // Do not reuse process-global compiler/decompiler state for the next game.
        // Cancellation also stops native workers before a subsequent job can take the lock.
        new Handler(Looper.getMainLooper()).postDelayed(() -> android.os.Process.killProcess(
                android.os.Process.myPid()), 300);
    }

    @Override public void onTimeout(int startId, int foregroundServiceType) {
        end("error", getString(R.string.install_timeout));
    }
}
