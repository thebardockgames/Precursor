package com.opengoalandroid.jak1;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Precursor's version names ("0.2.0", "0.2.0-beta", "0.3.0-alpha.2") and the choice of an update
 * among GitHub releases. Pure Java so it is tested on the host. build.gradle derives versionCode
 * with the same rules (code()), so Android also orders the APKs this way. (AI-assisted)
 */
final class Versions {
    private Versions() {}

    /**
     * major.minor.patch, then the stage: alpha.N = 20 + N, beta.N = 40 + N, rc.N = 60 + N and a
     * final release 99, so 0.2.0-alpha.1 < 0.2.0-beta < 0.2.0 < 0.2.1-alpha.1. Unparseable: -1.
     */
    static long code(String version) {
        if (version == null) return -1;
        String v = version.trim();
        if (v.startsWith("v") || v.startsWith("V")) v = v.substring(1);
        String core = v, pre = "";
        int dash = v.indexOf('-');
        if (dash >= 0) { core = v.substring(0, dash); pre = v.substring(dash + 1).toLowerCase(java.util.Locale.ROOT); }
        String[] parts = core.split("\\.");
        if (parts.length != 3) return -1;
        long number = 0;
        try {
            for (String part : parts) {
                int value = Integer.parseInt(part);
                if (value < 0 || value > 99) return -1;
                number = number * 100 + value;
            }
        } catch (NumberFormatException error) { return -1; }
        int stage;
        if (pre.isEmpty()) stage = 99;
        else {
            String label = pre, count = "0";
            int dot = pre.indexOf('.');
            if (dot >= 0) { label = pre.substring(0, dot); count = pre.substring(dot + 1); }
            int base = "alpha".equals(label) ? 20 : "beta".equals(label) ? 40 : "rc".equals(label) ? 60 : -1;
            int n;
            try { n = Integer.parseInt(count); } catch (NumberFormatException error) { return -1; }
            if (base < 0 || n < 0 || n > 19) return -1;
            stage = base + n;
        }
        return number * 100 + stage;
    }

    /** What an update offers: its version, release notes and the APK to download. */
    static final class Release {
        final String version, notes, apkUrl, apkName, page;
        final long size;
        final boolean prerelease;

        Release(String version, String notes, String apkUrl, String apkName, long size, boolean prerelease, String page) {
            this.version = version; this.notes = notes; this.apkUrl = apkUrl; this.apkName = apkName;
            this.size = size; this.prerelease = prerelease; this.page = page;
        }
    }

    /**
     * The newest release above `current` from GitHub's /releases list, or null. The stable channel
     * skips releases marked "pre-release"; the alpha channel takes both. Drafts and releases without
     * an attached .apk are ignored.
     */
    static Release newest(JSONArray releases, String current, boolean alphaChannel) throws Exception {
        long have = code(current);
        Release best = null;
        long bestCode = have;
        for (int i = 0; i < releases.length(); i++) {
            JSONObject release = releases.getJSONObject(i);
            if (release.optBoolean("draft", false)) continue;
            boolean pre = release.optBoolean("prerelease", false);
            if (pre && !alphaChannel) continue;
            String tag = release.optString("tag_name");
            long code = code(tag);
            if (code <= bestCode) continue;
            JSONArray assets = release.optJSONArray("assets");
            JSONObject apk = null;
            for (int a = 0; assets != null && a < assets.length(); a++) {
                JSONObject asset = assets.getJSONObject(a);
                String name = asset.optString("name").toLowerCase(java.util.Locale.ROOT);
                if (name.endsWith(".apk") && !name.contains("dev") && (apk == null || name.contains("precursor"))) apk = asset;
            }
            if (apk == null) continue;
            best = new Release(tag.startsWith("v") ? tag.substring(1) : tag, release.optString("body"),
                    apk.optString("browser_download_url"), apk.optString("name"), apk.optInt("size", -1), pre,
                    release.optString("html_url"));
            bestCode = code;
        }
        return best;
    }
}
