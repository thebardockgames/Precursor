package com.opengoalandroid.jak1;

/**
 * The guided setup's stages from the installer's state (install-state.json). InstallService writes
 * the stage next to a message in the player's language; the native installer reports Spanish
 * messages, which stage(String) recognizes. Pure Java so it is tested on the host. (AI-assisted)
 */
final class InstallProgress {
    private InstallProgress() {}

    /** Stages shown in the "Preparing" step; their names are the stage_* string resources. */
    static final int STAGE_COUNT = 5;

    /** Share of the whole installation each stage takes, roughly measured on device. */
    private static final int[] WEIGHT = {15, 15, 35, 30, 5};

    /** -1 while the ISO is still being checked, otherwise the stage (0 to STAGE_COUNT - 1). */
    static int stage(org.json.JSONObject job) {
        return job.has("stage") ? job.optInt("stage", -1) : stage(job.optString("message"));
    }

    /** The same from a message alone (the native installer's, or a state written without a stage). */
    static int stage(String message) {
        if (message == null) return -1;
        if (message.startsWith("Preparando la instalación") || message.startsWith("Comprobando que la ISO")
                || message.startsWith("Iniciando")) return -1;
        if (message.startsWith("Preparando las herramientas") || message.startsWith("Copiando la ISO")) return 0;
        if (message.startsWith("Comprobando el juego") || message.startsWith("Extrayendo")) return 1;
        if (message.startsWith("Preparando escenarios")) return 2;
        if (message.startsWith("Compilando")) return 3;
        if (message.startsWith("Finalizando") || message.startsWith("Guardando")) return 4;
        return -1;
    }

    /** Whole-installation percentage for the progress bar and the Orbe ring. */
    static int overall(org.json.JSONObject job) {
        return overall(stage(job), job.optInt("percent", -1));
    }

    static int overall(String message, int stagePercent) {
        return overall(stage(message), stagePercent);
    }

    private static int overall(int stage, int stagePercent) {
        if (stage < 0) return 0;
        int done = 0;
        for (int i = 0; i < stage; i++) done += WEIGHT[i];
        int within = stagePercent >= 0 ? Math.min(100, stagePercent) : 0;
        return Math.min(99, done + WEIGHT[stage] * within / 100);
    }
}
