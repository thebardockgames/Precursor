package com.opengoalandroid.jak1;

import java.io.*;
import java.nio.file.*;
import java.util.zip.*;

/** No Android/JUnit dependency: exercises the launcher's actual file operations. (AI-assisted) */
public final class InstallFilesTest {
    static int passed;
    static void require(boolean value) { if (!value) throw new AssertionError(); passed++; }
    static void rejects(RunnableIO action) throws Exception {
        try { action.run(); throw new AssertionError("Operation unexpectedly succeeded"); }
        catch (IOException expected) { passed++; }
    }
    interface RunnableIO { void run() throws Exception; }
    static byte[] zip(String name, String value) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(output)) {
            zip.putNextEntry(new ZipEntry(name));
            zip.write(value.getBytes("UTF-8"));
            zip.closeEntry();
        }
        return output.toByteArray();
    }
    public static void main(String[] args) throws Exception {
        File base = Files.createTempDirectory(Paths.get(args[0]), "launcher-test-").toFile();
        try {
            require(InstallFiles.validGame("jak1") && InstallFiles.validGame("jak2") && InstallFiles.validGame("jak3"));
            require(!InstallFiles.validGame(null) && !InstallFiles.validGame("../jak1") && !InstallFiles.validGame("jakx"));
            File work = new File(base, "work");
            InstallFiles.unzip(new ByteArrayInputStream(zip("game/shader.txt", "complete")), work);
            require(new String(Files.readAllBytes(new File(work, "game/shader.txt").toPath()), "UTF-8").equals("complete"));
            rejects(() -> InstallFiles.child(work, "../save"));
            rejects(() -> InstallFiles.child(work, "/save"));
            rejects(() -> InstallFiles.child(work, "C:\\save"));
            rejects(() -> InstallFiles.child(work, ""));
            rejects(() -> InstallFiles.unzip(new ByteArrayInputStream(zip("../save", "bad")), work));
            require(!new File(base, "save").exists());
            rejects(() -> InstallFiles.removeTree(base, base));
            rejects(() -> InstallFiles.removeTree(work, base));
            File saved = new File(base, "saved");
            Files.write(saved.toPath(), new byte[]{42});
            InstallFiles.removeTree(base, work);
            require(!work.exists() && saved.isFile());
            File real = new File(base, "real");
            InstallFiles.mkdir(real);
            File alias = new File(base, "alias");
            boolean linked = false;
            try {
                Files.createSymbolicLink(alias.toPath(), real.getCanonicalFile().toPath());
                linked = true;
            } catch (IOException | UnsupportedOperationException error) {
                System.out.println("SKIP symlink checks: host cannot create symbolic links");
            }
            if (linked) {
                File nested = new File(alias, "temporary");
                InstallFiles.mkdir(nested);
                InstallFiles.removeTree(alias, nested);
                require(!nested.exists());
                Files.write(new File(real, "keep").toPath(), new byte[]{1});
                InstallFiles.removeTree(base, alias);
                require(new File(real, "keep").isFile());
            }
            File library = new File(base, "library");
            String directory = "jak2-12345678-1234-1234-1234-123456789abc";
            File installed = new File(library, directory);
            InstallFiles.mkdir(installed);
            Files.write(new File(installed, "GAME.CGO").toPath(), new byte[]{2});
            File otherGame = new File(library, "jak1-12345678-1234-1234-1234-123456789abc");
            InstallFiles.mkdir(otherGame);
            rejects(() -> InstallFiles.removeGameInstallation(library, "jak2", "../saved"));
            rejects(() -> InstallFiles.removeGameInstallation(library, "jak1", directory));
            rejects(() -> InstallFiles.removeGameInstallation(library, "jak2", ""));
            rejects(() -> InstallFiles.removeGameInstallation(library, "jakx", directory));
            require(installed.isDirectory());
            if (linked) Files.createSymbolicLink(new File(installed, "tools").toPath(), real.getCanonicalFile().toPath());
            InstallFiles.removeGameInstallation(library, "jak2", directory);
            require(!installed.exists() && otherGame.isDirectory() && saved.isFile());
            if (linked) require(new File(real, "keep").isFile());
            InstallFiles.removeGameInstallation(library, "jak2", directory);
            require(otherGame.isDirectory());
            System.out.println(passed + " checks passed");
        } finally { InstallFiles.removeTree(base.getParentFile(), base); }
    }
}
