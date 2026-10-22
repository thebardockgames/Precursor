package com.opengoalandroid.jak1;

import android.content.Context;
import android.util.AtomicFile;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.channels.*;
import java.util.HashSet;
import java.util.Set;

/** Atomic manifests survive activity recreation and failed installations. (AI-assisted) */
final class LauncherStore {
    static JSONObject read(File file) {
        try (InputStream input = new AtomicFile(file).openRead();
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            InstallFiles.copy(input, output);
            return new JSONObject(output.toString("UTF-8"));
        } catch (Exception error) {
            return new JSONObject();
        }
    }

    static void write(File file, JSONObject value) throws IOException {
        InstallFiles.mkdir(file.getParentFile());
        AtomicFile atomic = new AtomicFile(file);
        FileOutputStream output = null;
        try {
            output = atomic.startWrite();
            output.write(value.toString().getBytes(StandardCharsets.UTF_8));
            atomic.finishWrite(output);
        } catch (IOException error) {
            if (output != null) atomic.failWrite(output);
            throw error;
        }
    }

    static File job(Context context) { return new File(context.getFilesDir(), "install-state.json"); }
    static File log(Context context) { return new File(context.getFilesDir(), "install.log"); }

    /**
     * Written by the game process when it starts and deleted when it closes normally. If it is
     * still there while no game process runs, the last session ended abnormally. (AI-assisted)
     */
    static File session(Context context) { return new File(context.getFilesDir(), "session.json"); }

    /** Where the native crash handler writes its dump for a game (see game/main.cpp). */
    static File crashDump(Context context, String game) {
        return new File(context.getFilesDir(), "config/" + game + "/crash.txt");
    }

    /** Evidence kept from the last abnormal session until the user sends or dismisses a report. */
    static File pendingReportDir(Context context) { return new File(context.getFilesDir(), "reports/pending"); }

    /**
     * A cancelled or failed installation leaves up to a few GB in install-work until the next attempt.
     * Move it aside while briefly holding the install lock (so a starting installation can never lose
     * its directory) and delete it afterwards. (AI-assisted)
     */
    static void discardStaleWork(Context context) {
        File[] leftovers = context.getFilesDir().listFiles((dir, name) -> name.startsWith("install-trash-"));
        if (leftovers != null) {
            for (File old : leftovers) {
                try { InstallFiles.removeTree(context.getFilesDir(), old); } catch (IOException ignored) { }
            }
        }
        File work = new File(context.getFilesDir(), "install-work");
        if (!work.exists()) return;
        File trash = new File(context.getFilesDir(), "install-trash-" + System.nanoTime());
        try (RandomAccessFile file = new RandomAccessFile(new File(context.getFilesDir(), "install.lock"), "rw");
             FileChannel channel = file.getChannel();
             FileLock lock = channel.tryLock()) {
            if (lock == null || !work.renameTo(trash)) return;
        } catch (IOException | OverlappingFileLockException busy) {
            return;
        }
        try { InstallFiles.removeTree(context.getFilesDir(), trash); } catch (IOException ignored) { }
    }

    static boolean installing(Context context) {
        try (RandomAccessFile file = new RandomAccessFile(new File(context.getFilesDir(), "install.lock"), "rw");
             FileChannel channel = file.getChannel()) {
            try (FileLock lock = channel.tryLock()) { return lock == null; }
        } catch (OverlappingFileLockException error) {
            return true;
        } catch (IOException error) {
            return true; // Do not start another writer if the lock cannot be checked.
        }
    }

    /**
     * Makes a prepared installation the active one: rename it into the library, commit the manifest,
     * forget mods applied to the old files and reclaim the previous installation. Returns the new
     * directory. The manifest write is the commit point; a failure before it leaves the previous
     * installation playable. (AI-assisted)
     */
    static File publishInstallation(Context context, String game, File prepared, String tools, int dataVersion) throws Exception {
        if (!InstallFiles.validGame(game)) throw new IOException("Juego no válido");
        File library = new File(context.getFilesDir(), "library");
        InstallFiles.mkdir(library);
        String directory = game + "-" + java.util.UUID.randomUUID();
        File installed = new File(library, directory);
        if (!prepared.renameTo(installed)) throw new IOException("No se pudo guardar la instalación");
        File manifestFile = new File(library, game + ".json");
        String previous = read(manifestFile).optString("directory");
        write(manifestFile, new JSONObject()
                .put("game", game).put("arch", "arm64").put("directory", directory)
                .put("tools", tools).put("dataVersion", dataVersion).put("installed", System.currentTimeMillis()));
        ModManager.resetForGame(context, game);
        if (!previous.isEmpty() && !previous.equals(directory)) {
            try { InstallFiles.removeGameInstallation(library, game, previous); }
            catch (IOException error) { /* Keep the space rather than fail a finished install. */ }
        }
        return installed;
    }

    /** Deletes the active installation of a game; saves and the original ISO are untouched. */
    static void removeInstallation(Context context, String game) throws Exception {
        File library = new File(context.getFilesDir(), "library");
        File manifestFile = new File(library, game + ".json");
        JSONObject manifest = read(manifestFile);
        if (!InstallFiles.validGame(game) || !game.equals(manifest.optString("game")))
            throw new IOException("No se encontró la instalación");
        InstallFiles.removeGameInstallation(library, game, manifest.getString("directory"));
        new AtomicFile(manifestFile).delete();
        ModManager.resetForGame(context, game);
    }

    static final int CURRENT = 0, OUTDATED = 1, INCOMPATIBLE = 2;

    /**
     * Whether a game's installation matches this app version. INCOMPATIBLE: it was compiled for an
     * older GAME_DATA_VERSION (bumped in build.gradle when the compiler or runtime changes how the
     * game must be built) and may crash. OUTDATED: the bundled game code changed (different
     * toolchain hash); it still runs, but without this version's fixes. Installations made before
     * the data version existed count as version 1. (AI-assisted)
     */
    static int staleness(Context context, String game, String tools, int dataVersion) {
        JSONObject manifest = read(new File(new File(context.getFilesDir(), "library"), game + ".json"));
        if (manifest.optInt("dataVersion", 1) != dataVersion) return INCOMPATIBLE;
        return tools != null && !tools.isEmpty() && !tools.equals(manifest.optString("tools")) ? OUTDATED : CURRENT;
    }

    /**
     * Each app version unpacks its own tools (a few hundred MB). Once no installation uses an older
     * set, it is deleted. Runs under the install lock, so a preparation in progress keeps its own.
     * (AI-assisted)
     */
    static void discardUnusedTools(Context context, String currentTools) {
        File tools = new File(context.getFilesDir(), "tools");
        File[] sets = tools.listFiles(File::isDirectory);
        if (sets == null) return;
        Set<String> used = new HashSet<>();
        used.add(currentTools);
        for (String game : new String[]{"jak1", "jak2", "jak3"})
            used.add(read(new File(new File(context.getFilesDir(), "library"), game + ".json")).optString("tools"));
        try (RandomAccessFile file = new RandomAccessFile(new File(context.getFilesDir(), "install.lock"), "rw");
             FileChannel channel = file.getChannel();
             FileLock lock = channel.tryLock()) {
            if (lock == null) return;
            for (File set : sets) {
                if (!used.contains(set.getName())) {
                    try { InstallFiles.removeTree(tools, set); } catch (IOException ignored) { }
                }
            }
        } catch (IOException | OverlappingFileLockException busy) {
            // Try again on a later start.
        }
    }

    static File installed(Context context, String game) {
        if (!InstallFiles.validGame(game)) return null;
        File library = new File(context.getFilesDir(), "library");
        JSONObject manifest = read(new File(library, game + ".json"));
        if (!"arm64".equals(manifest.optString("arch")) || !game.equals(manifest.optString("game"))) return null;
        try {
            File data = InstallFiles.child(library, manifest.getString("directory"));
            for (String file : new String[]{"KERNEL.CGO", "GAME.CGO"}) {
                File cgo = new File(data, "out/" + game + "/iso/" + file);
                if (!cgo.isFile() || cgo.length() == 0) return null;
            }
            if (!new File(data, "iso_data/" + game + "/buildinfo.json").isFile()
                    || !new File(data, "game/graphics/opengl_renderer/shaders").isDirectory()) return null;
            return data;
        } catch (Exception error) { return null; }
    }
}
