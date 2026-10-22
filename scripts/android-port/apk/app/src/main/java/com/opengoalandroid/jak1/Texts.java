package com.opengoalandroid.jak1;

import java.util.Arrays;
import java.util.function.Function;

/**
 * Player-facing text for the classes that also run on the host JVM (GameSaves, ModManager), which
 * cannot use Android resources directly. Language.wrap() installs a lookup into the app's string
 * resources; without it (host tests) the key itself is returned, followed by its arguments.
 * (AI-assisted)
 */
final class Texts {
    private Texts() {}

    static volatile Function<String, String> lookup;

    static String get(String key, Object... args) {
        Function<String, String> source = lookup;
        String format = source == null ? null : source.apply(key);
        if (format == null) return args.length == 0 ? key : key + " " + Arrays.toString(args);
        return args.length == 0 ? format : String.format(format, args);
    }
}
