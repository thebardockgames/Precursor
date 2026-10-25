package com.opengoalandroid.jak1;

import android.app.Activity;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Shared colors and view builders for the launcher's screens. (AI-assisted) */
final class UiKit {
    private UiKit() {}

    static final int BACKGROUND = 0xff101820, PANEL = 0xff1b2833;
    static final int TEXT = 0xfff1f5ee, MUTED = 0xffa9b8c4, ACCENT = 0xff4ee878;

    /** A save's level in Precursor's language: "Sandover Village", "Level #12" or "New game". (AI-assisted) */
    static String levelName(android.content.Context context, String game, int levelIndex) {
        int level = GameSaves.jak1Level(game, levelIndex);
        if (level >= 0) return context.getResources().getStringArray(R.array.jak1_levels)[level];
        return levelIndex > 0 ? context.getString(R.string.level_number, levelIndex) : context.getString(R.string.new_game);
    }

    /**
     * Full screen like the game: the status and navigation bars are hidden (a swipe from the edge
     * shows them for a moment) and the screen, backgrounds included, reaches into the camera
     * cutout. Buttons and text are kept clear of it with clearOfCutout. Call from onCreate and
     * again when the window regains focus. (AI-assisted)
     */
    static void immersive(Activity activity) {
        android.view.Window window = activity.getWindow();
        window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(BACKGROUND));
        android.view.WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.layoutInDisplayCutoutMode = android.os.Build.VERSION.SDK_INT >= 30
                ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        window.setAttributes(attributes);
        View content = activity.findViewById(android.R.id.content);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(android.view.WindowInsets.Type.systemBars());
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        content.requestApplyInsets();
    }

    static final int LEFT = 1, TOP = 2, RIGHT = 4, BOTTOM = 8, ALL = LEFT | TOP | RIGHT | BOTTOM;

    /**
     * Adds the camera cutout on the given edges to the view's own padding, following rotation (the
     * cutout changes sides). For the layer holding buttons and text, so the background under it
     * can still reach the edge of the screen. Call after the view's own padding is set. (AI-assisted)
     */
    static void clearOfCutout(View view, int edges) {
        int left = view.getPaddingLeft(), top = view.getPaddingTop();
        int right = view.getPaddingRight(), bottom = view.getPaddingBottom();
        view.setOnApplyWindowInsetsListener((v, insets) -> {
            int l = 0, t = 0, r = 0, b = 0;
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets cut = insets.getInsets(android.view.WindowInsets.Type.displayCutout());
                l = cut.left; t = cut.top; r = cut.right; b = cut.bottom;
            } else if (insets.getDisplayCutout() != null) {
                android.view.DisplayCutout cut = insets.getDisplayCutout();
                l = cut.getSafeInsetLeft(); t = cut.getSafeInsetTop(); r = cut.getSafeInsetRight(); b = cut.getSafeInsetBottom();
            }
            v.setPadding(left + ((edges & LEFT) != 0 ? l : 0), top + ((edges & TOP) != 0 ? t : 0),
                    right + ((edges & RIGHT) != 0 ? r : 0), bottom + ((edges & BOTTOM) != 0 ? b : 0));
            return insets;
        });
        view.requestApplyInsets();
    }

    static int dp(Activity activity, int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }

    static LinearLayout column(Activity activity) {
        LinearLayout view = new LinearLayout(activity);
        view.setOrientation(LinearLayout.VERTICAL);
        return view;
    }

    static GradientDrawable shape(Activity activity, int color, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(activity, radius));
        return background;
    }

    static TextView label(Activity activity, String text, int size, int color, boolean bold) {
        TextView view = new TextView(activity);
        view.setText(text);
        view.setTextSize(size);
        view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    static Button button(Activity activity, String title, int color, int text) {
        Button button = new Button(activity);
        button.setText(title);
        button.setAllCaps(false);
        button.setTextColor(text);
        button.setTextSize(14);
        button.setBackground(shape(activity, color, 12));
        button.setPadding(dp(activity, 10), dp(activity, 8), dp(activity, 10), dp(activity, 8));
        button.setMinHeight(dp(activity, 48));
        return button;
    }

    static void margin(Activity activity, LinearLayout parent, View view, int top) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(activity, top);
        parent.addView(view, params);
    }
}
