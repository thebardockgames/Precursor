package com.opengoalandroid.jak1;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/** Pointer ownership and analog state, independent of Android for regression tests. (AI-assisted) */
final class TouchControlsState {
    // Bits 0..14 match SDL_GamepadButton. Bits 15/16 are virtual L2/R2 axes.
    static final int CROSS = 0, CIRCLE = 1, SQUARE = 2, TRIANGLE = 3;
    static final int SELECT = 4, START = 6, L3 = 7, R3 = 8, L1 = 9, R1 = 10;
    static final int UP = 11, DOWN = 12, LEFT = 13, RIGHT = 14, L2 = 15, R2 = 16;
    static final int LEFT_STICK = -1, RIGHT_STICK = -2;

    static final class Control {
        final int code;
        final String label;
        final float x, y, radius;
        Control(int code, String label, float x, float y, float radius) {
            this.code = code;
            this.label = label;
            this.x = x;
            this.y = y;
            this.radius = radius;
        }
        boolean contains(float px, float py, float margin) {
            float dx = px - x, dy = py - y;
            return dx * dx + dy * dy <= radius * radius * margin * margin;
        }
    }

    private static final class Touch {
        final Control control;
        float x, y;
        Touch(Control control, float x, float y) {
            this.control = control;
            this.x = x;
            this.y = y;
        }
    }

    final ArrayList<Control> controls = new ArrayList<>();
    private final Map<Integer, Touch> touches = new HashMap<>();
    final float[] axes = new float[4];
    int buttons;

    TouchControlsState() {
        add(LEFT_STICK, "MOVER", 110, 330, 67);
        add(RIGHT_STICK, "CÁMARA", 715, 330, 51);
        add(CROSS, "×", 905, 380, 27);
        add(CIRCLE, "○", 955, 330, 27);
        add(SQUARE, "□", 855, 330, 27);
        add(TRIANGLE, "△", 905, 280, 27);
        add(UP, "↑", 265, 295, 22);
        add(DOWN, "↓", 265, 379, 22);
        add(LEFT, "←", 223, 337, 22);
        add(RIGHT, "→", 307, 337, 22);
        add(L1, "L1", 55, 65, 28);
        add(L2, "L2", 130, 65, 28);
        add(R1, "R1", 870, 65, 28);
        add(R2, "R2", 945, 65, 28);
        add(SELECT, "SELECT", 452, 407, 29);
        add(START, "START", 535, 407, 29);
        add(L3, "L3", 654, 407, 21);
        add(R3, "R3", 776, 407, 21);
    }

    private void add(int code, String label, float x, float y, float radius) {
        controls.add(new Control(code, label, x, y, radius));
    }

    void down(int pointerId, float x, float y) {
        touches.remove(pointerId);
        for (Control control : controls) {
            if (!control.contains(x, y, 1.1f)) continue;
            // A second finger must not steal a stick from its first owner.
            boolean owned = false;
            if (control.code < 0) {
                for (Touch touch : touches.values()) {
                    if (touch.control == control) owned = true;
                }
            }
            if (!owned) touches.put(pointerId, new Touch(control, x, y));
            break;
        }
        recalculate();
    }

    void move(int pointerId, float x, float y) {
        Touch touch = touches.get(pointerId);
        if (touch != null) {
            touch.x = x;
            touch.y = y;
        }
        recalculate();
    }

    void up(int pointerId) {
        touches.remove(pointerId);
        recalculate();
    }

    void reset() {
        touches.clear();
        recalculate();
    }

    private void recalculate() {
        buttons = 0;
        for (int i = 0; i < axes.length; i++) axes[i] = 0;
        for (Touch touch : touches.values()) {
            Control c = touch.control;
            if (c.code >= 0) {
                // Dragging off releases the button; multiple fingers act like reference counts.
                if (c.contains(touch.x, touch.y, 1.4f)) buttons |= 1 << c.code;
            } else {
                float dx = (touch.x - c.x) / (c.radius * 0.75f);
                float dy = (touch.y - c.y) / (c.radius * 0.75f);
                float length = (float) Math.sqrt(dx * dx + dy * dy);
                if (length > 0.08f) {
                    // Radial clamp preserves diagonals. The game applies its own dead zone.
                    int axis = c.code == LEFT_STICK ? 0 : 2;
                    axes[axis] = dx / Math.max(1, length);
                    axes[axis + 1] = dy / Math.max(1, length);
                }
            }
        }
    }
}
