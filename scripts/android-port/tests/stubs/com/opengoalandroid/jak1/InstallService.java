package com.opengoalandroid.jak1;

/** Host-JVM stand-in exposing only the static helper ModManager uses (same body as the real one). (AI-assisted) */
final class InstallService {
    static String title(String game) {
        return "jak1".equals(game) ? "Jak and Daxter" : "jak2".equals(game) ? "Jak II" : "Jak 3";
    }
}
