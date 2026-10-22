package com.opengoalandroid.jak1;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Controller support for the launcher screens: which pad is connected, its button glyphs, the
 * hint bar, the focus ring, and what each button does. Confirm/back follow the button's position
 * (bottom/right), as Android reports them. (AI-assisted)
 */
final class Gamepad {
    private Gamepad() {}

    enum Action { NONE, CONFIRM, BACK, OPTIONS, ADD, PREV, NEXT, START, SELECT }
    enum Style { XBOX, PLAYSTATION, NINTENDO }

    static Action action(KeyEvent event) {
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_BUTTON_A: return Action.CONFIRM;
            case KeyEvent.KEYCODE_BUTTON_B: return Action.BACK;
            case KeyEvent.KEYCODE_BUTTON_X: return Action.OPTIONS;
            case KeyEvent.KEYCODE_BUTTON_Y: return Action.ADD;
            case KeyEvent.KEYCODE_BUTTON_L1: return Action.PREV;
            case KeyEvent.KEYCODE_BUTTON_R1: return Action.NEXT;
            case KeyEvent.KEYCODE_BUTTON_START: return Action.START;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return Action.SELECT;
            default: return Action.NONE;
        }
    }

    /** First physical gamepad, or null when only touch is available. */
    static InputDevice connected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) return device;
        }
        return null;
    }

    static Style style(InputDevice device) {
        if (device == null) return Style.XBOX;
        switch (device.getVendorId()) {
            case 0x054c: return Style.PLAYSTATION; // Sony
            case 0x057e: return Style.NINTENDO;    // Nintendo
            default: return Style.XBOX;
        }
    }

    private static String glyph(Style style, Action action) {
        switch (action) {
            case CONFIRM: return style == Style.PLAYSTATION ? "✕" : style == Style.NINTENDO ? "B" : "A";
            case BACK: return style == Style.PLAYSTATION ? "○" : style == Style.NINTENDO ? "A" : "B";
            case OPTIONS: return style == Style.PLAYSTATION ? "□" : style == Style.NINTENDO ? "Y" : "X";
            case ADD: return style == Style.PLAYSTATION ? "△" : style == Style.NINTENDO ? "X" : "Y";
            case PREV: return style == Style.PLAYSTATION ? "L1" : style == Style.NINTENDO ? "L" : "LB";
            case NEXT: return style == Style.PLAYSTATION ? "R1" : style == Style.NINTENDO ? "R" : "RB";
            case START: return style == Style.PLAYSTATION ? "OPT" : style == Style.NINTENDO ? "+" : "☰";
            case SELECT: return style == Style.PLAYSTATION ? "SHARE" : style == Style.NINTENDO ? "−" : "⧉";
            default: return "";
        }
    }

    private static int glyphColor(Action action) {
        switch (action) {
            case CONFIRM: return 0xFF6FDC8C;
            case BACK: return 0xFFFF7B6B;
            case OPTIONS: return 0xFF6AA8FF;
            case ADD: return 0xFFFFD36B;
            default: return 0xFFC6D2DA;
        }
    }

    /**
     * A row of "[glyph] label" hints, or null when no gamepad is connected (touch only).
     * Pairs are {Action, label}; an empty label shows the glyph alone (e.g. LB next to "RB Cambiar").
     */
    static View hintBar(Context context, Object... pairs) {
        InputDevice pad = connected();
        if (pad == null) return null;
        Style style = style(pad);
        float density = context.getResources().getDisplayMetrics().density;
        LinearLayout bar = new LinearLayout(context);
        bar.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        bar.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        for (int i = 0; i + 1 < pairs.length; i += 2) {
            Action action = (Action) pairs[i];
            String label = (String) pairs[i + 1];
            TextView chip = new TextView(context);
            chip.setText(glyph(style, action));
            chip.setTextSize(11);
            chip.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
            chip.setTextColor(0xFF0B1116);
            chip.setGravity(Gravity.CENTER);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(glyphColor(action));
            boolean round = action == Action.CONFIRM || action == Action.BACK || action == Action.OPTIONS || action == Action.ADD;
            bg.setCornerRadius(round ? 99 * density : 5 * density);
            chip.setBackground(bg);
            int h = Math.round(22 * density);
            chip.setMinWidth(h);
            chip.setPadding(round ? 0 : Math.round(6 * density), 0, round ? 0 : Math.round(6 * density), 0);
            LinearLayout.LayoutParams chipParams = new LinearLayout.LayoutParams(-2, h);
            chipParams.leftMargin = Math.round((i == 0 ? 0 : 16) * density);
            bar.addView(chip, chipParams);
            if (!label.isEmpty()) {
                TextView text = new TextView(context);
                text.setText(label);
                text.setTextSize(13);
                text.setTextColor(UiKit.MUTED);
                LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-2, -2);
                textParams.leftMargin = Math.round(6 * density);
                bar.addView(text, textParams);
            }
        }
        return bar;
    }

    /**
     * Makes a view reachable with the d-pad and gives it the green focus ring and a slight
     * scale-up, so the selection is obvious from the couch.
     */
    static <T extends View> T focusable(T view, float radiusDp) {
        float density = view.getResources().getDisplayMetrics().density;
        view.setFocusable(true);
        view.setClickable(true);
        view.setDefaultFocusHighlightEnabled(false);
        GradientDrawable ring = new GradientDrawable();
        ring.setColor(Color.TRANSPARENT);
        ring.setCornerRadius(radiusDp * density);
        // Buttons can be green themselves, so their ring is white; tiles and cards use the accent.
        ring.setStroke(Math.round(3 * density), view instanceof android.widget.Button ? 0xFFFFFFFF : UiKit.ACCENT);
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[]{android.R.attr.state_focused}, ring);
        states.addState(new int[]{}, new GradientDrawable());
        view.setForeground(states);
        // Full-width buttons would be clipped by their parent if scaled; they rely on the ring.
        boolean grow = !(view instanceof android.widget.Button);
        View.OnFocusChangeListener previous = view.getOnFocusChangeListener();
        view.setOnFocusChangeListener((v, has) -> {
            if (grow) v.animate().scaleX(has ? 1.05f : 1f).scaleY(has ? 1.05f : 1f).setDuration(120).start();
            if (previous != null) previous.onFocusChange(v, has);
        });
        return view;
    }

    /** Runs extra work on focus changes without replacing the ring/scale behaviour. */
    static void alsoOnFocus(View view, View.OnFocusChangeListener extra) {
        View.OnFocusChangeListener previous = view.getOnFocusChangeListener();
        view.setOnFocusChangeListener((v, has) -> {
            if (previous != null) previous.onFocusChange(v, has);
            extra.onFocusChange(v, has);
        });
    }
}
