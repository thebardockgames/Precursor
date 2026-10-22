package com.opengoalandroid.jak1;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.View;
import android.view.animation.LinearInterpolator;

/**
 * A thin progress bar: the fill glides to each new value and a highlight runs along it, so a slow
 * installation still looks alive. Indeterminate mode sweeps a short brass segment instead (the
 * game's loading screen). (AI-assisted)
 */
final class GlowBar extends View {
    private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG), fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final boolean indeterminate;
    private float shown, phase;
    private ValueAnimator fillAnim, loop;

    GlowBar(Context context, boolean indeterminate) {
        super(context);
        this.indeterminate = indeterminate;
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        track.setColor(indeterminate ? 0x2EE2B25A : 0x1AFFFFFF);
        fill.setColor(indeterminate ? 0xFFE2B25A : UiKit.ACCENT);
    }

    void setProgress(int percent) {
        float target = Math.max(0, Math.min(100, percent)) / 100f;
        if (fillAnim != null) fillAnim.cancel();
        fillAnim = ValueAnimator.ofFloat(shown, target).setDuration(500);
        fillAnim.setInterpolator(Motion.OUT);
        fillAnim.addUpdateListener(a -> { shown = (float) a.getAnimatedValue(); invalidate(); });
        fillAnim.start();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        loop = ValueAnimator.ofFloat(0f, 1f).setDuration(indeterminate ? 1300 : 1600);
        loop.setRepeatCount(ValueAnimator.INFINITE);
        loop.setInterpolator(indeterminate ? Motion.OUT : new LinearInterpolator());
        loop.addUpdateListener(a -> { phase = (float) a.getAnimatedValue(); invalidate(); });
        loop.start();
    }

    @Override protected void onDetachedFromWindow() {
        if (loop != null) loop.cancel();
        if (fillAnim != null) fillAnim.cancel();
        super.onDetachedFromWindow();
    }

    @Override protected void onDraw(Canvas canvas) {
        float w = getWidth(), h = getHeight(), r = h / 2f;
        rect.set(0, 0, w, h);
        canvas.drawRoundRect(rect, r, r, track);
        if (indeterminate) {
            float seg = w * .3f, x = -seg + (w + seg) * phase;
            rect.set(Math.max(0, x), 0, Math.min(w, x + seg), h);
            if (rect.width() > 0) canvas.drawRoundRect(rect, r, r, fill);
            return;
        }
        float fw = w * shown;
        if (fw <= 0) return;
        rect.set(0, 0, fw, h);
        canvas.drawRoundRect(rect, r, r, fill);
        // Highlight running along the filled part.
        float band = Math.max(h * 6, fw * .35f), x = -band + (fw + band) * phase;
        shine.setShader(new LinearGradient(x, 0, x + band, 0,
                new int[]{0x00FFFFFF, 0x73FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
        canvas.save();
        canvas.clipRect(0, 0, fw, h);
        canvas.drawRoundRect(rect, r, r, shine);
        canvas.restore();
    }
}
