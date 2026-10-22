package com.opengoalandroid.jak1;

import android.app.Activity;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The in-game menu (Back, the touch MENÚ button or Select + Start): the game dims, a Precursor
 * panel rises with the Orbe and two choices, "Seguir jugando" selected first so a stray tap
 * never quits. It sits above SDL's surface, so it manages its own selection instead of Android
 * focus, which stays with the game. (AI-assisted)
 */
final class GameMenuView extends FrameLayout {
    interface Listener { void onMenuShown(boolean shown); }

    private final View scrim;
    private final LinearLayout panel;
    private final Button keep, leave;
    private final Runnable onLeave;
    private final Listener listener;
    private int selected;
    private boolean open;

    GameMenuView(Activity activity, String title, String leaveLabel, Runnable onLeave, Listener listener) {
        super(activity);
        this.onLeave = onLeave;
        this.listener = listener;
        setVisibility(GONE);
        setClipChildren(false);

        scrim = new View(activity);
        scrim.setBackgroundColor(0x9E060A0E);
        scrim.setOnClickListener(v -> close());
        addView(scrim, new LayoutParams(-1, -1));

        panel = UiKit.column(activity);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[]{0xFF213241, 0xFF16222C});
        bg.setCornerRadius(dp(24));
        bg.setStroke(dp(1), 0x59E2B25A);
        panel.setBackground(bg);
        panel.setElevation(dp(24));
        panel.setPadding(dp(24), dp(22), dp(24), dp(18));
        panel.setClickable(true); // taps on the panel do not close it

        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(new OrbView(activity, OrbView.Mode.STATIC), new LinearLayout.LayoutParams(dp(52), dp(52)));
        LinearLayout names = UiKit.column(activity);
        TextView name = UiKit.label(activity, title, 22, UiKit.TEXT, true);
        name.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        names.addView(name);
        TextView brand = UiKit.label(activity, "PRECURSOR · POWERED BY OPENGOAL", 11, UiKit.MUTED, false);
        brand.setLetterSpacing(.08f);
        names.addView(brand);
        LinearLayout.LayoutParams namesParams = new LinearLayout.LayoutParams(-2, -2);
        namesParams.leftMargin = dp(14);
        top.addView(names, namesParams);
        panel.addView(top);

        TextView warn = UiKit.label(activity, "Lo que no hayas guardado en el juego se perderá.", 14, UiKit.MUTED, false);
        LinearLayout.LayoutParams warnParams = new LinearLayout.LayoutParams(-1, -2);
        warnParams.topMargin = dp(14);
        panel.addView(warn, warnParams);

        keep = choice(activity, "Seguir jugando", true);
        keep.setOnClickListener(v -> close());
        leave = choice(activity, leaveLabel, false);
        leave.setOnClickListener(v -> { if (open) this.onLeave.run(); });
        panel.addView(keep, choiceParams(16));
        panel.addView(leave, choiceParams(10));

        View hints = Gamepad.hintBar(activity, Gamepad.Action.CONFIRM, "Elegir", Gamepad.Action.BACK, "Seguir jugando");
        if (hints != null) {
            LinearLayout.LayoutParams hintParams = new LinearLayout.LayoutParams(-1, -2);
            hintParams.topMargin = dp(14);
            panel.addView(hints, hintParams);
        }

        int width = Math.min(dp(440), Math.round(activity.getResources().getDisplayMetrics().widthPixels * .46f));
        addView(panel, new LayoutParams(width, -2, Gravity.CENTER));
        select(0);
    }

    boolean isOpen() { return open; }

    void open() {
        if (open) return;
        open = true;
        listener.onMenuShown(true);
        select(0);
        setVisibility(VISIBLE);
        scrim.setAlpha(0f);
        scrim.animate().alpha(1f).setDuration(180).setStartDelay(0).setInterpolator(Motion.OUT).withEndAction(null).start();
        panel.setAlpha(0f);
        panel.setTranslationY(dp(24));
        panel.setScaleX(.96f);
        panel.setScaleY(.96f);
        panel.animate().alpha(1f).translationY(0).scaleX(1f).scaleY(1f).setDuration(260).setStartDelay(0)
                .setInterpolator(Motion.OUT).withEndAction(null).start();
        int i = 0;
        for (Button button : new Button[]{keep, leave}) {
            button.setAlpha(0f);
            button.setTranslationY(dp(8));
            button.animate().alpha(1f).translationY(0).setDuration(240).setStartDelay(60 + 40L * i++)
                    .setInterpolator(Motion.OUT).start();
        }
    }

    void close() {
        if (!open) return;
        open = false;
        scrim.animate().alpha(0f).setDuration(160).setStartDelay(0).setInterpolator(Motion.IN).start();
        panel.animate().alpha(0f).translationY(dp(16)).scaleX(.96f).scaleY(.96f).setDuration(160).setStartDelay(0)
                .setInterpolator(Motion.IN).withEndAction(() -> { if (!open) setVisibility(GONE); }).start();
        listener.onMenuShown(false);
    }

    /** Controller and keyboard input while the menu is open; everything is kept from the game. */
    boolean handleKey(KeyEvent event) {
        int code = event.getKeyCode();
        boolean up = event.getAction() == KeyEvent.ACTION_UP;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: case KeyEvent.KEYCODE_DPAD_DOWN:
                if (!up) select(1 - selected);
                return true;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
                if (up) (selected == 0 ? keep : leave).performClick();
                return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE:
                if (up) close();
                return true;
            case KeyEvent.KEYCODE_VOLUME_UP: case KeyEvent.KEYCODE_VOLUME_DOWN: case KeyEvent.KEYCODE_VOLUME_MUTE:
                return false; // the system still changes the volume
            default:
                return true;
        }
    }

    private void select(int index) {
        selected = index;
        style(keep, true, index == 0);
        style(leave, false, index == 1);
    }

    private Button choice(Activity activity, String text, boolean primary) {
        Button button = UiKit.button(activity, text, 0, primary ? 0xFF0B1116 : UiKit.TEXT);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setTextSize(16);
        button.setFocusable(false);
        button.setMinHeight(dp(52));
        return Motion.pressable(button);
    }

    private void style(Button button, boolean primary, boolean selected) {
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(16));
        shape.setColor(primary ? (selected ? UiKit.ACCENT : 0xFF2E8F4D) : (selected ? 0x29FFFFFF : 0x14FFFFFF));
        if (selected) shape.setStroke(dp(2), 0xFFFFFFFF);
        button.setBackground(shape);
        button.setTextColor(primary ? 0xFF0B1116 : UiKit.TEXT);
    }

    private LinearLayout.LayoutParams choiceParams(int topDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(topDp);
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
