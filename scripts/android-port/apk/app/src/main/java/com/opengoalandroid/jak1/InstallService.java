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

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && CANCEL.equals(intent.getAction())) {
            if (removing) return START_NOT_STICKY;
            end("cancelled", "Instalación cancelada. Puedes volver a seleccionar la ISO.");
            return START_NOT_STICKY;
        }
        if (started) return START_NOT_STICKY;
        started = true;
        removing = intent != null && REMOVE.equals(intent.getAction());
        try {
            NotificationManager manager = getSystemService(NotificationManager.class);
            manager.createNotificationChannel(new NotificationChannel(CHANNEL,
                    "Preparación de juegos", NotificationManager.IMPORTANCE_LOW));
            startForeground(NOTIFICATION, notification("Iniciando instalación…"));
            if (!BuildConfig.OPENGOAL_LAUNCHER || intent == null || (!removing && intent.getData() == null))
                throw new IOException("No se recibió una ISO");
            lockFile = new RandomAccessFile(new File(getFilesDir(), "install.lock"), "rw");
            lock = lockFile.getChannel().tryLock();
            if (lock == null) throw new IOException("Ya hay una instalación en curso");
            if (removing) {
                game = intent.getStringExtra("game");
                new Thread(this::removeInstallation, "Remove installation").start();
                return START_NOT_STICKY;
            }
            game = intent.getStringExtra("game");
            if (!InstallFiles.validGame(game)) throw new IOException("Selecciona el juego que quieres instalar");
            wakeLock = getSystemService(PowerManager.class).newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK, getPackageName() + ":install");
            wakeLock.acquire(6 * 60 * 60 * 1000L);
            update("running", "Preparando la instalación…", -1);
            Uri uri = intent.getData();
            // The compiler needs considerably more stack than a Java thread's default.
            new Thread(null, () -> install(uri), "OpenGOAL installer", 16 * 1024 * 1024L).start();
        } catch (Exception error) {
            end("error", message(error));
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
                .setContentTitle(removing ? "Precursor · Borrando instalación" : "Precursor · Preparando juego").setContentText(text)
                .setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true);
        if (!removing) builder.addAction(new Notification.Action.Builder(null, "Cancelar", cancel).build());
        return builder.build();
    }

    private synchronized void update(String state, String text, int percent) throws Exception {
        if (ending) return;
        LauncherStore.write(LauncherStore.job(this), new JSONObject()
                .put("state", state).put("message", text).put("game", game)
                .put("operation", removing ? "remove" : "install")
                .put("percent", percent).put("updated", System.currentTimeMillis()));
        getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(text));
    }

    // Called synchronously on the native worker, never from the SDL game process.
    public void onNativeProgress(String detectedGame, String text) throws Exception {
        game = detectedGame;
        update("running", text, -1);
    }

    private static String message(Throwable error) {
        String text = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
        // The ISO reader reports in English; the launcher speaks Spanish. (AI-assisted)
        if (text.contains("ISO 9660") || text.contains("primary volume"))
            return "El archivo elegido no es una imagen ISO de PS2.";
        if (text.contains("Truncated") || text.contains("truncated"))
            return "La ISO está incompleta o dañada. Vuelve a copiarla desde el disco.";
        return text;
    }

    /** The game runs in its own process; its files must not change underneath it. */
    private void requireGameClosed(String action) throws IOException {
        java.util.List<ActivityManager.RunningAppProcessInfo> processes =
                getSystemService(ActivityManager.class).getRunningAppProcesses();
        if (processes == null) throw new IOException("No se pudo comprobar si el juego está cerrado. Inténtalo de nuevo.");
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if ((getPackageName() + ":game").equals(process.processName))
                throw new IOException("Cierra el juego antes de " + action + ".");
        }
    }

    private File prepareTools() throws Exception {
        String hash;
        try (InputStream input = getAssets().open("toolchain.sha256");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            InstallFiles.copy(input, output);
            hash = output.toString("UTF-8").trim();
        }
        if (!hash.matches("[a-f0-9]{64}")) throw new IOException("Paquete de herramientas no válido");
        File tools = new File(getFilesDir(), "tools");
        InstallFiles.mkdir(tools);
        File ready = new File(tools, hash);
        if (!new File(ready, ".complete").isFile()) {
            File temporary = new File(tools, "unpacking");
            InstallFiles.removeTree(tools, temporary);
            update("running", "Preparando las herramientas incluidas…", -1);
            InstallFiles.unzip(getAssets().open("toolchain.zip"), temporary);
            // This marker is written only after all archive entries and CRCs were read.
            try (FileOutputStream out = new FileOutputStream(new File(temporary, ".complete"))) {
                out.write(hash.getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (ready.exists()) throw new IOException("La carpeta de herramientas está incompleta");
            if (!temporary.renameTo(ready)) throw new IOException("No se pudieron guardar las herramientas");
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
        if (expected >= 0 && expected < 1_000_000_000L) throw new IOException("La ISO está incompleta o no es válida");
        // Working ISO, extracted inputs, converted levels and final packages coexist.
        long required = expected > 0 ? expected * 3 + 3_000_000_000L : 20_000_000_000L;
        if (target.getParentFile().getUsableSpace() < required) {
            throw new IOException("Se necesitan aproximadamente " + ((required + 999_999_999L) / 1_000_000_000L)
                    + " GB libres para preparar esta ISO");
        }
        update("running", "Copiando la ISO…", expected > 0 ? 0 : -1);
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(target)) {
            if (input == null) throw new IOException("No se pudo abrir la ISO seleccionada");
            byte[] buffer = new byte[1024 * 1024];
            long copied = 0, last = 0;
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                copied += count;
                long now = SystemClock.elapsedRealtime();
                if (now - last > 500) {
                    last = now;
                    update("running", "Copiando la ISO · " + copied / 1_000_000L + " MB",
                            expected > 0 ? (int) Math.min(100, copied * 100 / expected) : -1);
                }
            }
            output.getFD().sync();
            if (expected >= 0 && copied != expected) throw new IOException("La copia de la ISO quedó incompleta");
        }
    }

    private void install(Uri uri) {
        try {
            requireGameClosed("instalar");
            File work = new File(getFilesDir(), "install-work");
            InstallFiles.removeTree(getFilesDir(), work);
            InstallFiles.mkdir(work);
            try (FileOutputStream log = new FileOutputStream(LauncherStore.log(this))) {
                log.write("Precursor installer, powered by OpenGOAL (AI-assisted)\n".getBytes(StandardCharsets.UTF_8));
            }
            update("running", "Comprobando que la ISO sea de " + title(game) + "…", -1);
            System.loadLibrary("opengoal_installer");
            try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r")) {
                if (descriptor == null) throw new IOException("No se pudo abrir la ISO");
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
            if (!InstallFiles.validGame(game)) throw new IOException("El instalador no identificó el juego");
            update("running", "Finalizando instalación…", -1);
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
            update("running", "Guardando la instalación y liberando la anterior…", -1);
            LauncherStore.publishInstallation(this, game, data, tools.getName());
            end("ready", title(game) + " está listo para jugar.");
        } catch (Throwable error) {
            android.util.Log.e("OpenGOALInstaller", "Installation failed", error);
            try (PrintWriter log = new PrintWriter(new FileOutputStream(LauncherStore.log(this), true))) {
                error.printStackTrace(log);
            } catch (IOException ignored) { }
            end("error", message(error));
        } finally {
            try { getContentResolver().releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (Exception ignored) { }
        }
    }

    static String title(String game) {
        return "jak1".equals(game) ? "Jak and Daxter" : "jak2".equals(game) ? "Jak II" : "Jak 3";
    }

    private void removeInstallation() {
        try {
            if (!InstallFiles.validGame(game)) throw new IOException("Juego no válido");
            requireGameClosed("borrar una instalación");
            update("running", "Borrando la instalación de " + title(game) + "…", -1);
            LauncherStore.removeInstallation(this, game);
            end("removed", title(game) + " eliminado. Se conservaron las partidas y la ISO original.");
        } catch (Exception error) { end("error", message(error)); }
    }

    private synchronized void end(String state, String text) {
        if (ending) return;
        try { update(state, text, -1); }
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
        end("error", "Android detuvo la instalación por tiempo límite. Vuelve a seleccionar la ISO.");
    }
}
