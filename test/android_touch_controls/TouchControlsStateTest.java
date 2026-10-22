package com.opengoalandroid.jak1;

/** Tests the same pointer state used by the APK without Android/JUnit. (AI-assisted) */
public final class TouchControlsStateTest {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        TouchControlsState s = new TouchControlsState();
        for (TouchControlsState.Control c : s.controls) {
            if (c.code < 0) continue;
            s.down(17, c.x, c.y);
            check(s.buttons == (1 << c.code), "Button mapping: " + c.label);
            s.up(17);
            check(s.buttons == 0, "Button release: " + c.label);
        }
        s.down(42, 110, 330);
        check(s.axes[0] == 0 && s.axes[1] == 0, "Centered stick");
        s.move(42, 210, 330);
        check(s.axes[0] == 1 && s.axes[1] == 0, "Clamp horizontal stick");
        s.down(7, 905, 380);
        s.down(93, 715, 330);
        s.move(93, 715, 230);
        s.down(16, 130, 65);
        check(s.axes[0] == 1 && s.axes[3] == -1 && s.buttons == ((1 << 0) | (1 << 15)),
              "Four fingers: movement + camera + jump + L2");
        s.up(7);
        check(s.axes[0] == 1 && s.axes[3] == -1 && s.buttons == (1 << 15),
              "Lifting one pointer preserves the others");
        s.down(50, 110, 330);
        s.move(50, 10, 330);
        check(s.axes[0] == 1, "Second finger cannot steal an owned stick");
        s.up(42);
        check(s.axes[0] == 0 && s.axes[3] == -1, "Stick release and independent camera");
        s.reset();
        check(s.buttons == 0 && s.axes[3] == 0, "Pause/cancel/hide clears everything");
        s.down(1, 110, 330);
        s.move(1, 1110, 1330);
        check(Math.abs(s.axes[0] - 0.7071068f) < 0.00001f && s.axes[0] == s.axes[1],
              "Diagonal circular normalization");
        s.reset();
        s.down(1, 905, 380);
        s.down(2, 905, 380);
        s.up(1);
        check(s.buttons == 1, "Two fingers on same button");
        s.move(2, 500, 200);
        check(s.buttons == 0, "Drag off button releases");
        s.move(2, 905, 380);
        check(s.buttons == 1, "Drag back restores");
        s.reset();
        s.down(99, 500, 200);
        s.move(99, 905, 380);
        check(s.buttons == 0, "Unowned screen touch cannot acquire a control on move");
        System.out.println("PASS: " + checks + " touch control assertions (AI-assisted)");
    }
}
