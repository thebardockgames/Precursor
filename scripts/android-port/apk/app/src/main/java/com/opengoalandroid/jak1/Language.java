package com.opengoalandroid.jak1;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.content.res.Resources;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

/**
 * Precursor's language: the phone's own when it is one of SUPPORTED, otherwise English, unless the
 * player picked one in the launcher's Settings. Every activity and the installer service wrap their
 * context with it, and the game's text and subtitles follow it too (applyToGame). (AI-assisted)
 */
final class Language {
    private Language() {}

    /** Launcher languages, each with full game text in all three games (pt: see GAME_TEXT). */
    static final String[] SUPPORTED = {"en", "es", "fr", "de", "it", "pt"};
    /** Each language's own name, as the picker shows it. */
    static final String[] NAMES = {"English", "Español", "Français", "Deutsch", "Italiano", "Português (Brasil)"};
    /** "" follows the phone. */
    private static final String CHOSEN = "language";

    /**
     * The game's pc-language values (goal_src/jak{1,2,3}/pc/pckernel-impl.gc) for SUPPORTED, per game.
     * Portuguese: Jak 1 has only br-portuguese PC text (the rest falls back to English), Jak II has a
     * full br-portuguese translation, and Jak 3 shipped European Portuguese on the disc.
     */
    private static final int[][] GAME_TEXT = {
            {0, 3, 1, 2, 4, 13},  // jak1
            {0, 3, 1, 2, 4, 14},  // jak2
            {0, 3, 1, 2, 4, 9}};  // jak3
    /** The pc-settings.gc header each game writes (PC_KERNEL_VERSION in pckernel-impl.gc). */
    private static final String[] SETTINGS_VERSION = {"#x1000a00040000", "#x200000000", "#x300000000"};

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(GameOptionsActivity.PREFS, Context.MODE_PRIVATE);
    }

    /** The player's choice, or "" when Precursor follows the phone. */
    static String chosen(Context context) {
        String code = prefs(context).getString(CHOSEN, "");
        return index(code) < 0 ? "" : code;
    }

    static void choose(Context context, String code) {
        prefs(context).edit().putString(CHOSEN, index(code) < 0 ? "" : code).commit();
    }

    /** The phone's language if Precursor has it, otherwise English. */
    static String system() {
        String phone = Resources.getSystem().getConfiguration().getLocales().get(0).getLanguage();
        return index(phone) < 0 ? "en" : phone;
    }

    static String current(Context context) {
        String code = chosen(context);
        return code.isEmpty() ? system() : code;
    }

    static int index(String code) {
        for (int i = 0; i < SUPPORTED.length; i++) if (SUPPORTED[i].equals(code)) return i;
        return -1;
    }

    private static Locale locale(String code) {
        return "pt".equals(code) ? new Locale("pt", "BR") : new Locale(code);
    }

    /** For attachBaseContext: the same context in Precursor's language. */
    static Context wrap(Context base) {
        Configuration config = new Configuration(base.getResources().getConfiguration());
        config.setLocale(locale(current(base)));
        Context wrapped = base.createConfigurationContext(config);
        Resources resources = wrapped.getResources();
        String pkg = wrapped.getPackageName();
        Texts.lookup = key -> {
            int id = resources.getIdentifier(key, "string", pkg);
            return id == 0 ? null : resources.getString(id);
        };
        return wrapped;
    }

    /**
     * Makes the game's text and subtitle language match Precursor's. Only when Precursor's language
     * changed since the last time, so a language picked in the game's own menu is kept otherwise.
     * Before the first start there is no settings file yet: a minimal one carries the language and
     * the frame rate, and the game keeps its defaults (set just before reading it) for the rest. Audio stays as it is, since the
     * spoken languages depend on the disc. (AI-assisted)
     */
    static void applyToGame(Context context, String game, File configDir) {
        int gameIndex = "jak1".equals(game) ? 0 : "jak2".equals(game) ? 1 : "jak3".equals(game) ? 2 : -1;
        if (gameIndex < 0) return;
        String code = current(context);
        String appliedKey = "gameLanguage_" + game;
        File file = new File(configDir, "OpenGOAL/" + game + "/settings/pc-settings.gc");
        if (code.equals(prefs(context).getString(appliedKey, "")) && file.isFile()) return;
        int value = GAME_TEXT[gameIndex][index(code)];
        try {
            String text;
            if (file.isFile()) {
                text = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
                text = text.replaceAll("\\(text-language -?\\d+\\)", "(text-language " + value + ")")
                        .replaceAll("\\(subtitle-language -?\\d+\\)", "(subtitle-language " + value + ")");
            } else {
                File dir = file.getParentFile();
                if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create " + dir);
                // fps: reading it sends the frame rate to the runtime, as the defaults would.
                text = "(settings " + SETTINGS_VERSION[gameIndex] + "\n  (fps 60)\n  (text-language " + value + ")\n"
                        + "  (subtitle-language " + value + ")\n  )\n";
            }
            File temp = new File(file.getPath() + ".tmp");
            Files.write(temp.toPath(), text.getBytes(StandardCharsets.UTF_8));
            if (!temp.renameTo(file)) throw new IOException("Cannot replace " + file);
            prefs(context).edit().putString(appliedKey, code).commit();
        } catch (Exception error) {
            android.util.Log.w("OpenGOAL", "Cannot set the game's language", error);
        }
    }
}
