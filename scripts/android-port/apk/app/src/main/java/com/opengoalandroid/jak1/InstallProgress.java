package com.opengoalandroid.jak1;

/**
 * Turns the installer's status messages (install-state.json, written by InstallService and the
 * native installer) into the guided setup's stages. Pure Java so it is tested on the host.
 * (AI-assisted)
 */
final class InstallProgress {
    private InstallProgress() {}

    /** Stage names shown in the "Preparando" step, in order. */
    static final String[] STAGES = {
            "Copiando la ISO", "Extrayendo archivos", "Preparando escenarios y audio",
            "Compilando para tu teléfono", "Últimos detalles"};

    /** Share of the whole installation each stage takes, roughly measured on device. */
    private static final int[] WEIGHT = {15, 15, 35, 30, 5};

    /** -1 while the ISO is still being checked, otherwise the index into STAGES. */
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
    static int overall(String message, int stagePercent) {
        int stage = stage(message);
        if (stage < 0) return 0;
        int done = 0;
        for (int i = 0; i < stage; i++) done += WEIGHT[i];
        int within = stagePercent >= 0 ? Math.min(100, stagePercent) : 0;
        return Math.min(99, done + WEIGHT[stage] * within / 100);
    }
}
