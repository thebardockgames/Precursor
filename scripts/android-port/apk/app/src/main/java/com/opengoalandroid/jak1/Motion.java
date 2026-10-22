package com.opengoalandroid.jak1;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ObjectAnimator;
import android.animation.ValueAnimator;
import android.annotation.SuppressLint;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.AccelerateInterpolator;
import android.view.animation.DecelerateInterpolator;
import android.widget.Button;

/**
 * Small, shared motion helpers for the launcher and the game overlays. Everything goes through
 * Android's animators, so the system "remove animations" setting (animator duration scale 0)
 * turns it all into instant changes. (AI-assisted)
 */
final class Motion {
    private Motion() {}

    static final DecelerateInterpolator OUT = new DecelerateInterpolator(2f);
    static final AccelerateInterpolator IN = new AccelerateInterpolator(1.5f);

    /** False when the user turned animations off in Android's settings. */
    static boolean enabled() {
        return ValueAnimator.areAnimatorsEnabled();
    }

    /** Waits only when animations are on (used between an animation and the next screen). */
    static long delay(long ms) {
        return enabled() ? ms : 0;
    }

    /** Children rise 16 dp and fade in one after another. */
    static void stagger(ViewGroup group, long stepMs, long firstDelayMs) {
        float rise = 16 * group.getResources().getDisplayMetrics().density;
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            child.setAlpha(0f);
            child.setTranslationY(rise);
            child.animate().alpha(1f).translationY(0f).setDuration(320)
                    .setStartDelay(firstDelayMs + i * stepMs).setInterpolator(OUT).start();
        }
    }

    /** Children slide in sideways (dir > 0: from the right), one after another. */
    static void slideIn(ViewGroup group, int dir, long stepMs) {
        float shift = 20 * group.getResources().getDisplayMetrics().density * Math.signum(dir);
        for (int i = 0; i < group.getChildCount(); i++) {
            View child = group.getChildAt(i);
            child.setAlpha(0f);
            child.setTranslationX(shift);
            child.animate().alpha(1f).translationX(0f).setDuration(260)
                    .setStartDelay(i * stepMs).setInterpolator(OUT).start();
        }
    }

    /** Buttons and tiles sink a little while pressed. Clicks still go through. */
    @SuppressLint("ClickableViewAccessibility")
    static <T extends View> T pressable(T view) {
        view.setOnTouchListener((v, event) -> {
            float rest = v.isFocused() && !(v instanceof Button) ? 1.05f : 1f;
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    v.animate().scaleX(rest * .95f).scaleY(rest * .95f).setDuration(90).setStartDelay(0).start();
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    v.animate().scaleX(rest).scaleY(rest).setDuration(140).setStartDelay(0).start();
                    break;
                default:
                    break;
            }
            return false;
        });
        return view;
    }

    /**
     * A gentle, endless "press me" beat. It breathes inward (1 to 0.97) so it never draws past its
     * own bounds, where a full-width button would be cut by its row. Stops off screen.
     */
    static void beat(View view) {
        ObjectAnimator beat = ObjectAnimator.ofPropertyValuesHolder(view,
                android.animation.PropertyValuesHolder.ofFloat(View.SCALE_X, 1f, .97f),
                android.animation.PropertyValuesHolder.ofFloat(View.SCALE_Y, 1f, .94f));
        beat.setDuration(1000);
        beat.setRepeatCount(ValueAnimator.INFINITE);
        beat.setRepeatMode(ValueAnimator.REVERSE);
        beat.setStartDelay(900);
        cancelOnDetach(view, beat);
        beat.start();
    }

    /** One quick side-to-side shake (errors). */
    static void shake(View view) {
        float x = 10 * view.getResources().getDisplayMetrics().density;
        ObjectAnimator shake = ObjectAnimator.ofFloat(view, View.TRANSLATION_X, 0, -x, x, -x, x, 0);
        shake.setDuration(320);
        shake.start();
    }

    /** Endless alpha pulse (the current installation stage). */
    static void pulse(View view) {
        ObjectAnimator pulse = ObjectAnimator.ofFloat(view, View.ALPHA, 1f, .45f);
        pulse.setDuration(600);
        pulse.setRepeatCount(ValueAnimator.INFINITE);
        pulse.setRepeatMode(ValueAnimator.REVERSE);
        cancelOnDetach(view, pulse);
        pulse.start();
    }

    static void cancelOnDetach(View view, Animator animator) {
        view.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener() {
            @Override public void onViewAttachedToWindow(View v) { }
            @Override public void onViewDetachedFromWindow(View v) { animator.cancel(); }
        });
    }

    /** Runs {@code then} after the animator ends (or at once if it never starts). */
    static void after(Animator animator, Runnable then) {
        animator.addListener(new AnimatorListenerAdapter() {
            private boolean done;
            @Override public void onAnimationEnd(Animator a) { if (!done) { done = true; then.run(); } }
            @Override public void onAnimationCancel(Animator a) { onAnimationEnd(a); }
        });
    }
}
