package com.opengoalandroid.jak1;

import android.content.Context;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.*;

/**
 * Reads the memory-card save files OpenGOAL writes under each game's config directory.
 * Mirrors game/kernel/common/kmemcard.cpp (bank/header layout) and the `game-save` struct
 * from goal_src/&lt;game&gt;/engine/game/game-save.gc (info-int32 quick-summary block).
 * (AI-assisted)
 */
final class GameSaves {
    private GameSaves() {}

    static final int HEADER_SIZE = 0x400;
    static final int SLOT_COUNT = 4;

    /** One save slot as shown on the in-game load screen (a pair of alternating banks). */
    static final class Slot {
        final int index;
        final boolean present;
        final File activeFile;     // most recently written bank of the pair, or null
        final File otherFile;      // the other bank of the pair, may not exist
        final int levelIndex;
        final float[] stats;       // one entry per Profile.statIcons/statLabels
        final float completionPercent;
        final long saveCount;

        Slot(int index, boolean present, File activeFile, File otherFile, int levelIndex,
             float[] stats, float completionPercent, long saveCount) {
            this.index = index; this.present = present;
            this.activeFile = activeFile; this.otherFile = otherFile;
            this.levelIndex = levelIndex;
            this.stats = stats;
            this.completionPercent = completionPercent; this.saveCount = saveCount;
        }
    }

    /**
     * Per-game constants needed to decode a bank file and label its stats. The `game-save` struct
     * (decompiler/config/&lt;game&gt;/all-types.gc) shares its first 16 bytes (level-index) across all
     * three games, but the stat floats that follow it, and where completion-percentage lands, differ:
     * jak1 has three (fuel-cell/money/buzzer) before completion-percentage@32; jak2/jak3 have only two
     * (gem/skill) before completion-percentage@28.
     */
    static final class Profile {
        final String bascusId;
        final int bankSize; // BANK_SIZE from kmemcard.h; total file size is bankSize + 2*HEADER_SIZE
        final String[] statIcons;   // 2 or 3 pictographic glyphs, in display order
        final String[] statLabels;  // matching human labels
        final int[] statDenominators; // 0 means "no fixed total, show raw count"
        final int completionOffset; // offset of completion-percentage within the game-save object

        Profile(String bascusId, int bankSize, String[] statIcons, String[] statLabels,
                int[] statDenominators, int completionOffset) {
            this.bascusId = bascusId; this.bankSize = bankSize;
            this.statIcons = statIcons; this.statLabels = statLabels; this.statDenominators = statDenominators;
            this.completionOffset = completionOffset;
        }

        int totalFileSize() { return bankSize + 2 * HEADER_SIZE; }
    }

    static final Map<String, Profile> PROFILES = new HashMap<>();
    static {
        PROFILES.put("jak1", new Profile("BASCUS-97124AYBABTU!", 0x10000,
                new String[]{"🔵", "🥚", "🪰"}, // power cell, orb(egg), scout fly
                new String[]{"Power Cells", "Orbes", "Scout Flies"},
                new int[]{100, 2000, 112}, 32));
        PROFILES.put("jak2", new Profile("BASCUS-97265AYBABTU!", 0x20000,
                new String[]{"💎", "⚡"}, // gem, skill
                new String[]{"Gemas", "Habilidad"},
                new int[]{0, 0}, 28));
        PROFILES.put("jak3", new Profile("BASCUS-97330AYBABTU!", 0x1e000,
                new String[]{"💎", "⚡"},
                new String[]{"Gemas", "Habilidad"},
                new int[]{0, 0}, 28));
    }

    /** *level-task-data-remap* + *level-task-data* (goal_src/jak1/engine/ui/progress/progress-static.gc),
     *  names from decompiler_out/jak1/assets/game_text.txt (es-ES, index 3). Jak1 only: jak2/jak3 use a
     *  different, unmapped progress-menu structure, so those fall back to a generic "Nivel #N" label. */
    private static final int[] JAK1_LEVEL_REMAP = {
            0, 1, 2, 3, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 13, 13, 14, 15, 15, 4, 4};
    private static final String[] JAK1_LEVEL_NAMES = {
            "Roca del Géiser", "Aldea de Sandover", "Playa del Centinela", "Selva Prohibida",
            "Isla de Misty", "Desfiladero del Fuego", "Aldea de Piedra", "Ciudad Perdida de Precursor",
            "Pantano Cenagoso", "Cuenca de Precursor", "Paso entre las Montañas", "Cráter Volcánico",
            "Montaña Nevada", "Cueva de las Arañas", "Conducto de Lava", "Ciudadela de Gol y Maia"};

    static String levelName(String game, int levelIndex) {
        if ("jak1".equals(game) && levelIndex >= 1 && levelIndex <= JAK1_LEVEL_REMAP.length) {
            return JAK1_LEVEL_NAMES[JAK1_LEVEL_REMAP[levelIndex - 1]];
        }
        return levelIndex > 0 ? "Nivel #" + levelIndex : "Partida nueva";
    }

    static File savesDir(Context context, String game) {
        Profile profile = PROFILES.get(game);
        if (profile == null) return null;
        return new File(context.getFilesDir(), "config/" + game + "/OpenGOAL/" + game + "/saves/" + profile.bascusId);
    }

    /** Bank file for a given slot (0..3) and pair position (0 or 1): slot*2+pair. */
    static File bankFile(File savesDir, int slot, int pair) {
        return new File(savesDir, "bank" + (slot * 2 + pair) + ".bin");
    }

    static List<Slot> readSlots(Context context, String game) {
        Profile profile = PROFILES.get(game);
        File dir = savesDir(context, game);
        List<Slot> slots = new ArrayList<>();
        for (int i = 0; i < SLOT_COUNT; i++) {
            slots.add(readSlot(dir, profile, i));
        }
        return slots;
    }

    /** The most recently saved slot of a game (what "Continuar" shows), or null if none. (AI-assisted) */
    static Slot latest(Context context, String game) {
        Slot best = null;
        for (Slot slot : readSlots(context, game)) {
            if (!slot.present) continue;
            if (best == null || slot.activeFile.lastModified() > best.activeFile.lastModified()) best = slot;
        }
        return best;
    }

    private static Slot readSlot(File dir, Profile profile, int index) {
        File bankA = bankFile(dir, index, 0);
        File bankB = bankFile(dir, index, 1);
        BankData dataA = readBank(bankA, profile);
        BankData dataB = readBank(bankB, profile);
        BankData chosen; File active, other;
        if (dataA != null && (dataB == null || dataA.saveCount >= dataB.saveCount)) {
            chosen = dataA; active = bankA; other = bankB;
        } else if (dataB != null) {
            chosen = dataB; active = bankB; other = bankA;
        } else {
            return new Slot(index, false, null, null, 0, new float[0], 0, 0);
        }
        return new Slot(index, true, active, other, chosen.levelIndex,
                chosen.stats, chosen.completionPercent, chosen.saveCount);
    }

    static final class BankData {
        int levelIndex; float[] stats; float completionPercent; long saveCount;
    }

    /** Returns null if the file is missing, the wrong size, or fails the magic/checksum check. */
    private static BankData readBank(File file, Profile profile) {
        if (profile == null || !file.isFile() || file.length() != profile.totalFileSize()) return null;
        try (InputStream input = new FileInputStream(file)) {
            byte[] all = new byte[profile.totalFileSize()];
            new DataInputStream(input).readFully(all);
            return parseBank(all, profile);
        } catch (IOException error) {
            return null;
        }
    }

    /** mc_checksum() from game/kernel/common/kmemcard.cpp, over the bank data after the header. */
    static int checksum(byte[] bank, int offset, int size) {
        ByteBuffer buf = ByteBuffer.wrap(bank, offset, size).order(ByteOrder.LITTLE_ENDIAN);
        int result = 0;
        for (int i = 0; i < size / 4; i++) {
            result = (result << 1) ^ (result >> 31) ^ buf.getInt() ^ 0x12345678;
        }
        return result ^ 0xedd1e666;
    }

    /** Validates a whole bank file exactly like the game does when loading it. */
    static BankData parseBank(byte[] all, Profile profile) {
        if (profile == null || all.length != profile.totalFileSize()) return null;
        ByteBuffer header = ByteBuffer.wrap(all, 0, HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        long saveCount = header.getInt(0) & 0xffffffffL;
        if (header.getInt(8) != 0x12345678) return null;
        if (header.getInt(4) != checksum(all, HEADER_SIZE, profile.bankSize)) return null;
        // type-tag(4) + version/allocated/length(12) + info-int32(64), enough to cover
        // level-index@16 through completion-percentage (offset 28 or 32 depending on the game).
        ByteBuffer buf = ByteBuffer.wrap(all, HEADER_SIZE, 80).slice().order(ByteOrder.LITTLE_ENDIAN);
        BankData data = new BankData();
        data.saveCount = saveCount;
        data.levelIndex = buf.getInt(16);
        data.stats = new float[profile.statIcons.length];
        for (int i = 0; i < data.stats.length; i++) data.stats[i] = buf.getFloat(20 + i * 4);
        data.completionPercent = buf.getFloat(profile.completionOffset);
        return data;
    }

    /**
     * Replaces one slot with 1-2 bank files. Every bank must pass the game's own checks before
     * anything is touched; the previous pair is kept as bankN.bin.bak until the new one is fully
     * written, and restored if writing fails.
     */
    static void importSlot(File savesDir, Profile profile, int slot, List<byte[]> banks) throws IOException {
        if (banks.isEmpty() || banks.size() > 2) throw new IOException("El archivo no contiene una partida válida");
        for (byte[] bank : banks) {
            if (parseBank(bank, profile) == null)
                throw new IOException("El archivo no es una partida válida de este juego (o está dañada)");
        }
        InstallFiles.mkdir(savesDir);
        File[] targets = {bankFile(savesDir, slot, 0), bankFile(savesDir, slot, 1)};
        File[] backups = {new File(targets[0].getPath() + ".bak"), new File(targets[1].getPath() + ".bak")};
        for (int i = 0; i < 2; i++) {
            backups[i].delete();
            if (targets[i].exists() && !targets[i].renameTo(backups[i]))
                throw new IOException("No se pudo respaldar la partida actual");
        }
        try {
            for (int i = 0; i < banks.size(); i++) {
                File temp = new File(targets[i].getPath() + ".tmp");
                try (FileOutputStream out = new FileOutputStream(temp)) {
                    out.write(banks.get(i));
                    out.getFD().sync();
                }
                if (!temp.renameTo(targets[i])) throw new IOException("No se pudo escribir la partida");
            }
        } catch (IOException error) {
            for (int i = 0; i < 2; i++) {
                targets[i].delete();
                new File(targets[i].getPath() + ".tmp").delete();
                if (backups[i].exists()) backups[i].renameTo(targets[i]);
            }
            throw error;
        }
        for (File backup : backups) backup.delete();
    }
}
