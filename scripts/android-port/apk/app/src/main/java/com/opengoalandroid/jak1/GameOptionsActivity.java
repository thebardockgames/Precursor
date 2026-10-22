package com.opengoalandroid.jak1;

import android.app.*;
import android.content.*;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.zip.*;

/**
 * One game's options, reached from "Partidas y opciones" on the shelf: saves (export, replace,
 * import), mods, touch controls and removing the installation. Tabs switch with LB/RB on a
 * gamepad; B goes back. Landscape shows the tabs as a side column, portrait as chips under the
 * game's art. (AI-assisted)
 */
public final class GameOptionsActivity extends Activity {
    private static final int EXPORT_SLOT = 20, IMPORT_SLOT = 21, IMPORT_MOD = 30;
    private static final int[] TABS = {R.string.tab_saves, R.string.tab_graphics, R.string.tab_mods,
            R.string.tab_controls, R.string.tab_remove};
    private static final int TAB_SAVES = 0, TAB_GRAPHICS = 1, TAB_MODS = 2, TAB_CONTROLS = 3;
    /** Shared with Jak1Activity, which reads it when the game starts. */
    static final String PREFS = "precursor", TOUCH_MODE = "touchMode";
    /** Internal resolution choices, in percent of the screen's size. */
    static final int[] RENDER_SCALES = {100, 75, 67, 50};

    // Graphics options are per game (Jak 3 needs more than Jak 1); Jak1Activity applies them at start.
    static int renderScale(Context context, String game) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE).getInt("renderScale_" + game, 100);
    }

    static boolean showFps(Context context, String game) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean("showFps_" + game, true);
    }

    static boolean sustainedPerformance(Context context, String game) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean("sustained_" + game, false);
    }

    private String game;
    private int tab, pendingSlot = -1;
    private LinearLayout content;
    private final TextView[] tabViews = new TextView[TABS.length];

    /** Slides back out to the right while the shelf comes forward. (AI-assisted) */
    @Override public void finish() {
        super.finish();
        overridePendingTransition(R.anim.shelf_enter, R.anim.options_exit);
    }

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(Language.wrap(base)); }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        game = getIntent().getStringExtra("game");
        if (!InstallFiles.validGame(game)) { finish(); return; }
        if (saved != null) { tab = saved.getInt("tab", 0); pendingSlot = saved.getInt("pendingSlot", -1); }
        setContentView(build());
        UiKit.immersive(this);
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putInt("tab", tab);
        state.putInt("pendingSlot", pendingSlot);
        super.onSaveInstanceState(state);
    }

    @Override protected void onResume() { super.onResume(); if (content != null) showTab(tab); }

    private boolean wide() { return getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE; }
    private int dp(int v) { return UiKit.dp(this, v); }
    private TextView label(String text, int size, int color, boolean bold) { return UiKit.label(this, text, size, color, bold); }
    private int coverRes() {
        return "jak1".equals(game) ? R.drawable.cover_jak1 : "jak2".equals(game) ? R.drawable.cover_jak2_playstation : R.drawable.cover_jak3;
    }

    private View build() {
        FrameLayout frame = new FrameLayout(this);
        frame.setBackgroundColor(UiKit.BACKGROUND);
        content = UiKit.column(this);
        content.setPadding(dp(wide() ? 24 : 18), dp(8), dp(wide() ? 24 : 18), dp(48));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        if (wide()) {
            LinearLayout row = new LinearLayout(this);
            LinearLayout side = UiKit.column(this);
            side.setPadding(dp(24), dp(20), dp(16), dp(20));
            side.setBackgroundColor(0xFF0C1319);
            side.setClipChildren(false);
            side.setClipToPadding(false);
            side.addView(backButton());
            TextView sub = label(getString(R.string.options_caps), 11, UiKit.MUTED, true);
            sub.setLetterSpacing(.12f);
            UiKit.margin(this, side, sub, 14);
            TextView title = label(InstallService.title(game), 22, UiKit.TEXT, true);
            title.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
            side.addView(title);
            for (int i = 0; i < TABS.length; i++) UiKit.margin(this, side, tabView(i, false), i == 0 ? 18 : 8);
            row.addView(side, new LinearLayout.LayoutParams(dp(250), -1));
            LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(0, -1, 1);
            content.setPadding(dp(24), dp(24), dp(24), dp(48));
            row.addView(scroll, scrollParams);
            frame.addView(row, new FrameLayout.LayoutParams(-1, -1));
        } else {
            LinearLayout col = UiKit.column(this);
            FrameLayout head = new FrameLayout(this);
            ImageView art = new ImageView(this);
            art.setImageResource(coverRes());
            art.setScaleType(ImageView.ScaleType.CENTER_CROP);
            head.addView(art, new FrameLayout.LayoutParams(-1, -1));
            View fade = new View(this);
            fade.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[]{0x26101820, UiKit.BACKGROUND}));
            head.addView(fade, new FrameLayout.LayoutParams(-1, -1));
            LinearLayout title = new LinearLayout(this);
            title.setGravity(Gravity.CENTER_VERTICAL);
            title.setPadding(dp(18), 0, dp(18), dp(12));
            title.addView(backButton());
            LinearLayout names = UiKit.column(this);
            names.setPadding(dp(12), 0, 0, 0);
            TextView sub = label(getString(R.string.options_caps), 11, UiKit.MUTED, true);
            sub.setLetterSpacing(.12f);
            names.addView(sub);
            TextView name = label(InstallService.title(game), 24, UiKit.TEXT, true);
            name.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
            names.addView(name);
            title.addView(names);
            head.addView(title, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
            col.addView(head, new LinearLayout.LayoutParams(-1, dp(150)));
            HorizontalScrollView chips = new HorizontalScrollView(this);
            chips.setHorizontalScrollBarEnabled(false);
            chips.setClipToPadding(false);
            chips.setClipChildren(false);
            LinearLayout chipRow = new LinearLayout(this);
            chipRow.setPadding(dp(18), dp(6), dp(18), dp(10));
            for (int i = 0; i < TABS.length; i++) {
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-2, -2);
                if (i > 0) params.leftMargin = dp(8);
                chipRow.addView(tabView(i, true), params);
            }
            chips.addView(chipRow);
            col.addView(chips);
            col.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
            frame.addView(col, new FrameLayout.LayoutParams(-1, -1));
        }
        View hints = Gamepad.hintBar(this, Gamepad.Action.CONFIRM, getString(R.string.hint_open), Gamepad.Action.BACK, getString(R.string.back),
                Gamepad.Action.PREV, "", Gamepad.Action.NEXT, getString(R.string.hint_section));
        if (hints != null) {
            hints.setPadding(dp(16), dp(6), dp(20), dp(10));
            hints.setBackgroundColor(0xCC0B1116);
            frame.addView(hints, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
        }
        showTab(tab);
        if (!frame.isInTouchMode()) frame.post(frame::restoreDefaultFocus);
        return frame;
    }

    private View backButton() {
        TextView back = label("←", 20, UiKit.TEXT, true);
        back.setGravity(Gravity.CENTER);
        back.setBackground(UiKit.shape(this, 0x73000000, 99));
        back.setContentDescription(getString(R.string.back));
        back.setLayoutParams(new LinearLayout.LayoutParams(dp(44), dp(44)));
        back.setMinWidth(dp(44));
        back.setMinHeight(dp(44));
        Gamepad.focusable(back, 99);
        back.setOnClickListener(v -> finish());
        return back;
    }

    private TextView tabView(int index, boolean chip) {
        TextView view = label(getString(TABS[index]), 14, UiKit.MUTED, true);
        view.setPadding(dp(14), dp(chip ? 8 : 12), dp(14), dp(chip ? 8 : 12));
        view.setContentDescription(getString(R.string.section, getString(TABS[index])));
        Gamepad.focusable(view, chip ? 99 : 12);
        view.setOnClickListener(v -> showTab(index));
        if (index == tab) view.setFocusedByDefault(true);
        tabViews[index] = view;
        return view;
    }

    private void showTab(int index) {
        tab = index;
        for (int i = 0; i < TABS.length; i++) {
            if (tabViews[i] == null) continue;
            boolean on = i == index;
            tabViews[i].setTextColor(on ? UiKit.ACCENT : UiKit.MUTED);
            tabViews[i].setBackground(UiKit.shape(this, on ? 0x294EE878 : 0x12FFFFFF, wide() ? 12 : 99));
        }
        content.removeAllViews();
        if (index == TAB_SAVES) buildSaves();
        else if (index == TAB_GRAPHICS) buildGraphics();
        else if (index == TAB_MODS) buildMods();
        else if (index == TAB_CONTROLS) buildControls();
        else buildRemove();
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        Gamepad.Action action = Gamepad.action(event);
        if (action == Gamepad.Action.NONE) return super.dispatchKeyEvent(event);
        if (event.getAction() != KeyEvent.ACTION_UP) return true;
        if (getWindow().getDecorView().isInTouchMode() && (action == Gamepad.Action.CONFIRM || action == Gamepad.Action.START)) {
            if (tabViews[tab] != null) tabViews[tab].requestFocusFromTouch();
            return true;
        }
        switch (action) {
            case CONFIRM: case START: {
                View focused = getCurrentFocus();
                if (focused != null) focused.performClick();
                return true;
            }
            case BACK: finish(); return true;
            case PREV: case NEXT: {
                int next = (tab + (action == Gamepad.Action.NEXT ? 1 : TABS.length - 1)) % TABS.length;
                showTab(next);
                if (tabViews[next] != null) tabViews[next].requestFocusFromTouch();
                return true;
            }
            default: return true;
        }
    }

    // ---------------------------------------------------------------- saves

    private void buildSaves() {
        GameSaves.Profile profile = GameSaves.PROFILES.get(game);
        heading(getString(R.string.saves_title), getString(R.string.saves_hint));
        for (GameSaves.Slot slot : GameSaves.readSlots(this, game)) {
            LinearLayout card = UiKit.column(this);
            card.setBackground(slot.present ? UiKit.shape(this, UiKit.PANEL, 16) : dashed());
            card.setPadding(dp(16), dp(14), dp(16), dp(14));
            LinearLayout top = new LinearLayout(this);
            top.setGravity(Gravity.CENTER_VERTICAL);
            String heading = slot.present ? UiKit.levelName(this, game, slot.levelIndex) : getString(R.string.slot_empty);
            top.addView(label(getString(R.string.slot_title, slot.index + 1, heading), 15, UiKit.TEXT, true), new LinearLayout.LayoutParams(0, -2, 1));
            if (slot.present) top.addView(label(((int) slot.completionPercent) + "%", 17, UiKit.ACCENT, true));
            card.addView(top);
            if (slot.present) {
                StringBuilder stats = new StringBuilder();
                for (int i = 0; i < profile.statIcons.length; i++) {
                    int denom = profile.statDenominators[i];
                    if (i > 0) stats.append("     ");
                    stats.append(profile.statIcons[i]).append(' ').append((int) slot.stats[i]);
                    if (denom > 0) stats.append('/').append(denom);
                }
                UiKit.margin(this, card, label(stats.toString(), 13, UiKit.MUTED, false), 8);
                LinearLayout acts = new LinearLayout(this);
                acts.addView(smallButton(getString(R.string.export), UiKit.ACCENT, v -> exportSlot(slot)));
                LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, -2);
                p.leftMargin = dp(10);
                acts.addView(smallButton(getString(R.string.replace), 0xFFFF9A80, v -> confirmReplace(slot)), p);
                UiKit.margin(this, card, acts, 12);
            } else {
                UiKit.margin(this, card, label(getString(R.string.no_saved_data), 13, UiKit.MUTED, false), 6);
                UiKit.margin(this, card, smallButton(getString(R.string.import_save), UiKit.ACCENT, v -> beginImport(slot.index)), 10);
            }
            UiKit.margin(this, content, card, 12);
        }
    }

    private GradientDrawable dashed() {
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(16));
        shape.setStroke(dp(1), 0x40FFFFFF, dp(6), dp(5));
        return shape;
    }

    private void exportSlot(GameSaves.Slot slot) {
        String level = GameSaves.levelName(game, slot.levelIndex).toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "-").replaceAll("(^-|-$)", "");
        pendingSlot = slot.index;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE)
                .putExtra(Intent.EXTRA_TITLE, game + "-slot" + (slot.index + 1) + "-" + (level.isEmpty() ? "save" : level) + ".zip");
        try { startActivityForResult(intent, EXPORT_SLOT); }
        catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
    }

    private void confirmReplace(GameSaves.Slot slot) {
        new AlertDialog.Builder(this).setTitle(getString(R.string.replace_title, slot.index + 1))
                .setMessage(R.string.replace_body)
                .setNegativeButton(R.string.cancel, null)
                .setPositiveButton(R.string.choose_file, (d, w) -> beginImport(slot.index)).show();
    }

    private void beginImport(int slotIndex) {
        pendingSlot = slotIndex;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(intent, IMPORT_SLOT); }
        catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
    }

    private void doExport(Uri destination, int slotIndex) {
        new Thread(() -> {
            try {
                File dir = GameSaves.savesDir(this, game);
                try (OutputStream raw = getContentResolver().openOutputStream(destination, "wt");
                     ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(raw))) {
                    if (raw == null) throw new IOException(getString(R.string.cannot_open_destination));
                    for (File bank : new File[]{GameSaves.bankFile(dir, slotIndex, 0), GameSaves.bankFile(dir, slotIndex, 1)}) {
                        if (!bank.isFile()) continue;
                        zip.putNextEntry(new ZipEntry(bank.getName()));
                        try (InputStream input = new FileInputStream(bank)) { InstallFiles.copy(input, zip); }
                        zip.closeEntry();
                    }
                }
                runOnUiThread(() -> Toast.makeText(this, R.string.save_exported, Toast.LENGTH_SHORT).show());
            } catch (Exception error) {
                runOnUiThread(() -> showError(getString(R.string.export_failed, error.getMessage())));
            }
        }, "Export save").start();
    }

    private void doImport(Uri source, int slotIndex) {
        new Thread(() -> {
            try {
                GameSaves.Profile profile = GameSaves.PROFILES.get(game);
                List<byte[]> banks = new ArrayList<>();
                try (InputStream input = getContentResolver().openInputStream(source)) {
                    if (input == null) throw new IOException(getString(R.string.cannot_open_file));
                    ByteArrayOutputStream all = new ByteArrayOutputStream();
                    InstallFiles.copy(input, all);
                    byte[] bytes = all.toByteArray();
                    if (bytes.length >= 2 && bytes[0] == 'P' && bytes[1] == 'K') {
                        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(bytes))) {
                            ZipEntry entry;
                            while ((entry = zip.getNextEntry()) != null && banks.size() < 2) {
                                ByteArrayOutputStream one = new ByteArrayOutputStream();
                                InstallFiles.copy(zip, one);
                                if (one.size() == profile.totalFileSize()) banks.add(one.toByteArray());
                                zip.closeEntry();
                            }
                        }
                    } else if (bytes.length == profile.totalFileSize()) {
                        banks.add(bytes);
                    }
                }
                if (banks.isEmpty()) throw new IOException(getString(R.string.invalid_save_for, InstallService.title(game)));
                GameSaves.importSlot(GameSaves.savesDir(this, game), profile, slotIndex, banks);
                runOnUiThread(() -> { Toast.makeText(this, R.string.save_imported, Toast.LENGTH_SHORT).show(); showTab(TAB_SAVES); });
            } catch (Exception error) {
                runOnUiThread(() -> showError(getString(R.string.import_failed, error.getMessage())));
            }
        }, "Import save").start();
    }

    // ---------------------------------------------------------------- mods

    private void buildMods() {
        heading(getString(R.string.tab_mods), getString(R.string.mods_hint));
        UiKit.margin(this, content, smallButton(getString(R.string.import_mod), UiKit.ACCENT, v -> pickMod()), 12);
        List<ModManager.Mod> mods = ModManager.listMods(this, game);
        if (mods.isEmpty()) UiKit.margin(this, content, label(getString(R.string.no_mods), 14, UiKit.MUTED, false), 16);
        for (ModManager.Mod mod : mods) {
            LinearLayout card = UiKit.column(this);
            card.setBackground(UiKit.shape(this, UiKit.PANEL, 16));
            card.setPadding(dp(16), dp(14), dp(16), dp(14));
            card.addView(label(mod.name, 15, UiKit.TEXT, true));
            if (!mod.description.isEmpty()) UiKit.margin(this, card, label(mod.description, 13, UiKit.MUTED, false), 4);
            LinearLayout acts = new LinearLayout(this);
            acts.addView(smallButton(getString(mod.enabled ? R.string.disable : R.string.enable), mod.enabled ? 0xFFFF9A80 : UiKit.ACCENT, v -> toggleMod(mod)));
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, -2);
            p.leftMargin = dp(10);
            acts.addView(smallButton(getString(R.string.delete), UiKit.MUTED, v -> confirmRemoveMod(mod)), p);
            UiKit.margin(this, card, acts, 12);
            TextView state = label(getString(mod.enabled ? R.string.mod_state_enabled : R.string.mod_state_disabled), 12, mod.enabled ? UiKit.ACCENT : UiKit.MUTED, true);
            UiKit.margin(this, card, state, 8);
            UiKit.margin(this, content, card, 12);
        }
    }

    private void toggleMod(ModManager.Mod mod) {
        new Thread(() -> {
            try {
                ModManager.setEnabled(this, game, mod.id, !mod.enabled);
                runOnUiThread(() -> { Toast.makeText(this, getString(mod.enabled ? R.string.mod_now_disabled : R.string.mod_now_enabled, mod.name), Toast.LENGTH_SHORT).show(); showTab(TAB_MODS); });
            } catch (Exception error) {
                runOnUiThread(() -> showError(getString(R.string.apply_failed, error.getMessage())));
            }
        }, "Toggle mod").start();
    }

    private void confirmRemoveMod(ModManager.Mod mod) {
        new AlertDialog.Builder(this).setTitle(getString(R.string.delete_mod_title, mod.name))
                .setMessage(mod.enabled ? R.string.delete_mod_enabled_body : R.string.delete_mod_body)
                .setNegativeButton(R.string.cancel, null)
                .setPositiveButton(R.string.delete, (d, w) -> new Thread(() -> {
                    try {
                        ModManager.deletePackage(this, game, mod.id);
                        runOnUiThread(() -> showTab(TAB_MODS));
                    } catch (Exception error) {
                        runOnUiThread(() -> showError(getString(R.string.delete_failed, error.getMessage())));
                    }
                }, "Delete mod").start()).show();
    }

    private void pickMod() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(intent, IMPORT_MOD); }
        catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
    }

    // ---------------------------------------------------------------- controls

    private void buildControls() {
        heading(getString(R.string.controls_title), getString(R.string.controls_hint));
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        String current = prefs.getString(TOUCH_MODE, "auto");
        String[][] options = {
                {"auto", getString(R.string.touch_auto), getString(R.string.touch_auto_desc)},
                {"always", getString(R.string.touch_always), getString(R.string.touch_always_desc)},
                {"never", getString(R.string.touch_never), getString(R.string.touch_never_desc)}};
        for (String[] option : options) {
            boolean on = option[0].equals(current);
            LinearLayout card = UiKit.column(this);
            card.setBackground(UiKit.shape(this, on ? 0x294EE878 : UiKit.PANEL, 16));
            card.setPadding(dp(16), dp(14), dp(16), dp(14));
            card.addView(label((on ? "●  " : "○  ") + option[1], 15, on ? UiKit.ACCENT : UiKit.TEXT, true));
            UiKit.margin(this, card, label(option[2], 13, UiKit.MUTED, false), 4);
            Gamepad.focusable(card, 16);
            card.setOnClickListener(v -> { prefs.edit().putString(TOUCH_MODE, option[0]).apply(); showTab(TAB_CONTROLS); });
            UiKit.margin(this, content, card, 12);
        }
    }

    // ---------------------------------------------------------------- graphics

    /**
     * What the game's own graphics menu cannot do on a phone: Android only offers the native
     * resolution, so the internal one is scaled here; plus the FPS counter and Android's
     * sustained performance mode. Applied when the game starts. (AI-assisted)
     */
    private void buildGraphics() {
        heading(getString(R.string.tab_graphics), getString(R.string.graphics_hint));
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);

        subheading(getString(R.string.render_scale_title), getString(R.string.render_scale_desc));
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int longSide = Math.max(metrics.widthPixels, metrics.heightPixels);
        int shortSide = Math.min(metrics.widthPixels, metrics.heightPixels);
        int current = renderScale(this, game);
        for (int scale : RENDER_SCALES) {
            int w = (longSide * scale / 100) & ~1, h = (shortSide * scale / 100) & ~1;
            String text = getString(scale == 100 ? R.string.render_scale_native : R.string.render_scale_option, scale, w, h);
            choiceCard(text, null, scale == current, true,
                    () -> { prefs.edit().putInt("renderScale_" + game, scale).apply(); showTab(TAB_GRAPHICS); });
        }

        subheading(getString(R.string.other_title), null);
        boolean fps = showFps(this, game);
        choiceCard(getString(R.string.show_fps), getString(R.string.show_fps_desc), fps, true,
                () -> { prefs.edit().putBoolean("showFps_" + game, !fps).apply(); showTab(TAB_GRAPHICS); });
        boolean supported = getSystemService(android.os.PowerManager.class).isSustainedPerformanceModeSupported();
        boolean sustained = supported && sustainedPerformance(this, game);
        choiceCard(getString(R.string.sustained), getString(supported ? R.string.sustained_desc : R.string.sustained_unsupported),
                sustained, supported,
                () -> { prefs.edit().putBoolean("sustained_" + game, !sustained).apply(); showTab(TAB_GRAPHICS); });
    }

    /** A selectable card like the touch-control choices; a disabled one is dimmed and inert. */
    private void choiceCard(String title, String description, boolean on, boolean enabled, Runnable click) {
        LinearLayout card = UiKit.column(this);
        card.setBackground(UiKit.shape(this, on ? 0x294EE878 : UiKit.PANEL, 16));
        card.setPadding(dp(16), dp(14), dp(16), dp(14));
        card.addView(label((on ? "●  " : "○  ") + title, 15, on ? UiKit.ACCENT : UiKit.TEXT, true));
        if (description != null) UiKit.margin(this, card, label(description, 13, UiKit.MUTED, false), 4);
        if (enabled) {
            Gamepad.focusable(card, 16);
            card.setOnClickListener(v -> click.run());
        } else {
            card.setAlpha(.5f);
        }
        UiKit.margin(this, content, card, 10);
    }

    private void subheading(String title, String hint) {
        TextView t = label(title, 16, UiKit.TEXT, true);
        UiKit.margin(this, content, t, 22);
        if (hint != null) UiKit.margin(this, content, label(hint, 13, UiKit.MUTED, false), 2);
    }

    // ---------------------------------------------------------------- remove

    private void buildRemove() {
        String title = InstallService.title(game);
        heading(getString(R.string.remove_title), getString(R.string.remove_hint, title));
        UiKit.margin(this, content, smallButton(getString(R.string.remove_button, title), 0xFFFF9A80, v ->
                new AlertDialog.Builder(this).setTitle(getString(R.string.remove_confirm_title, title))
                        .setMessage(R.string.remove_confirm_body)
                        .setNegativeButton(R.string.cancel, null)
                        .setPositiveButton(R.string.remove, (d, w) -> {
                            if (LauncherStore.installing(this)) { showError(getString(R.string.wait_for_preparation)); return; }
                            startForegroundService(new Intent(this, InstallService.class).setAction(InstallService.REMOVE).putExtra("game", game));
                            Toast.makeText(this, getString(R.string.removing_game, title), Toast.LENGTH_SHORT).show();
                            finish();
                        }).show()), 16);
    }

    // ---------------------------------------------------------------- shared

    private void heading(String title, String hint) {
        TextView t = label(title, 22, UiKit.TEXT, true);
        t.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        content.addView(t);
        UiKit.margin(this, content, label(hint, 13, UiKit.MUTED, false), 4);
    }

    private Button smallButton(String text, int color, View.OnClickListener click) {
        Button button = UiKit.button(this, text, 0x1FFFFFFF, color);
        button.setBackground(UiKit.shape(this, 0x1FFFFFFF, 99));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setPadding(dp(16), dp(8), dp(16), dp(8));
        button.setOnClickListener(click);
        return Gamepad.focusable(button, 99);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (request == IMPORT_MOD) {
            new Thread(() -> {
                try (InputStream input = getContentResolver().openInputStream(uri)) {
                    if (input == null) throw new IOException(getString(R.string.cannot_open_file));
                    ModManager.Mod mod = ModManager.importPackage(this, game, input);
                    runOnUiThread(() -> { Toast.makeText(this, getString(R.string.mod_imported, mod.name), Toast.LENGTH_SHORT).show(); showTab(TAB_MODS); });
                } catch (Exception error) {
                    runOnUiThread(() -> showError(getString(R.string.import_failed, error.getMessage())));
                }
            }, "Import mod").start();
            return;
        }
        if (pendingSlot < 0) return;
        int slot = pendingSlot;
        pendingSlot = -1;
        if (request == EXPORT_SLOT) doExport(uri, slot);
        else if (request == IMPORT_SLOT) doImport(uri, slot);
    }

    private void showError(String text) {
        new AlertDialog.Builder(this).setTitle(InstallService.title(game)).setMessage(text).setPositiveButton(R.string.ok, null).show();
    }

    /** Dialogs and the edge swipe can bring the bars back; hide them again on return. (AI-assisted) */
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) UiKit.immersive(this);
    }
}
