package com.opengoalandroid.jak1;

import android.content.Context;
import java.io.*;
import java.text.SimpleDateFormat;
import java.util.*;

/**
 * Copies of the player's saves and game settings (files/config: each game's OpenGOAL saves and
 * pc-settings.gc), taken before anything that could put them at risk: an app update, preparing a
 * game again, removing one, or restoring an older copy. Installations never write there, so these
 * copies are a safety net, not the only protection. The newest KEEP copies are kept. Pure Java so
 * it is tested on the host. (AI-assisted)
 */
final class Backups {
    private Backups() {}

    static final int KEEP = 5;
    static final String UPDATE = "update", PREPARE = "prepare", REMOVE = "remove", RESTORE = "restore";

    /** One copy: its folder, when it was taken and why (one of the constants above). */
    static final class Backup {
        final File dir;
        final long time;
        final String reason;

        Backup(File dir, long time, String reason) { this.dir = dir; this.time = time; this.reason = reason; }
    }

    static File root(Context context) { return new File(context.getFilesDir(), "backups"); }

    private static File config(Context context) { return new File(context.getFilesDir(), "config"); }

    /** Crash dumps and logs are not the player's data and can be large. */
    private static boolean skip(File file) {
        String name = file.getName();
        return name.equals("crash.txt") || name.endsWith(".log") || name.endsWith(".tmp") || file.length() > 32L * 1024 * 1024;
    }

    /** Takes a copy now. Returns null when there is nothing to back up yet. */
    static File snapshot(Context context, String reason) throws IOException {
        File source = config(context);
        if (!hasFiles(source)) return null;
        File root = root(context);
        InstallFiles.mkdir(root);
        long now = System.currentTimeMillis();
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT);
        String stamp = format.format(new Date(now));
        // Two copies in the same millisecond still get distinct, correctly ordered names.
        while (new File(root, stamp + "_" + reason).exists()) stamp = format.format(new Date(++now));
        File temp = new File(root, ".partial-" + stamp);
        InstallFiles.removeTree(root, temp);
        copyTree(source, new File(temp, "config"));
        File done = new File(root, stamp + "_" + reason);
        // A copy appears in list() only once complete.
        if (!temp.renameTo(done)) throw new IOException("Cannot finish backup " + done);
        prune(context);
        return done;
    }

    /** Newest first. */
    static List<Backup> list(Context context) {
        List<Backup> result = new ArrayList<>();
        File[] dirs = root(context).listFiles(f -> f.isDirectory() && !f.getName().startsWith("."));
        if (dirs == null) return result;
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT);
        for (File dir : dirs) {
            String name = dir.getName();
            int split = name.indexOf('_');
            if (split < 0) continue;
            try {
                result.add(new Backup(dir, format.parse(name.substring(0, split)).getTime(), name.substring(split + 1)));
            } catch (java.text.ParseException ignored) { }
        }
        result.sort((a, b) -> Long.compare(b.time, a.time));
        return result;
    }

    /**
     * Puts a copy's files back. What exists now is backed up first, and files the copy does not
     * have (a newer slot, for example) are left in place. The game must be closed.
     */
    static void restore(Context context, Backup backup) throws IOException {
        File from = new File(backup.dir, "config");
        if (!from.isDirectory()) throw new IOException("Backup is empty: " + backup.dir);
        snapshot(context, RESTORE);
        copyTree(from, config(context));
    }

    private static void prune(Context context) {
        List<Backup> all = list(context);
        for (int i = KEEP; i < all.size(); i++) {
            try { InstallFiles.removeTree(root(context), all.get(i).dir); } catch (IOException ignored) { }
        }
        File[] partial = root(context).listFiles(f -> f.getName().startsWith(".partial-"));
        if (partial != null) {
            for (File old : partial) {
                // Another snapshot may be writing one right now; only drop old leftovers.
                if (System.currentTimeMillis() - old.lastModified() > 60 * 60 * 1000L) {
                    try { InstallFiles.removeTree(root(context), old); } catch (IOException ignored) { }
                }
            }
        }
    }

    private static boolean hasFiles(File dir) {
        File[] children = dir.listFiles();
        if (children == null) return false;
        for (File child : children) {
            if (child.isDirectory() ? hasFiles(child) : !skip(child)) return true;
        }
        return false;
    }

    /** Copies regular files (no symlinks), replacing each target through a temporary file. */
    private static void copyTree(File from, File to) throws IOException {
        File[] children = from.listFiles();
        if (children == null) return;
        InstallFiles.mkdir(to);
        for (File child : children) {
            if (java.nio.file.Files.isSymbolicLink(child.toPath())) continue;
            File target = new File(to, child.getName());
            if (child.isDirectory()) { copyTree(child, target); continue; }
            if (skip(child)) continue;
            File temp = new File(to, child.getName() + ".copying");
            try (InputStream input = new FileInputStream(child); FileOutputStream output = new FileOutputStream(temp)) {
                InstallFiles.copy(input, output);
                output.getFD().sync();
            }
            java.nio.file.Files.move(temp.toPath(), target.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                    java.nio.file.StandardCopyOption.ATOMIC_MOVE);
            target.setLastModified(child.lastModified());
        }
    }
}
