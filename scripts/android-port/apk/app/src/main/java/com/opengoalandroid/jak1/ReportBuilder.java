package com.opengoalandroid.jak1;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import android.os.StatFs;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.zip.*;

/**
 * Builds a bug-report zip the tester can share from the phone: device and build information,
 * the install log, the game's runtime logs, the native crash dump, the system's record of how
 * the game process ended and this app's recent logcat. Never includes saves, ISOs or game files.
 * (AI-assisted)
 */
final class ReportBuilder {
    private ReportBuilder() {}

    private static final long MAX_FILE = 4L * 1024 * 1024;

    static File reportsDir(Context context) { return new File(context.getCacheDir(), "reports"); }

    /**
     * Called when the launcher finds that the last game session ended abnormally: keeps the crash
     * dump and the exit record before the next launch overwrites them. Returns a short description,
     * or null if the session ended in a way that is not worth reporting (e.g. the user closed it).
     */
    static String captureAbnormalSession(Context context, JSONObject session) {
        String game = session.optString("game");
        int pid = session.optInt("pid", -1);
        String reason = null;
        StringBuilder exit = new StringBuilder();
        if (Build.VERSION.SDK_INT >= 30 && pid > 0) {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(context.getPackageName(), pid, 1)) {
                exit.append(info).append('\n');
                switch (info.getReason()) {
                    case ApplicationExitInfo.REASON_CRASH:
                    case ApplicationExitInfo.REASON_CRASH_NATIVE: reason = "se cerró por un error"; break;
                    case ApplicationExitInfo.REASON_ANR: reason = "dejó de responder"; break;
                    case ApplicationExitInfo.REASON_LOW_MEMORY: reason = "se cerró por falta de memoria"; break;
                    case ApplicationExitInfo.REASON_SIGNALED:
                        if (info.getStatus() != 9) reason = "se cerró por un error";
                        break;
                    case ApplicationExitInfo.REASON_EXIT_SELF:
                        if (info.getStatus() != 0) reason = "se cerró con un error";
                        break;
                    default: break;
                }
                if (info.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                    try (InputStream trace = info.getTraceInputStream()) {
                        if (trace != null) copyLimited(trace, new File(LauncherStore.pendingReportDir(context), "tombstone.pb"));
                    } catch (IOException ignored) { }
                }
            }
        }
        File dump = LauncherStore.crashDump(context, game);
        boolean hasDump = dump.isFile() && dump.length() > 0;
        if (hasDump && reason == null) reason = "se cerró por un error";
        if (reason == null) return null;
        File pending = LauncherStore.pendingReportDir(context);
        try {
            InstallFiles.mkdir(pending);
            write(new File(pending, "session.json"), session.toString(2));
            write(new File(pending, "exit-info.txt"), exit.length() == 0 ? "(no disponible en esta versión de Android)" : exit.toString());
            if (hasDump) copyLimited(new FileInputStream(dump), new File(pending, "crash-" + game + ".txt"));
        } catch (Exception ignored) { }
        return InstallService.title(game) + " " + reason;
    }

    static boolean hasPendingCrash(Context context) {
        String[] files = LauncherStore.pendingReportDir(context).list();
        return files != null && files.length > 0;
    }

    static void clearPending(Context context) {
        try { InstallFiles.removeTree(context.getFilesDir(), LauncherStore.pendingReportDir(context)); }
        catch (IOException ignored) { }
    }

    /** Writes the zip into the cache (shared through ReportProvider) and returns it. */
    static File build(Context context, String note) throws Exception {
        File dir = reportsDir(context);
        InstallFiles.mkdir(dir);
        File[] old = dir.listFiles();
        if (old != null) for (File f : old) f.delete();
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File zipFile = new File(dir, "precursor-reporte-" + stamp + ".zip");
        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(zipFile)))) {
            putText(zip, "reporte.txt", summary(context, note));
            File files = context.getFilesDir();
            putFile(zip, "install.log", LauncherStore.log(context));
            putFile(zip, "install-state.json", LauncherStore.job(context));
            File[] pending = LauncherStore.pendingReportDir(context).listFiles();
            if (pending != null) for (File f : pending) putFile(zip, "ultimo-cierre/" + f.getName(), f);
            for (String game : new String[]{"jak1", "jak2", "jak3"}) {
                putFile(zip, game + "/manifest.json", new File(files, "library/" + game + ".json"));
                putFile(zip, game + "/crash.txt", LauncherStore.crashDump(context, game));
                putFile(zip, game + "/mods.json", new File(files, "mods/" + game + "/state.json"));
                File installation = LauncherStore.installed(context, game);
                if (installation == null) continue;
                putFile(zip, game + "/buildinfo.json", new File(installation, "iso_data/" + game + "/buildinfo.json"));
                File[] logs = new File(installation, "log").listFiles((d, name) -> name.endsWith(".log"));
                if (logs != null) {
                    Arrays.sort(logs, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                    for (int i = 0; i < Math.min(3, logs.length); i++) putFile(zip, game + "/log/" + logs[i].getName(), logs[i]);
                }
            }
            putText(zip, "logcat.txt", logcat());
        }
        return zipFile;
    }

    private static String summary(Context context, String note) {
        StringBuilder out = new StringBuilder();
        out.append("Precursor (powered by OpenGOAL, proyecto no oficial) - reporte de problema\n");
        out.append("Fecha: ").append(new Date()).append("\n\n");
        if (note != null && !note.trim().isEmpty()) out.append("Qué pasó (según quien reporta):\n").append(note.trim()).append("\n\n");
        out.append("App: ").append(BuildConfig.APPLICATION_ID).append(' ').append(BuildConfig.VERSION_NAME)
                .append(" (").append(BuildConfig.VERSION_CODE).append(")\n");
        out.append("Commit: ").append(BuildConfig.GIT_COMMIT).append("   Compilado: ").append(BuildConfig.BUILD_TIME).append('\n');
        out.append("Dispositivo: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append(", ").append(Build.HARDWARE).append(")\n");
        if (Build.VERSION.SDK_INT >= 31) out.append("SoC: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        out.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        out.append("ABIs: ").append(String.join(", ", Build.SUPPORTED_ABIS)).append('\n');
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        out.append("OpenGL ES: ").append(manager.getDeviceConfigurationInfo().getGlEsVersion()).append('\n');
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(memory);
        out.append("RAM: ").append(memory.totalMem / (1024 * 1024)).append(" MB total, ")
                .append(memory.availMem / (1024 * 1024)).append(" MB libres\n");
        StatFs fs = new StatFs(context.getFilesDir().getPath());
        out.append("Almacenamiento libre: ").append(fs.getAvailableBytes() / (1024 * 1024)).append(" MB\n");
        for (String game : new String[]{"jak1", "jak2", "jak3"}) {
            out.append(InstallService.title(game)).append(": ")
                    .append(LauncherStore.installed(context, game) != null ? "instalado" : "no instalado").append('\n');
        }
        out.append("\nNo se incluyen partidas guardadas, ISOs ni archivos del juego.\n");
        return out.toString();
    }

    /** This app's own recent log lines (Android only lets an app read its own). */
    private static String logcat() {
        try {
            Process process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "6000")
                    .redirectErrorStream(true).start();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (InputStream in = process.getInputStream()) {
                byte[] buffer = new byte[64 * 1024];
                int n;
                while ((n = in.read(buffer)) != -1 && out.size() < MAX_FILE) out.write(buffer, 0, n);
            }
            process.waitFor(5, TimeUnit.SECONDS);
            process.destroy();
            return out.toString("UTF-8");
        } catch (Exception error) {
            return "logcat no disponible: " + error;
        }
    }

    private static void putText(ZipOutputStream zip, String name, String text) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(text.getBytes(StandardCharsets.UTF_8));
        zip.closeEntry();
    }

    /** Adds a file, keeping only its last MAX_FILE bytes (logs grow at the end). */
    private static void putFile(ZipOutputStream zip, String name, File file) throws IOException {
        if (!file.isFile()) return;
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long start = Math.max(0, in.length() - MAX_FILE);
            in.seek(start);
            zip.putNextEntry(new ZipEntry(name));
            byte[] buffer = new byte[256 * 1024];
            int n;
            while ((n = in.read(buffer)) != -1) zip.write(buffer, 0, n);
            zip.closeEntry();
        }
    }

    private static void write(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) { out.write(text.getBytes(StandardCharsets.UTF_8)); }
    }

    private static void copyLimited(InputStream input, File target) throws IOException {
        try (InputStream in = input; OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[256 * 1024];
            long total = 0;
            int n;
            while ((n = in.read(buffer)) != -1 && total < MAX_FILE) { out.write(buffer, 0, n); total += n; }
        }
    }
}
