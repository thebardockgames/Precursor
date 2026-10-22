package android.content;

import java.io.File;

/** Host-JVM stand-in: only what the launcher's non-UI classes call. (AI-assisted) */
public class Context {
    private final File files, cache;
    public Context(File root) {
        files = new File(root, "files"); cache = new File(root, "cache");
        files.mkdirs(); cache.mkdirs();
    }
    public File getFilesDir() { return files; }
    public File getCacheDir() { return cache; }
}
