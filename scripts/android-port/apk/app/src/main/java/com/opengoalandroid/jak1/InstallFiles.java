package com.opengoalandroid.jak1;

import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.zip.*;

/** Files owned exclusively by the launcher; also tested on the host JVM. (AI-assisted) */
final class InstallFiles {
    static boolean validGame(String game) {
        return "jak1".equals(game) || "jak2".equals(game) || "jak3".equals(game);
    }

    static File child(File root, String relative) throws IOException {
        if (relative.isEmpty() || relative.startsWith("/") || relative.indexOf('\\') >= 0
                || new File(relative).isAbsolute()) throw new IOException("Ruta no válida");
        File child = new File(root, relative);
        String base = root.getCanonicalPath() + File.separator;
        if (!child.getCanonicalPath().startsWith(base)) throw new IOException("Ruta fuera de la instalación");
        return child;
    }

    static void mkdir(File dir) throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("No se pudo crear " + dir);
    }

    static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1024 * 1024];
        int size;
        while ((size = in.read(buffer)) != -1) out.write(buffer, 0, size);
    }

    static void unzip(InputStream input, File root) throws IOException {
        mkdir(root);
        // The archive is bundled in the APK, never supplied by the user.
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(input))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                File dest = child(root, entry.getName());
                if (entry.isDirectory()) {
                    mkdir(dest);
                } else {
                    mkdir(dest.getParentFile());
                    try (OutputStream out = new BufferedOutputStream(new FileOutputStream(dest))) {
                        copy(zip, out);
                    }
                }
                zip.closeEntry();
            }
        }
    }

    static void copyTree(File src, File dst) throws IOException {
        mkdir(dst);
        File[] children = src.listFiles();
        if (children == null) return;
        for (File child : children) {
            File target = new File(dst, child.getName());
            if (child.isDirectory()) {
                copyTree(child, target);
            } else {
                try (InputStream in = new FileInputStream(child); OutputStream out = new FileOutputStream(target)) {
                    copy(in, out);
                }
            }
        }
    }

    static void removeTree(File ownedRoot, File target) throws IOException {
        // No FOLLOW_LINKS: deleting a working directory must never traverse source symlinks.
        Path root = ownedRoot.getCanonicalFile().toPath();
        // /data/user/0 and /data/data may refer to the same directory on Android.
        // Canonicalize the parent, but never follow the final entry if it is a symlink.
        File absolute = target.getAbsoluteFile();
        Path path = new File(absolute.getParentFile().getCanonicalFile(), absolute.getName())
                .toPath().normalize();
        if (!path.startsWith(root) || path.equals(root)) throw new IOException("Limpieza fuera del área temporal");
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return;
        Files.walkFileTree(path, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }
            @Override public FileVisitResult postVisitDirectory(Path dir, IOException error) throws IOException {
                if (error != null) throw error;
                Files.delete(dir);
                return FileVisitResult.CONTINUE;
            }
        });
    }

    static void removeGameInstallation(File library, String game, String directory) throws IOException {
        if (!validGame(game) || !directory.matches(game + "-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
            throw new IOException("Directorio de instalación no válido");
        File target = child(library, directory);
        if (!target.getCanonicalFile().getParentFile().equals(library.getCanonicalFile()))
            throw new IOException("Ruta fuera de la biblioteca");
        removeTree(library, target);
    }
}
