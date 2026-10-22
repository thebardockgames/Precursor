package com.opengoalandroid.jak1;

import android.app.*;
import android.content.*;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.format.DateUtils;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.List;
import java.util.Locale;

/**
 * Precursor's Settings, built like a game's options (GameOptionsActivity): language, updates,
 * backups of saves and settings, the installation log and "about". Landscape shows the sections
 * as a side column, portrait as chips under the Orbe; LB/RB switch sections, B goes back.
 * (AI-assisted)
 */
public final class SettingsActivity extends Activity {
    /** Result for the launcher: show the welcome again. */
    static final int RESULT_WELCOME = RESULT_FIRST_USER + 1;
    private static final int EXPORT_LOG = 40;
    private static final int[] TABS = {R.string.tab_language, R.string.tab_updates, R.string.tab_backups,
            R.string.tab_log, R.string.tab_about};
    private static final int TAB_LANGUAGE = 0, TAB_UPDATES = 1, TAB_BACKUPS = 2, TAB_LOG = 3;

    private int tab;
    private LinearLayout content;
    private final TextView[] tabViews = new TextView[TABS.length];

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(Language.wrap(base)); }

    @Override public void finish() {
        super.finish();
        overridePendingTransition(R.anim.shelf_enter, R.anim.options_exit);
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (saved != null) tab = saved.getInt("tab", 0);
        setContentView(build());
        UiKit.immersive(this);
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putInt("tab", tab);
        super.onSaveInstanceState(state);
    }

    @Override protected void onResume() {
        super.onResume();
        if (content != null) showTab(tab);
        UpdateFlow.resume(this);
    }

    private boolean wide() { return getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE; }
    private int dp(int v) { return UiKit.dp(this, v); }
    private TextView label(String text, int size, int color, boolean bold) { return UiKit.label(this, text, size, color, bold); }

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
            TextView sub = label("PRECURSOR", 11, UiKit.MUTED, true);
            sub.setLetterSpacing(.12f);
            UiKit.margin(this, side, sub, 14);
            TextView title = label(getString(R.string.settings), 22, UiKit.TEXT, true);
            title.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
            side.addView(title);
            for (int i = 0; i < TABS.length; i++) UiKit.margin(this, side, tabView(i, false), i == 0 ? 18 : 8);
            ScrollView sideScroll = new ScrollView(this);
            sideScroll.setVerticalScrollBarEnabled(false);
            sideScroll.addView(side);
            sideScroll.setBackgroundColor(0xFF0C1319);
            row.addView(sideScroll, new LinearLayout.LayoutParams(dp(250), -1));
            content.setPadding(dp(24), dp(24), dp(24), dp(48));
            row.addView(scroll, new LinearLayout.LayoutParams(0, -1, 1));
            frame.addView(row, new FrameLayout.LayoutParams(-1, -1));
        } else {
            LinearLayout col = UiKit.column(this);
            FrameLayout head = new FrameLayout(this);
            GradientDrawable glow = new GradientDrawable();
            glow.setGradientType(GradientDrawable.RADIAL_GRADIENT);
            glow.setColors(new int[]{0xFF1B4550, UiKit.BACKGROUND});
            glow.setGradientCenter(.85f, .4f);
            glow.setGradientRadius(dp(260));
            head.setBackground(glow);
            OrbView mark = new OrbView(this, OrbView.Mode.MARK);
            mark.setAlpha(.35f);
            FrameLayout.LayoutParams markParams = new FrameLayout.LayoutParams(dp(170), dp(170), Gravity.END | Gravity.CENTER_VERTICAL);
            markParams.rightMargin = -dp(30);
            head.addView(mark, markParams);
            LinearLayout title = new LinearLayout(this);
            title.setGravity(Gravity.CENTER_VERTICAL);
            title.setPadding(dp(18), 0, dp(18), dp(12));
            title.addView(backButton());
            LinearLayout names = UiKit.column(this);
            names.setPadding(dp(12), 0, 0, 0);
            TextView sub = label("PRECURSOR", 11, UiKit.MUTED, true);
            sub.setLetterSpacing(.12f);
            names.addView(sub);
            TextView name = label(getString(R.string.settings), 24, UiKit.TEXT, true);
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
        if (index == TAB_LANGUAGE) buildLanguage();
        else if (index == TAB_UPDATES) buildUpdates();
        else if (index == TAB_BACKUPS) buildBackups();
        else if (index == TAB_LOG) buildLog();
        else buildAbout();
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

    // ---------------------------------------------------------------- language

    /** The phone's language (the default) or one of Precursor's; the screens change right away. */
    private void buildLanguage() {
        heading(getString(R.string.tab_language), getString(R.string.language_note));
        String chosen = Language.chosen(this);
        choiceCard(getString(R.string.language_system, Language.NAMES[Language.index(Language.system())]), null,
                chosen.isEmpty(), true, () -> setLanguage(""));
        for (int i = 0; i < Language.SUPPORTED.length; i++) {
            String code = Language.SUPPORTED[i];
            choiceCard(Language.NAMES[i], null, code.equals(chosen), true, () -> setLanguage(code));
        }
    }

    private void setLanguage(String code) {
        if (code.equals(Language.chosen(this))) return;
        Language.choose(this, code);
        recreate();
    }

    // ---------------------------------------------------------------- updates

    private void buildUpdates() {
        heading(getString(R.string.tab_updates), getString(R.string.current_version, BuildConfig.VERSION_NAME));
        UiKit.margin(this, content, wrapLeft(smallButton(getString(R.string.update_check_now), UiKit.ACCENT,
                v -> UpdateFlow.check(this, true))), 12);
        subheading(getString(R.string.update_channel_title));
        boolean alpha = Updater.alphaChannel(this);
        choiceCard(getString(R.string.update_channel_stable), capitalized(getString(R.string.update_stable_desc)), !alpha, true,
                () -> { Updater.setAlphaChannel(this, false); showTab(TAB_UPDATES); });
        choiceCard(getString(R.string.update_channel_alpha), capitalized(getString(R.string.update_alpha_desc)), alpha, true,
                () -> { Updater.setAlphaChannel(this, true); showTab(TAB_UPDATES); });
        subheading(getString(R.string.other_title));
        boolean auto = Updater.autoCheck(this);
        choiceCard(getString(R.string.update_auto_label), getString(R.string.update_auto_desc), auto, true,
                () -> { Updater.setAutoCheck(this, !auto); showTab(TAB_UPDATES); });
    }

    private static String capitalized(String text) {
        return text.isEmpty() ? text : text.substring(0, 1).toUpperCase(Locale.getDefault()) + text.substring(1);
    }

    // ---------------------------------------------------------------- backups

    private void buildBackups() {
        heading(getString(R.string.tab_backups), getString(R.string.backups_hint));
        UiKit.margin(this, content, wrapLeft(smallButton(getString(R.string.backups_create), UiKit.ACCENT, v -> new Thread(() -> {
            try {
                File made = Backups.snapshot(this, "manual");
                runOnUiThread(() -> {
                    Toast.makeText(this, made == null ? R.string.backups_nothing : R.string.backups_created, Toast.LENGTH_SHORT).show();
                    showTab(TAB_BACKUPS);
                });
            } catch (IOException error) {
                runOnUiThread(() -> showError(String.valueOf(error.getMessage())));
            }
        }, "Backup").start())), 12);
        List<Backups.Backup> backups = Backups.list(this);
        if (backups.isEmpty()) UiKit.margin(this, content, label(getString(R.string.backups_none), 14, UiKit.MUTED, false), 16);
        for (Backups.Backup backup : backups) {
            String when = DateUtils.formatDateTime(this, backup.time, DateUtils.FORMAT_SHOW_DATE | DateUtils.FORMAT_SHOW_TIME
                    | DateUtils.FORMAT_ABBREV_MONTH);
            LinearLayout card = UiKit.column(this);
            card.setBackground(UiKit.shape(this, UiKit.PANEL, 16));
            card.setPadding(dp(16), dp(14), dp(16), dp(14));
            card.addView(label(when, 15, UiKit.TEXT, true));
            UiKit.margin(this, card, label(backupReason(backup.reason), 13, UiKit.MUTED, false), 4);
            UiKit.margin(this, card, wrapLeft(smallButton(getString(R.string.restore), UiKit.ACCENT,
                    v -> confirmRestore(backup, when))), 12);
            UiKit.margin(this, content, card, 12);
        }
    }

    private String backupReason(String reason) {
        switch (reason) {
            case Backups.UPDATE: return getString(R.string.backup_reason_update);
            case Backups.PREPARE: return getString(R.string.backup_reason_prepare);
            case Backups.REMOVE: return getString(R.string.backup_reason_remove);
            case Backups.RESTORE: return getString(R.string.backup_reason_restore);
            default: return getString(R.string.backup_reason_manual);
        }
    }

    private void confirmRestore(Backups.Backup backup, String when) {
        new AlertDialog.Builder(this).setTitle(R.string.restore_title).setMessage(getString(R.string.restore_body, when))
                .setNegativeButton(R.string.cancel, null)
                .setPositiveButton(R.string.restore, (d, w) -> {
                    if (UpdateFlow.gameRunning(this)) { showError(getString(R.string.restore_close_game)); return; }
                    new Thread(() -> {
                        try {
                            Backups.restore(this, backup);
                            runOnUiThread(() -> { Toast.makeText(this, R.string.restored, Toast.LENGTH_SHORT).show(); showTab(TAB_BACKUPS); });
                        } catch (IOException error) {
                            runOnUiThread(() -> showError(String.valueOf(error.getMessage())));
                        }
                    }, "Restore backup").start();
                }).show();
    }

    // ---------------------------------------------------------------- log

    private void buildLog() {
        heading(getString(R.string.tab_log), getString(R.string.log_hint));
        UiKit.margin(this, content, wrapLeft(smallButton(getString(R.string.save_log), UiKit.ACCENT, v -> exportLog())), 12);
        TextView text = label(readLogTail(24000), 11, UiKit.TEXT, false);
        text.setTypeface(Typeface.MONOSPACE);
        text.setTextIsSelectable(true);
        text.setPadding(dp(14), dp(12), dp(14), dp(12));
        text.setBackground(UiKit.shape(this, 0xFF0C1319, 14));
        UiKit.margin(this, content, text, 12);
    }

    private String readLogTail(int limit) {
        try (RandomAccessFile file = new RandomAccessFile(LauncherStore.log(this), "r")) {
            long length = file.length();
            byte[] bytes = new byte[(int) Math.min(length, limit)];
            file.seek(length - bytes.length);
            file.readFully(bytes);
            return bytes.length == 0 ? getString(R.string.log_empty) : new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
        } catch (IOException error) { return getString(R.string.log_missing); }
    }

    private void exportLog() {
        if (!LauncherStore.log(this).isFile()) { showError(getString(R.string.log_missing)); return; }
        Intent export = new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("text/plain")
                .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, "precursor-install.log");
        try { startActivityForResult(export, EXPORT_LOG); }
        catch (ActivityNotFoundException error) { showError(getString(R.string.no_file_picker)); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != EXPORT_LOG || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        new Thread(() -> {
            try (InputStream input = new FileInputStream(LauncherStore.log(this));
                 OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                if (output == null) throw new IOException(getString(R.string.cannot_open_destination));
                InstallFiles.copy(input, output);
                runOnUiThread(() -> Toast.makeText(this, R.string.log_saved, Toast.LENGTH_SHORT).show());
            } catch (Exception error) { runOnUiThread(() -> showError(String.valueOf(error.getMessage()))); }
        }, "Export log").start();
    }

    // ---------------------------------------------------------------- about

    private void buildAbout() {
        heading(getString(R.string.about_title), null);
        TextView body = label(getString(R.string.about_body, BuildConfig.VERSION_NAME, BuildConfig.GIT_COMMIT), 14, 0xFFC3CFD8, false);
        body.setLineSpacing(dp(2), 1f);
        LinearLayout card = UiKit.column(this);
        card.setBackground(UiKit.shape(this, UiKit.PANEL, 16));
        card.setPadding(dp(16), dp(14), dp(16), dp(14));
        card.addView(body);
        UiKit.margin(this, content, card, 12);
        UiKit.margin(this, content, wrapLeft(smallButton(getString(R.string.settings_welcome), UiKit.ACCENT, v -> {
            setResult(RESULT_WELCOME);
            finish();
        })), 12);
    }

    // ---------------------------------------------------------------- shared

    private void heading(String title, String hint) {
        TextView t = label(title, 22, UiKit.TEXT, true);
        t.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        content.addView(t);
        if (hint != null) UiKit.margin(this, content, label(hint, 13, UiKit.MUTED, false), 4);
    }

    private void subheading(String title) {
        UiKit.margin(this, content, label(title, 16, UiKit.TEXT, true), 22);
    }

    /** A selectable card like the game options' choices. */
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

    private Button smallButton(String text, int color, View.OnClickListener click) {
        Button button = UiKit.button(this, text, 0x1FFFFFFF, color);
        button.setBackground(UiKit.shape(this, 0x1FFFFFFF, 99));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setPadding(dp(16), dp(8), dp(16), dp(8));
        button.setOnClickListener(click);
        return Gamepad.focusable(button, 99);
    }

    /** Keeps a button its natural width inside a full-width column. */
    private View wrapLeft(View view) {
        LinearLayout row = new LinearLayout(this);
        row.addView(view, new LinearLayout.LayoutParams(-2, -2));
        return row;
    }

    private void showError(String text) {
        new AlertDialog.Builder(this).setTitle(R.string.app_name).setMessage(text).setPositiveButton(R.string.ok, null).show();
    }

    /** Dialogs and the edge swipe can bring the bars back; hide them again on return. */
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) UiKit.immersive(this);
    }
}
