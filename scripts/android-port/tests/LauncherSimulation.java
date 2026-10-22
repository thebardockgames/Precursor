package com.opengoalandroid.jak1;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/**
 * Runs the launcher's real non-UI classes (LauncherStore, InstallFiles, GameSaves, ModManager)
 * on the host JVM against simulated app storage. Each scenario states the correct behaviour and
 * prints OK or BUG. (AI-assisted)
 *
 *   javac -d out stubs/android/content/Context.java stubs/android/util/AtomicFile.java \
 *     stubs/org/json/*.java LauncherSimulation.java ../apk/app/src/main/java/com/opengoalandroid/jak1/\
 *     {InstallFiles,LauncherStore,GameSaves,ModManager,InstallProgress,Texts,Versions,Backups}.java
 *   java -cp out com.opengoalandroid.jak1.LauncherSimulation <scratch dir>
 */
public final class LauncherSimulation {
    static int ok, bugs;
    static final List<String> bugList = new ArrayList<>();

    static void check(String scenario, boolean correct, String detail) {
        if (correct) { ok++; System.out.println("OK   " + scenario); }
        else { bugs++; bugList.add(scenario); System.out.println("BUG  " + scenario + "\n       " + detail); }
    }

    static Context freshApp(File base, String name) throws IOException {
        return new Context(Files.createTempDirectory(base.toPath(), name + "-").toFile());
    }

    static String read(File f) throws IOException { return new String(Files.readAllBytes(f.toPath()), "UTF-8"); }
    static void write(File f, String s) throws IOException { f.getParentFile().mkdirs(); Files.write(f.toPath(), s.getBytes("UTF-8")); }

    /** Prepares an installation like InstallService.install() and publishes it the same way. */
    static File install(Context ctx, String game, String tag) throws Exception {
        File data = Files.createTempDirectory(ctx.getCacheDir().toPath(), "prepared-").toFile();
        write(new File(data, "out/" + game + "/iso/KERNEL.CGO"), "kernel-" + tag);
        write(new File(data, "out/" + game + "/iso/GAME.CGO"), "game-" + tag);
        write(new File(data, "iso_data/" + game + "/buildinfo.json"), "{}");
        new File(data, "game/graphics/opengl_renderer/shaders").mkdirs();
        return LauncherStore.publishInstallation(ctx, game, data, "tools", 1);
    }

    /** files: {path, entry, content} triples. */
    static InputStream modZip(String id, String game, String[]... files) throws Exception {
        JSONArray list = new JSONArray();
        for (String[] f : files) list.put(new JSONObject().put("path", f[0]).put("entry", f[1]));
        JSONObject manifest = new JSONObject().put("id", id).put("game", game).put("name", "Mod " + id).put("files", list);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry("mod.json"));
            zip.write(manifest.toString().getBytes("UTF-8"));
            zip.closeEntry();
            for (String[] f : files) {
                if (f[2] == null) continue; // payload deliberately missing
                zip.putNextEntry(new ZipEntry(f[1]));
                zip.write(f[2].getBytes("UTF-8"));
                zip.closeEntry();
            }
        }
        return new ByteArrayInputStream(bytes.toByteArray());
    }

    static ModManager.Mod mod(Context ctx, String game, String id) {
        for (ModManager.Mod m : ModManager.listMods(ctx, game)) if (m.id.equals(id)) return m;
        return null;
    }

    /** A memory-card bank exactly as kmemcard.cpp writes it: header, game-save data, footer. */
    static void bank(File file, GameSaves.Profile p, long saveCount, int magic, int level, float[] stats, float completion)
            throws IOException {
        bank(file, p, saveCount, magic, level, stats, completion, true);
    }

    static void bank(File file, GameSaves.Profile p, long saveCount, int magic, int level, float[] stats, float completion,
                     boolean validChecksum) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(p.totalFileSize()).order(ByteOrder.LITTLE_ENDIAN);
        int data = GameSaves.HEADER_SIZE;
        b.putInt(data + 16, level);
        for (int i = 0; i < stats.length; i++) b.putFloat(data + 20 + 4 * i, stats[i]);
        b.putFloat(data + p.completionOffset, completion);
        // mc_checksum() from kmemcard.cpp
        int sum = 0;
        for (int i = 0; i < p.bankSize / 4; i++)
            sum = (sum << 1) ^ (sum >> 31) ^ b.getInt(data + 4 * i) ^ 0x12345678;
        sum ^= 0xedd1e666;
        if (!validChecksum) sum ^= 1;
        for (int header : new int[]{0, data + p.bankSize}) { // header and identical footer
            b.putInt(header, (int) saveCount); b.putInt(header + 4, sum); b.putInt(header + 8, magic);
            b.putInt(header + 0x3fc, (int) saveCount);
        }
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), b.array());
    }

    public static void main(String[] args) throws Exception {
        File base = Files.createDirectories(Paths.get(args[0])).toFile();

        // ---------------- installations ----------------
        {
            Context ctx = freshApp(base, "installed");
            File data = install(ctx, "jak1", "a");
            check("installed() recognises a complete installation", data.equals(LauncherStore.installed(ctx, "jak1")), "");
            new File(data, "out/jak1/iso/GAME.CGO").delete();
            check("installed() rejects an installation without GAME.CGO", LauncherStore.installed(ctx, "jak1") == null, "");
            check("installed() rejects an unknown game id", LauncherStore.installed(ctx, "../jak1") == null, "");
        }
        {
            Context ctx = freshApp(base, "reinstall");
            File first = install(ctx, "jak1", "first");
            File second = install(ctx, "jak1", "second");
            check("reinstalling reclaims the previous installation's space", !first.exists(),
                    "the old directory " + first.getName() + " is still on disk next to " + second.getName()
                            + " (InstallService.install() never deletes it; every reinstall leaks a whole game)");
        }
        {
            Context ctx = freshApp(base, "lock");
            File lockFile = new File(ctx.getFilesDir(), "install.lock");
            try (RandomAccessFile f = new RandomAccessFile(lockFile, "rw"); FileLock held = f.getChannel().lock()) {
                check("installing() is true while the install lock is held", LauncherStore.installing(ctx), "");
            }
            check("installing() is false once the lock is released", !LauncherStore.installing(ctx), "");
        }

        // ---------------- saves ----------------
        {
            Context ctx = freshApp(base, "saves");
            GameSaves.Profile p1 = GameSaves.PROFILES.get("jak1");
            File dir = GameSaves.savesDir(ctx, "jak1");
            check("jak1 saves dir matches kmemcard (config/jak1/OpenGOAL/jak1/saves/BASCUS-97124AYBABTU!)",
                    dir.getPath().replace('\\', '/').endsWith("files/config/jak1/OpenGOAL/jak1/saves/BASCUS-97124AYBABTU!"),
                    dir.getPath());
            bank(GameSaves.bankFile(dir, 0, 0), p1, 3, 0x12345678, 2, new float[]{20, 450, 31}, 18.5f);
            bank(GameSaves.bankFile(dir, 0, 1), p1, 4, 0x12345678, 5, new float[]{22, 470, 33}, 21f);
            bank(GameSaves.bankFile(dir, 1, 0), p1, 9, 0x0badf00d, 3, new float[]{1, 2, 3}, 1f);
            write(GameSaves.bankFile(dir, 2, 0), "truncated");
            List<GameSaves.Slot> slots = GameSaves.readSlots(ctx, "jak1");
            GameSaves.Slot s0 = slots.get(0);
            check("slot 1 shows the newer bank of the pair", s0.present && s0.saveCount == 4 && s0.levelIndex == 5
                    && s0.stats[0] == 22 && s0.stats[1] == 470 && s0.stats[2] == 33 && s0.completionPercent == 21f,
                    "count=" + s0.saveCount + " level=" + s0.levelIndex);
            check("bank with a bad magic is ignored", !slots.get(1).present, "");
            check("truncated bank is ignored", !slots.get(2).present, "");
            check("empty slot is reported empty", !slots.get(3).present, "");
            GameSaves.Profile p2 = GameSaves.PROFILES.get("jak2");
            File dir2 = GameSaves.savesDir(ctx, "jak2");
            bank(GameSaves.bankFile(dir2, 0, 0), p2, 1, 0x12345678, 7, new float[]{12, 3}, 44f);
            GameSaves.Slot j2 = GameSaves.readSlots(ctx, "jak2").get(0);
            check("jak2 reads gems/skill and completion at offset 28", j2.present && j2.stats[0] == 12 && j2.stats[1] == 3
                    && j2.completionPercent == 44f, "completion=" + j2.completionPercent);
            // A crash between the header and the data leaves a full-size file whose checksum fails.
            bank(GameSaves.bankFile(dir, 3, 0), p1, 10, 0x12345678, 4, new float[]{40, 900, 50}, 39f);
            File torn = GameSaves.bankFile(dir, 3, 1);
            bank(torn, p1, 50, 0x12345678, 0, new float[]{0, 0, 0}, 0f, false);
            GameSaves.Slot t = GameSaves.readSlots(ctx, "jak1").get(3);
            check("a bank whose checksum fails loses to the valid bank of the pair (as in the game)",
                    t.present && t.saveCount == 10 && t.levelIndex == 4,
                    "GameSaves never checks McHeader.checksum: the corrupt bank (save count " + t.saveCount
                            + ", level " + t.levelIndex + ") is shown instead of the valid one the game would load");
        }

        // ---------------- mods ----------------
        String cgo = "out/jak1/iso/GAME.CGO";
        {
            Context ctx = freshApp(base, "mod-roundtrip");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            boolean on = read(new File(data, cgo)).equals("alpha") && mod(ctx, "jak1", "alpha").enabled;
            ModManager.setEnabled(ctx, "jak1", "alpha", false);
            check("enable then disable a mod restores the original file",
                    on && read(new File(data, cgo)).equals("game-orig") && !mod(ctx, "jak1", "alpha").enabled, "");
        }
        {
            Context ctx = freshApp(base, "mod-conflict");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.importPackage(ctx, "jak1", modZip("beta", "jak1", new String[]{cgo, "GAME.CGO", "beta"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            try { ModManager.setEnabled(ctx, "jak1", "beta", true); } catch (IOException refused) { }
            ModManager.setEnabled(ctx, "jak1", "alpha", false);
            String live = read(new File(data, cgo));
            boolean betaOn = mod(ctx, "jak1", "beta").enabled;
            check("two mods on the same file: disabling one keeps the other's file (or the conflict is refused)",
                    !(betaOn && !live.equals("beta")),
                    "beta is still marked enabled but GAME.CGO now contains '" + live + "' (disabling alpha restored the original)");
        }
        {
            Context ctx = freshApp(base, "mod-reinstall");
            install(ctx, "jak1", "v1");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            File v2 = install(ctx, "jak1", "v2"); // reinstall: fresh files, new directory
            boolean shownOn = mod(ctx, "jak1", "alpha").enabled;
            String liveAfterReinstall = read(new File(v2, cgo));
            check("after reinstalling, the mod switch matches what is actually installed",
                    !(shownOn && !liveAfterReinstall.equals("alpha")),
                    "switch says ON but the new installation has the stock GAME.CGO ('" + liveAfterReinstall + "')");
            ModManager.setEnabled(ctx, "jak1", "alpha", false);
            String restored = read(new File(v2, cgo));
            check("disabling after a reinstall never writes files from the old installation",
                    restored.equals("game-v2"),
                    "GAME.CGO of the new installation became '" + restored + "', the backup taken from the OLD installation");
        }
        {
            Context ctx = freshApp(base, "mod-newfile");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("extra", "jak1",
                    new String[]{"out/jak1/iso/EXTRA.DGO", "EXTRA.DGO", "new level"}));
            String error = null;
            try { ModManager.setEnabled(ctx, "jak1", "extra", true); } catch (Exception e) { error = e.toString(); }
            check("a mod can add a file that the base game does not have",
                    error == null && new File(data, "out/jak1/iso/EXTRA.DGO").isFile(), "enabling fails: " + error);
        }
        {
            Context ctx = freshApp(base, "mod-traversal");
            File data = install(ctx, "jak1", "orig");
            String error = null;
            try {
                ModManager.importPackage(ctx, "jak1", modZip("evil", "jak1",
                        new String[]{cgo, "../../../../library/jak1.json", null}));
                ModManager.setEnabled(ctx, "jak1", "evil", true);
            } catch (Exception e) { error = e.toString(); }
            String live = read(new File(data, cgo));
            check("a mod's 'entry' cannot point outside its own package",
                    error != null && live.equals("game-orig"),
                    "GAME.CGO was overwritten with another app file: '" + live.substring(0, Math.min(40, live.length())) + "…'");
        }
        {
            Context ctx = freshApp(base, "mod-partial");
            File data = install(ctx, "jak1", "orig");
            String error = null;
            try {
                ModManager.importPackage(ctx, "jak1", modZip("half", "jak1",
                        new String[]{cgo, "GAME.CGO", "half"}, new String[]{"out/jak1/iso/KERNEL.CGO", "KERNEL.CGO", null}));
                ModManager.setEnabled(ctx, "jak1", "half", true);
            } catch (Exception e) { error = e.toString(); }
            String live = read(new File(data, cgo));
            check("a mod that fails half-way leaves the installation untouched",
                    error != null && live.equals("game-orig"),
                    "enable failed (" + error + ") but GAME.CGO is already '" + live + "' and the switch says OFF");
        }
        {
            Context ctx = freshApp(base, "mod-reimport");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            try { ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha v2"})); }
            catch (IOException refused) { }
            boolean shownOn = mod(ctx, "jak1", "alpha").enabled;
            String live = read(new File(data, cgo));
            check("re-importing an enabled mod keeps the switch consistent with the files",
                    shownOn == !live.equals("game-orig"),
                    "switch shows " + (shownOn ? "ON" : "OFF") + " but GAME.CGO is '" + live + "'");
        }
        {
            Context ctx = freshApp(base, "mod-badpackage");
            install(ctx, "jak1", "orig");
            try { ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak2", new String[]{cgo, "GAME.CGO", "x"})); }
            catch (Exception expected) { }
            String[] left = ctx.getCacheDir().list();
            check("a rejected mod package leaves nothing behind in the cache",
                    left == null || left.length == 0, "left in cache: " + Arrays.toString(left));
        }
        {
            Context ctx = freshApp(base, "mod-removal");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            LauncherStore.removeInstallation(ctx, "jak1"); // same call as InstallService
            File v2 = install(ctx, "jak1", "v2");
            boolean shownOn = mod(ctx, "jak1", "alpha").enabled;
            check("after deleting and reinstalling a game, its mods are not shown as active",
                    !(shownOn && read(new File(v2, cgo)).equals("game-v2")),
                    "the switch still says ON from the deleted installation");
        }

        {
            Context ctx = freshApp(base, "mod-conflict-refused");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("alpha", "jak1", new String[]{cgo, "GAME.CGO", "alpha"}));
            ModManager.importPackage(ctx, "jak1", modZip("beta", "jak1", new String[]{cgo, "GAME.CGO", "beta"}));
            ModManager.setEnabled(ctx, "jak1", "alpha", true);
            String error = null;
            try { ModManager.setEnabled(ctx, "jak1", "beta", true); } catch (Exception e) { error = e.getMessage(); }
            check("enabling a second mod on the same file is refused with an explanation",
                    error != null && error.startsWith("mod_conflict") && read(new File(data, cgo)).equals("alpha")
                            && !mod(ctx, "jak1", "beta").enabled, "error=" + error);
        }
        {
            Context ctx = freshApp(base, "mod-newfile-off");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("extra", "jak1",
                    new String[]{"out/jak1/iso/EXTRA.DGO", "EXTRA.DGO", "new level"}));
            ModManager.setEnabled(ctx, "jak1", "extra", true);
            ModManager.setEnabled(ctx, "jak1", "extra", false);
            check("disabling a mod that added a file removes that file again",
                    !new File(data, "out/jak1/iso/EXTRA.DGO").exists(), "");
        }
        {
            Context ctx = freshApp(base, "mod-rollback");
            File data = install(ctx, "jak1", "orig");
            ModManager.importPackage(ctx, "jak1", modZip("two", "jak1",
                    new String[]{cgo, "GAME.CGO", "two-game"}, new String[]{"out/jak1/iso/KERNEL.CGO", "KERNEL.CGO", "two-kernel"}));
            // The package is damaged after import (e.g. storage cleaned): the second payload is gone.
            new File(ctx.getFilesDir(), "mods/jak1/packages/two/KERNEL.CGO").delete();
            String error = null;
            try { ModManager.setEnabled(ctx, "jak1", "two", true); } catch (Exception e) { error = e.toString(); }
            check("a mod whose files disappear is rolled back completely",
                    error != null && read(new File(data, cgo)).equals("game-orig")
                            && read(new File(data, "out/jak1/iso/KERNEL.CGO")).equals("kernel-orig"),
                    "GAME.CGO='" + read(new File(data, cgo)) + "' error=" + error);
        }
        {
            Context ctx = freshApp(base, "save-import");
            GameSaves.Profile p1 = GameSaves.PROFILES.get("jak1");
            File dir = GameSaves.savesDir(ctx, "jak1");
            bank(GameSaves.bankFile(dir, 0, 0), p1, 3, 0x12345678, 2, new float[]{20, 450, 31}, 18.5f);
            byte[] garbage = new byte[p1.totalFileSize()];
            new Random(1).nextBytes(garbage);
            String error = null;
            try { GameSaves.importSlot(dir, p1, 0, Collections.singletonList(garbage)); }
            catch (IOException e) { error = e.getMessage(); }
            GameSaves.Slot s0 = GameSaves.readSlots(ctx, "jak1").get(0);
            check("importing a file that is not a save is refused and keeps the existing save",
                    error != null && s0.present && s0.saveCount == 3, "error=" + error + " present=" + s0.present);
            File tmp = new File(base, "valid.bin");
            bank(tmp, p1, 9, 0x12345678, 6, new float[]{60, 1500, 80}, 70f);
            GameSaves.importSlot(dir, p1, 0, Collections.singletonList(Files.readAllBytes(tmp.toPath())));
            s0 = GameSaves.readSlots(ctx, "jak1").get(0);
            check("importing a valid save replaces the slot and leaves no backups behind",
                    s0.present && s0.saveCount == 9 && s0.levelIndex == 6
                            && !new File(dir, "bank0.bin.bak").exists() && !new File(dir, "bank1.bin").exists(),
                    "count=" + s0.saveCount);
        }

        // ---------------- guided setup progress ----------------
        check("checking the ISO is reported before any stage", InstallProgress.stage("Comprobando que la ISO sea de Jak and Daxter…") == -1, "");
        check("copy maps to stage 1 and its percent", InstallProgress.stage("Copiando la ISO · 700 MB") == 0
                && InstallProgress.overall("Copiando la ISO · 700 MB", 50) == 7, "overall=" + InstallProgress.overall("Copiando la ISO · 700 MB", 50));
        check("native extraction and compile map to stages 2 and 4",
                InstallProgress.stage("Extrayendo y verificando la ISO…") == 1 && InstallProgress.stage("Compilando el juego para el teléfono…") == 3, "");
        check("final steps never show 100% before the installer says ready",
                InstallProgress.overall("Guardando la instalación y liberando la anterior…", 100) == 99, "");
        {
            Context ctx = freshApp(base, "latest-save");
            GameSaves.Profile p1 = GameSaves.PROFILES.get("jak1");
            File dir = GameSaves.savesDir(ctx, "jak1");
            check("no saves means a new game", GameSaves.latest(ctx, "jak1") == null, "");
            bank(GameSaves.bankFile(dir, 0, 0), p1, 3, 0x12345678, 2, new float[]{20, 450, 31}, 18.5f);
            bank(GameSaves.bankFile(dir, 2, 0), p1, 1, 0x12345678, 6, new float[]{60, 900, 70}, 60f);
            GameSaves.bankFile(dir, 0, 0).setLastModified(System.currentTimeMillis() - 86_400_000L);
            GameSaves.Slot last = GameSaves.latest(ctx, "jak1");
            check("Continuar uses the most recently written slot", last != null && last.index == 2 && last.levelIndex == 6,
                    "slot=" + (last == null ? "none" : last.index));
        }

        // Updates: version order, as build.gradle computes versionCode. (AI-assisted)
        check("versions order alpha < beta < rc < release < next patch",
                Versions.code("0.2.0-alpha.1") < Versions.code("0.2.0-alpha.2")
                        && Versions.code("0.2.0-alpha.2") < Versions.code("0.2.0-beta")
                        && Versions.code("0.2.0-beta") < Versions.code("0.2.0-rc.1")
                        && Versions.code("0.2.0-rc.1") < Versions.code("v0.2.0")
                        && Versions.code("0.2.0") < Versions.code("0.2.1-alpha.1"), "");
        check("version codes match build.gradle (0.1.0-beta = 10040, 0.2.0-beta = 20040)", Versions.code("0.1.0-beta") == 10040 && Versions.code("0.2.0-beta") == 20040, "");
        check("unparseable tags are ignored", Versions.code("nightly") < 0 && Versions.code("1.2") < 0
                && Versions.code("1.2.3-gamma") < 0, "");
        {
            JSONArray releases = new JSONArray();
            JSONObject apk = new JSONObject().put("name", "Precursor-0.2.0.apk").put("browser_download_url", "https://x/p.apk").put("size", 100);
            JSONObject alphaApk = new JSONObject().put("name", "Precursor-0.3.0-alpha.1.apk").put("browser_download_url", "https://x/a.apk").put("size", 100);
            releases.put(new JSONObject().put("tag_name", "v0.3.0-alpha.1").put("prerelease", true).put("assets", new JSONArray().put(alphaApk)));
            releases.put(new JSONObject().put("tag_name", "v0.4.0").put("draft", true).put("assets", new JSONArray().put(apk)));
            releases.put(new JSONObject().put("tag_name", "v0.2.5").put("assets", new JSONArray()));
            releases.put(new JSONObject().put("tag_name", "v0.2.0").put("body", "notes").put("assets", new JSONArray().put(apk)));
            Versions.Release stable = Versions.newest(releases, "0.1.0-beta", false);
            Versions.Release alpha = Versions.newest(releases, "0.1.0-beta", true);
            check("stable channel takes the newest normal release with an APK (not drafts, not pre-releases)",
                    stable != null && stable.version.equals("0.2.0") && stable.apkUrl.equals("https://x/p.apk"),
                    "got " + (stable == null ? "none" : stable.version));
            check("alpha channel also takes pre-releases", alpha != null && alpha.version.equals("0.3.0-alpha.1") && alpha.prerelease,
                    "got " + (alpha == null ? "none" : alpha.version));
            check("no update when already on the newest", Versions.newest(releases, "0.2.0", false) == null, "");
        }

        // Backups of saves and settings. (AI-assisted)
        {
            Context ctx = freshApp(base, "backups");
            File config = new File(ctx.getFilesDir(), "config");
            check("nothing to back up before the first game", Backups.snapshot(ctx, Backups.UPDATE) == null, "");
            File save = new File(config, "jak1/OpenGOAL/jak1/saves/BASCUS/bank0.bin");
            File settings = new File(config, "jak1/OpenGOAL/jak1/settings/pc-settings.gc");
            write(save, "slot-a");
            write(settings, "(settings 1)");
            write(new File(config, "jak1/crash.txt"), "dump");
            File copy = Backups.snapshot(ctx, Backups.UPDATE);
            check("a backup holds the saves and settings, not crash dumps",
                    copy != null && new File(copy, "config/jak1/OpenGOAL/jak1/saves/BASCUS/bank0.bin").isFile()
                            && new File(copy, "config/jak1/OpenGOAL/jak1/settings/pc-settings.gc").isFile()
                            && !new File(copy, "config/jak1/crash.txt").exists(), "copy=" + copy);
            write(save, "slot-b");
            write(new File(config, "jak1/OpenGOAL/jak1/saves/BASCUS/bank2.bin"), "newer-slot");
            Backups.restore(ctx, Backups.list(ctx).get(0));
            check("restoring brings the old save back and keeps files the copy did not have",
                    read(save).equals("slot-a") && new File(config, "jak1/OpenGOAL/jak1/saves/BASCUS/bank2.bin").isFile(), "");
            List<Backups.Backup> all = Backups.list(ctx);
            check("restoring first backs up what was there", all.size() == 2 && all.get(0).reason.equals(Backups.RESTORE)
                    && read(new File(all.get(0).dir, "config/jak1/OpenGOAL/jak1/saves/BASCUS/bank0.bin")).equals("slot-b"),
                    "count=" + all.size());
            for (int i = 0; i < Backups.KEEP + 2; i++) Backups.snapshot(ctx, "manual");
            check("only the newest copies are kept", Backups.list(ctx).size() == Backups.KEEP, "count=" + Backups.list(ctx).size());
        }

        // Preparing again after an app update. (AI-assisted)
        {
            Context ctx = freshApp(base, "stale");
            install(ctx, "jak1", "a");
            check("an installation from this version is current",
                    LauncherStore.staleness(ctx, "jak1", "tools", 1) == LauncherStore.CURRENT, "");
            check("new bundled game code only recommends preparing again",
                    LauncherStore.staleness(ctx, "jak1", "other-tools", 1) == LauncherStore.OUTDATED, "");
            check("a new game data version requires preparing again",
                    LauncherStore.staleness(ctx, "jak1", "tools", 2) == LauncherStore.INCOMPATIBLE, "");
            File tools = new File(ctx.getFilesDir(), "tools");
            new File(tools, "tools").mkdirs();
            new File(tools, "old-unused").mkdirs();
            new File(tools, "current").mkdirs();
            LauncherStore.discardUnusedTools(ctx, "current");
            check("unused tool sets are deleted, the installed and current ones kept",
                    new File(tools, "tools").isDirectory() && new File(tools, "current").isDirectory()
                            && !new File(tools, "old-unused").exists(), "");
        }

        System.out.println();
        System.out.println(ok + " OK, " + bugs + " BUG");
        for (String b : bugList) System.out.println("  - " + b);
        if (bugs > 0) System.exit(1);
    }
}
