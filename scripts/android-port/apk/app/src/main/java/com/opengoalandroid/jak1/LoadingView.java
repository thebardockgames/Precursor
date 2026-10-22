package com.opengoalandroid.jak1;

import android.app.Activity;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.function.LongSupplier;

/**
 * Covers the seconds between "Jugar" and the game's first real frame: the Orbe's brass arc spins,
 * the game's name and a status line sit under it, and the whole screen fades out once the game
 * has drawn real frames (counted natively, not just the empty window). (AI-assisted)
 */
final class LoadingView extends FrameLayout {
    /** Real game frames to wait for, so a single black frame does not end the screen early. */
    private static final long FRAMES_TO_SHOW = 20;
    private static final long SLOW_MS = 20000;

    private final LongSupplier gameFrames;
    private final TextView status;
    private final long started = SystemClock.elapsedRealtime();
    private boolean leaving, slowShown;

    private final Runnable check = new Runnable() {
        @Override public void run() {
            if (leaving) return;
            if (gameFrames.getAsLong() >= FRAMES_TO_SHOW) { leave(); return; }
            if (!slowShown && SystemClock.elapsedRealtime() - started > SLOW_MS) {
                slowShown = true;
                status.animate().alpha(0f).setDuration(200).withEndAction(() -> {
                    status.setText("Iniciando el motor… La primera vez tarda un poco más.");
                    status.animate().alpha(1f).setDuration(200).start();
                }).start();
            }
            postDelayed(this, 150);
        }
    };

    LoadingView(Activity activity, String title, LongSupplier gameFrames) {
        super(activity);
        this.gameFrames = gameFrames;
        setClickable(true); // nothing reaches the touch controls underneath while loading
        GradientDrawable bg = new GradientDrawable();
        bg.setGradientType(GradientDrawable.RADIAL_GRADIENT);
        bg.setColors(new int[]{0xFF14222C, 0xFF070B0F});
        bg.setGradientRadius(activity.getResources().getDisplayMetrics().widthPixels * .55f);
        setBackground(bg);

        LinearLayout stack = UiKit.column(activity);
        stack.setGravity(Gravity.CENTER_HORIZONTAL);
        OrbView orb = new OrbView(activity, OrbView.Mode.LOADING);
        stack.addView(orb, new LinearLayout.LayoutParams(dp(150), dp(150)));
        orb.setAlpha(0f);
        orb.setScaleX(.6f);
        orb.setScaleY(.6f);
        orb.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(600).setInterpolator(Motion.OUT).start();

        TextView name = UiKit.label(activity, title, 30, UiKit.TEXT, true);
        name.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        LinearLayout.LayoutParams nameParams = new LinearLayout.LayoutParams(-2, -2);
        nameParams.topMargin = dp(18);
        stack.addView(name, nameParams);

        GlowBar bar = new GlowBar(activity, true);
        LinearLayout.LayoutParams barParams = new LinearLayout.LayoutParams(dp(220), dp(4));
        barParams.topMargin = dp(16);
        stack.addView(bar, barParams);

        status = UiKit.label(activity, "Iniciando el motor…", 13, UiKit.MUTED, false);
        status.setTypeface(Typeface.MONOSPACE);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(-2, -2);
        statusParams.topMargin = dp(14);
        stack.addView(status, statusParams);
        addView(stack, new LayoutParams(-2, -2, Gravity.CENTER));

        TextView credit = UiKit.label(activity, "Precursor · powered by OpenGOAL · proyecto no oficial", 11, 0x99A9B8C4, false);
        LayoutParams creditParams = new LayoutParams(-2, -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        creditParams.bottomMargin = dp(18);
        addView(credit, creditParams);
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        post(check);
    }

    @Override protected void onDetachedFromWindow() {
        removeCallbacks(check);
        super.onDetachedFromWindow();
    }

    private void leave() {
        leaving = true;
        animate().alpha(0f).setDuration(350).withEndAction(() -> {
            if (getParent() instanceof ViewGroup) ((ViewGroup) getParent()).removeView(this);
        }).start();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
