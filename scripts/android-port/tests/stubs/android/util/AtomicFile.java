package android.util;

import java.io.*;

/** Same on-disk protocol as Android's AtomicFile: write to .new, then rename. (AI-assisted) */
public class AtomicFile {
    private final File base, newName, legacyBackup;
    public AtomicFile(File file) {
        base = file; newName = new File(file.getPath() + ".new"); legacyBackup = new File(file.getPath() + ".bak");
    }
    public FileInputStream openRead() throws FileNotFoundException {
        if (legacyBackup.exists()) { base.delete(); legacyBackup.renameTo(base); }
        if (newName.exists() && base.exists()) newName.delete();
        return new FileInputStream(base);
    }
    public FileOutputStream startWrite() throws IOException {
        if (legacyBackup.exists()) { base.delete(); legacyBackup.renameTo(base); }
        return new FileOutputStream(newName);
    }
    public void finishWrite(FileOutputStream out) throws IOException {
        out.getFD().sync(); out.close();
        if (!newName.renameTo(base)) { base.delete(); if (!newName.renameTo(base)) throw new IOException("rename"); }
    }
    public void failWrite(FileOutputStream out) { try { out.close(); } catch (IOException ignored) { } newName.delete(); }
    public void delete() { base.delete(); newName.delete(); legacyBackup.delete(); }
}
