package com.opengoalandroid.jak1;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.util.*;

/**
 * Toggleable mods for an installed game: a mod package replaces (or adds) a small set of
 * already-compiled files (typically just GAME.CGO) inside the active installation.
 * Enabling backs up whatever was live the first time a path is touched; disabling
 * restores that backup. Mods never rebuild anything - the ARM64 compile happens once,
 * on a PC, via mod_recipe.py + goalc; only the resulting files travel to the phone.
 *
 * The backups and on/off state belong to one installation directory. If the game is
 * reinstalled or removed, the old state is discarded instead of being applied to the new
 * files. (AI-assisted)
 */
final class ModManager {
    private ModManager() {}

    static final class Mod {
        final String id, game, name, description;
        final List<String> paths; // install-relative, e.g. "out/jak1/iso/GAME.CGO"
        boolean enabled;

        Mod(String id, String game, String name, String description, List<String> paths, boolean enabled) {
            this.id = id; this.game = game; this.name = name; this.description = description;
            this.paths = paths; this.enabled = enabled;
        }
    }

    /** Marks a backup of a path that did not exist before the mod added it. */
    private static final String ABSENT = ".absent";
    private static final String INSTALLATION_KEY = "installation";
    private static final String ENABLED_KEY = "enabled";

    private static File modsRoot(Context context, String game) {
        return new File(context.getFilesDir(), "mods/" + game);
    }

    private static File packagesDir(Context context, String game) {
        return new File(modsRoot(context, game), "packages");
    }

    private static File originalsDir(Context context, String game) {
        return new File(modsRoot(context, game), "originals");
    }

    private static File stateFile(Context context, String game) {
        return new File(modsRoot(context, game), "state.json");
    }

    private static String flatten(String installRelativePath) {
        return installRelativePath.replace('/', '_');
    }

    /** Root of the game's currently active installation (files/library/<directory>). */
    static File activeInstallDir(Context context, String game) {
        return InstallFiles.validGame(game) ? LauncherStore.installed(context, game) : null;
    }

    /** Name of the active installation directory, or "" if the game is not installed. */
    private static String activeInstallation(Context context, String game) {
        File dir = activeInstallDir(context, game);
        return dir == null ? "" : dir.getName();
    }

    /**
     * State is {"installation": "<dir>", "enabled": {"<id>": true}}. Anything recorded for another
     * installation (or the pre-versioned flat format) describes files that no longer exist, so it
     * is dropped together with its backups.
     */
    private static JSONObject readState(Context context, String game) throws IOException {
        JSONObject state = LauncherStore.read(stateFile(context, game));
        String current = activeInstallation(context, game);
        if (!current.equals(state.optString(INSTALLATION_KEY)) || state.optJSONObject(ENABLED_KEY) == null) {
            InstallFiles.removeTree(modsRoot(context, game), originalsDir(context, game));
            state = new JSONObject();
            try {
                state.put(INSTALLATION_KEY, current).put(ENABLED_KEY, new JSONObject());
            } catch (org.json.JSONException impossible) { throw new IOException(impossible); }
            if (!current.isEmpty()) LauncherStore.write(stateFile(context, game), state);
        }
        return state;
    }

    private static JSONObject enabledMap(JSONObject state) { return state.optJSONObject(ENABLED_KEY); }

    private static void writeState(Context context, String game, JSONObject state) throws IOException {
        LauncherStore.write(stateFile(context, game), state);
    }

    /** Called when a game is installed again or removed: forget mods applied to the old files. */
    static void resetForGame(Context context, String game) throws IOException {
        InstallFiles.removeTree(modsRoot(context, game), originalsDir(context, game));
        new android.util.AtomicFile(stateFile(context, game)).delete();
    }

    /** Extracts a mod package zip (mod.json + payload files) so it can be listed/toggled. */
    static Mod importPackage(Context context, String game, InputStream zipStream) throws IOException, org.json.JSONException {
        File tempDir = new File(context.getCacheDir(), "mod-import-" + System.nanoTime());
        try {
            InstallFiles.unzip(zipStream, tempDir);
            File manifestFile = new File(tempDir, "mod.json");
            if (!manifestFile.isFile()) throw new IOException("El paquete no trae mod.json");
            JSONObject manifest = LauncherStore.read(manifestFile);
            String modGame = manifest.optString("game");
            if (!modGame.equals(game))
                throw new IOException("Este mod es para " + InstallService.title(modGame) + ", no para " + InstallService.title(game));
            String id = manifest.getString("id");
            if (!id.matches("[a-z0-9][a-z0-9-]{1,63}"))
                throw new IOException("Id de mod no válido");
            Mod mod = readMod(tempDir, false);
            if (mod.paths.isEmpty()) throw new IOException("El mod no declara archivos");

            JSONObject state = readState(context, game);
            if (enabledMap(state).optBoolean(id, false))
                throw new IOException("Desactiva \"" + mod.name + "\" antes de importar una versión nueva");

            File dest = new File(packagesDir(context, game), id);
            InstallFiles.mkdir(dest.getParentFile());
            if (dest.exists()) InstallFiles.removeTree(packagesDir(context, game), dest);
            if (!tempDir.renameTo(dest)) InstallFiles.copyTree(tempDir, dest);

            enabledMap(state).put(id, false);
            writeState(context, game, state);
            return readMod(dest, false);
        } finally {
            InstallFiles.removeTree(context.getCacheDir(), tempDir);
        }
    }

    /** Reads mod.json and checks that every entry stays inside the package and has its payload. */
    private static Mod readMod(File packageDir, boolean enabled) throws IOException, org.json.JSONException {
        JSONObject manifest = LauncherStore.read(new File(packageDir, "mod.json"));
        List<String> paths = new ArrayList<>();
        JSONArray files = manifest.optJSONArray("files");
        if (files != null) {
            for (int i = 0; i < files.length(); i++) {
                JSONObject entry = files.getJSONObject(i);
                String path = entry.getString("path");
                if (paths.contains(path)) throw new IOException("El mod repite " + path);
                if (!InstallFiles.child(packageDir, entry.getString("entry")).isFile())
                    throw new IOException("Al paquete le falta " + entry.getString("entry"));
                paths.add(path);
            }
        }
        return new Mod(manifest.getString("id"), manifest.getString("game"), manifest.getString("name"),
                manifest.optString("description"), paths, enabled);
    }

    static List<Mod> listMods(Context context, String game) {
        List<Mod> mods = new ArrayList<>();
        File[] dirs = packagesDir(context, game).listFiles();
        if (dirs == null) return mods;
        JSONObject enabled;
        try { enabled = enabledMap(readState(context, game)); }
        catch (IOException error) { enabled = new JSONObject(); }
        Arrays.sort(dirs, Comparator.comparing(File::getName));
        for (File dir : dirs) {
            if (!new File(dir, "mod.json").isFile()) continue;
            try {
                mods.add(readMod(dir, enabled.optBoolean(dir.getName(), false)));
            } catch (IOException | org.json.JSONException ignored) { /* skip malformed package */ }
        }
        return mods;
    }

    /**
     * Applies or reverts a mod's file swaps against the active installation. Either every file of
     * the mod is applied or none is: on any failure the files already touched are restored.
     */
    static void setEnabled(Context context, String game, String modId, boolean enabled) throws IOException, org.json.JSONException {
        File installDir = activeInstallDir(context, game);
        if (installDir == null) throw new IOException(InstallService.title(game) + " no está instalado");
        JSONObject state = readState(context, game);
        JSONObject enabledMods = enabledMap(state);
        if (enabledMods.optBoolean(modId, false) == enabled) return;
        File packageDir = InstallFiles.child(packagesDir(context, game), modId);
        Mod mod = readMod(packageDir, enabled);
        JSONArray files = LauncherStore.read(new File(packageDir, "mod.json")).getJSONArray("files");
        File originals = originalsDir(context, game);

        if (enabled) {
            // Two mods replacing the same file would undo each other when one is turned off.
            for (Mod other : listMods(context, game)) {
                if (!other.enabled || other.id.equals(modId)) continue;
                for (String path : mod.paths) {
                    if (other.paths.contains(path))
                        throw new IOException("\"" + mod.name + "\" y \"" + other.name + "\" modifican " + path
                                + ". Desactiva uno antes de activar el otro.");
                }
            }
        }

        List<File[]> applied = new ArrayList<>(); // {live, previous content copy}
        try {
            for (int i = 0; i < files.length(); i++) {
                JSONObject entry = files.getJSONObject(i);
                String relPath = entry.getString("path");
                File live = InstallFiles.child(installDir, relPath);
                File backup = new File(originals, flatten(relPath));
                File absent = new File(originals, flatten(relPath) + ABSENT);
                File undo = new File(originals, flatten(relPath) + ".undo");
                InstallFiles.mkdir(originals);
                if (live.isFile()) copyFile(live, undo); else undo.delete();
                applied.add(new File[]{live, undo});
                if (enabled) {
                    if (!backup.isFile() && !absent.isFile()) {
                        if (live.isFile()) copyFile(live, backup);
                        else new FileOutputStream(absent).close();
                    }
                    copyFile(InstallFiles.child(packageDir, entry.getString("entry")), live);
                } else if (backup.isFile()) {
                    copyFile(backup, live);
                    backup.delete();
                } else if (absent.isFile()) {
                    if (live.exists() && !live.delete()) throw new IOException("No se pudo quitar " + relPath);
                    absent.delete();
                }
            }
        } catch (IOException | RuntimeException error) {
            for (File[] change : applied) {
                try {
                    if (change[1].isFile()) copyFile(change[1], change[0]);
                    else change[0].delete();
                } catch (IOException ignored) { /* best effort; the original error is reported */ }
            }
            throw error;
        } finally {
            for (File[] change : applied) change[1].delete();
        }

        enabledMods.put(modId, enabled);
        writeState(context, game, state);
    }

    static void deletePackage(Context context, String game, String modId) throws IOException, org.json.JSONException {
        Mod current = null;
        for (Mod m : listMods(context, game)) if (m.id.equals(modId)) current = m;
        if (current != null && current.enabled) setEnabled(context, game, modId, false);
        InstallFiles.removeTree(packagesDir(context, game), InstallFiles.child(packagesDir(context, game), modId));
        JSONObject state = readState(context, game);
        enabledMap(state).remove(modId);
        writeState(context, game, state);
    }

    /** Writes next to the destination and renames, so a crash never leaves a half-written file. */
    private static void copyFile(File src, File dst) throws IOException {
        InstallFiles.mkdir(dst.getParentFile());
        File temp = new File(dst.getParentFile(), dst.getName() + ".tmp");
        try (InputStream in = new FileInputStream(src); FileOutputStream out = new FileOutputStream(temp)) {
            InstallFiles.copy(in, out);
            out.getFD().sync();
        }
        if (!temp.renameTo(dst)) {
            dst.delete();
            if (!temp.renameTo(dst)) { temp.delete(); throw new IOException("No se pudo escribir " + dst.getName()); }
        }
    }
}
