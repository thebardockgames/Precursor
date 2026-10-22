package com.opengoalandroid.jak1;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.*;
import android.view.View;
import android.view.animation.LinearInterpolator;

/**
 * The Precursor "Orbe" (same artwork as the app icon), drawn natively so it can animate:
 *  - ASSEMBLE: builds itself on the welcome screen; STATIC: the finished emblem;
 *  - IDLE: switched off (dark tips, dim core) while the player picks a game and an ISO;
 *  - SCAN: a sweep circles it while the ISO is checked;
 *  - PROGRESS: the ring fills with the installation, and each finished stage lights one tip;
 *  - DONE: everything lit, a ring bursts and small orbs fly out;
 *  - ERROR: a single shake, then it stays dim;
 *  - LOADING: a brass arc spins while the game starts;
 *  - MARK: a faint outline watermark.
 * Animations follow the system animation scale, so "remove animations" in Android's
 * accessibility settings turns them off. (AI-assisted)
 */
final class OrbView extends View {
    enum Mode { STATIC, ASSEMBLE, IDLE, SCAN, PROGRESS, DONE, ERROR, LOADING, MARK }

    private static final int BRASS_A = 0xFFF3CF7A, BRASS_B = 0xFFD39A35, BRASS_C = 0xFF9A6516;
    private static final int TIP_OFF = 0xFF4A3A20, MOTES = 14;
    private Mode mode;
    private float assemble = 1f, pulse = 0f, spin = 0f, burst = 1f, progress = 0f, coreLit = 1f;
    private final float[] tipLit = {1f, 1f, 1f, 1f};
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG), fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG), core = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG), off = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shine = new Paint(Paint.ANTI_ALIAS_FLAG), dim = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mote = new Paint(Paint.ANTI_ALIAS_FLAG), scan = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path notch = new Path();
    private final RectF arc = new RectF(29, 29, 79, 79);
    private ValueAnimator assembleAnim, pulseAnim, spinAnim, burstAnim, progressAnim, coreAnim;
    private final ValueAnimator[] tipAnims = new ValueAnimator[4];

    OrbView(Context context, Mode mode) {
        super(context);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeCap(Paint.Cap.BUTT);
        line.setStyle(Paint.Style.STROKE);
        off.setColor(TIP_OFF);
        shine.setColor(0xB3FFFFFF);
        dim.setColor(0xFF06121A);
        Shader brass = new LinearGradient(22, 22, 86, 86, new int[]{BRASS_A, BRASS_B, BRASS_C},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP);
        fill.setShader(brass);
        ring.setShader(new LinearGradient(29, 29, 79, 79, new int[]{BRASS_A, BRASS_B, BRASS_C},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        core.setShader(new RadialGradient(50, 48, 24, new int[]{0xFFB9FFF4, 0xFF3FD6CF, 0xFF0F6F78},
                new float[]{0f, .45f, 1f}, Shader.TileMode.CLAMP));
        scan.setShader(new SweepGradient(54, 54, new int[]{0x007CF3E6, 0x007CF3E6, 0x737CF3E6},
                new float[]{0f, .82f, 1f}));
        this.mode = mode;
        applyModeState(mode);
    }

    /** Tips and core for a mode, without animation (a freshly built screen). */
    private void applyModeState(Mode m) {
        boolean dark = m == Mode.IDLE || m == Mode.SCAN || m == Mode.PROGRESS;
        for (int i = 0; i < 4; i++) tipLit[i] = dark ? 0f : 1f;
        coreLit = dark ? 0f : 1f;
        if (m == Mode.ASSEMBLE) assemble = 0f;
        if (m == Mode.DONE) burst = 0f;
        if (m == Mode.ERROR) {
            ColorMatrix gray = new ColorMatrix();
            gray.setSaturation(.35f);
            Paint layer = new Paint();
            layer.setColorFilter(new ColorMatrixColorFilter(gray));
            setLayerType(LAYER_TYPE_HARDWARE, layer);
            setAlpha(.55f);
        }
    }

    /** Ring fill for PROGRESS (and the dimmed ring of ERROR); glides to the new value. */
    void setProgress(int percent) {
        float target = Math.max(0, Math.min(100, percent)) / 100f;
        if (progressAnim != null) progressAnim.cancel();
        progressAnim = ValueAnimator.ofFloat(progress, target).setDuration(500);
        progressAnim.setInterpolator(Motion.OUT);
        progressAnim.addUpdateListener(a -> { progress = (float) a.getAnimatedValue(); invalidate(); });
        progressAnim.start();
    }

    /** Installation stages finished so far (0-5): each lights one tip, the fifth the core. */
    void setStagesDone(int done) {
        for (int i = 0; i < 4; i++) animateTip(i, i < done ? 1f : 0f);
        float target = done >= 5 ? 1f : 0f;
        if (coreLit != target) {
            if (coreAnim != null) coreAnim.cancel();
            coreAnim = ValueAnimator.ofFloat(coreLit, target).setDuration(500);
            coreAnim.addUpdateListener(a -> { coreLit = (float) a.getAnimatedValue(); invalidate(); });
            coreAnim.start();
        }
    }

    private void animateTip(int i, float target) {
        if (tipLit[i] == target) return;
        if (tipAnims[i] != null) tipAnims[i].cancel();
        tipAnims[i] = ValueAnimator.ofFloat(tipLit[i], target).setDuration(400);
        tipAnims[i].setInterpolator(Motion.OUT);
        tipAnims[i].addUpdateListener(a -> { tipLit[i] = (float) a.getAnimatedValue(); invalidate(); });
        tipAnims[i].start();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        startAnimations();
    }

    @Override protected void onDetachedFromWindow() {
        stopAnimations();
        super.onDetachedFromWindow();
    }

    private ValueAnimator loop(float to, long ms, ValueAnimator.AnimatorUpdateListener update) {
        ValueAnimator a = ValueAnimator.ofFloat(0f, to).setDuration(ms);
        a.setRepeatCount(ValueAnimator.INFINITE);
        a.setInterpolator(new LinearInterpolator());
        a.addUpdateListener(update);
        a.start();
        return a;
    }

    private void startAnimations() {
        if (mode == Mode.MARK || mode == Mode.STATIC) return;
        if (mode == Mode.ASSEMBLE && assemble < 1f) {
            assembleAnim = ValueAnimator.ofFloat(0f, 1f).setDuration(1600);
            assembleAnim.setStartDelay(150);
            assembleAnim.addUpdateListener(a -> { assemble = (float) a.getAnimatedValue(); invalidate(); });
            assembleAnim.start();
        }
        if (mode != Mode.ERROR) {
            pulseAnim = loop((float) (Math.PI * 2), 2600, a -> { pulse = (float) a.getAnimatedValue(); invalidate(); });
        }
        if (mode == Mode.SCAN) spinAnim = loop(360f, 1600, a -> { spin = (float) a.getAnimatedValue(); invalidate(); });
        if (mode == Mode.LOADING) spinAnim = loop(360f, 1400, a -> { spin = (float) a.getAnimatedValue(); invalidate(); });
        if (mode == Mode.DONE && burst < 1f) {
            burstAnim = ValueAnimator.ofFloat(0f, 1f).setDuration(1600);
            burstAnim.setInterpolator(Motion.OUT);
            burstAnim.addUpdateListener(a -> { burst = (float) a.getAnimatedValue(); invalidate(); });
            burstAnim.start();
            animate().scaleX(1.06f).scaleY(1.06f).setDuration(300).withEndAction(
                    () -> animate().scaleX(1f).scaleY(1f).setDuration(300).start()).start();
        }
        if (mode == Mode.ERROR) Motion.shake(this);
    }

    private void stopAnimations() {
        for (ValueAnimator a : new ValueAnimator[]{assembleAnim, pulseAnim, spinAnim, burstAnim, progressAnim, coreAnim})
            if (a != null) a.cancel();
        for (ValueAnimator a : tipAnims) if (a != null) a.cancel();
        assembleAnim = pulseAnim = spinAnim = burstAnim = null;
        assemble = 1f;
        burst = 1f;
    }

    private static float ease(float t, float from, float to) {
        float x = Math.max(0f, Math.min(1f, (t - from) / (to - from)));
        return 1f - (1f - x) * (1f - x) * (1f - x);
    }

    @Override protected void onDraw(Canvas canvas) {
        float size = Math.min(getWidth(), getHeight());
        if (size <= 0) return;
        canvas.save();
        canvas.translate((getWidth() - size) / 2f, (getHeight() - size) / 2f);
        // Artwork coordinates: the icon's 108-unit canvas, cropped to 16..92 (76 units).
        float s = size / 76f;
        canvas.scale(s, s);
        canvas.translate(-16f, -16f);

        if (mode == Mode.MARK) {
            line.setColor(0xFFE2B25A);
            line.setStrokeWidth(1.6f);
            line.setPathEffect(null);
            canvas.drawCircle(54, 54, 25, line);
            line.setColor(0xFF3FD6CF);
            line.setStrokeWidth(.8f);
            line.setPathEffect(new DashPathEffect(new float[]{3, 4}, 0));
            canvas.drawCircle(54, 54, 31, line);
            Paint flat = new Paint(Paint.ANTI_ALIAS_FLAG);
            flat.setColor(0xFFE2B25A);
            for (int i = 0; i < 4; i++) drawNotch(canvas, i, 1f, flat);
            canvas.restore();
            return;
        }

        float glowAlpha = (0.35f + 0.25f * coreLit) + 0.2f * (float) Math.sin(pulse);
        glow.setShader(new RadialGradient(54, 54, 36, new int[]{
                Color.argb(Math.round(255 * Math.max(0, glowAlpha)), 124, 243, 230), Color.argb(0, 124, 243, 230)},
                null, Shader.TileMode.CLAMP));
        canvas.drawCircle(54, 54, 36, glow);

        if (mode == Mode.DONE && burst < 1f) drawBurst(canvas);
        if (mode == Mode.SCAN) {
            canvas.save();
            canvas.rotate(spin, 54, 54);
            canvas.drawCircle(54, 54, 37, scan);
            canvas.restore();
        }

        ring.setStrokeWidth(5f);
        boolean tracked = mode == Mode.IDLE || mode == Mode.SCAN || mode == Mode.PROGRESS
                || mode == Mode.ERROR || mode == Mode.LOADING;
        if (tracked) {
            Paint track = new Paint(ring);
            track.setShader(null);
            track.setColor(0x2EE2B25A);
            canvas.drawArc(arc, 0, 360, false, track);
        }
        if (mode == Mode.PROGRESS || mode == Mode.ERROR) {
            if (progress > 0f) canvas.drawArc(arc, -90, 360f * progress, false, ring);
        } else if (mode == Mode.LOADING) {
            ring.setStrokeCap(Paint.Cap.ROUND);
            canvas.drawArc(arc, -90 + spin, 120, false, ring);
            ring.setStrokeCap(Paint.Cap.BUTT);
        } else if (mode != Mode.IDLE && mode != Mode.SCAN) {
            canvas.drawArc(arc, -90, 360f * ease(assemble, 0f, .65f), false, ring);
        }

        float[] grow = {ease(assemble, .55f, .75f), ease(assemble, .6f, .8f),
                ease(assemble, .65f, .85f), ease(assemble, .7f, .9f)};
        for (int i = 0; i < 4; i++) {
            if (grow[i] <= 0f) continue;
            if (tipLit[i] < 1f) drawNotch(canvas, i, grow[i], off);
            if (tipLit[i] > 0f) {
                fill.setAlpha(Math.round(255 * tipLit[i]));
                drawNotch(canvas, i, grow[i], fill);
                fill.setAlpha(255);
            }
        }

        float coreScale = ease(assemble, .3f, .75f);
        if (coreScale > 0f) {
            canvas.save();
            canvas.scale(coreScale, coreScale, 54, 54);
            canvas.drawCircle(54, 54, 17, core);
            if (coreLit < 1f) {
                dim.setAlpha(Math.round(150 * (1f - coreLit)));
                canvas.drawCircle(54, 54, 17, dim);
            }
            canvas.save();
            canvas.rotate(-30, 48, 47);
            shine.setAlpha(Math.round(110 + 69 * coreLit));
            canvas.drawOval(new RectF(43, 43.8f, 53, 50.2f), shine);
            canvas.restore();
            canvas.restore();
        }
        canvas.restore();
    }

    private void drawBurst(Canvas canvas) {
        line.setPathEffect(null);
        line.setStrokeWidth(2.4f * (1 - burst) + .4f);
        line.setColor(Color.argb(Math.round(230 * (1 - burst)), 124, 243, 230));
        canvas.drawCircle(54, 54, 24 + 14 * burst, line);
        // Small orbs flying out of the core and fading.
        for (int i = 0; i < MOTES; i++) {
            float t = Math.max(0f, Math.min(1f, (burst - (i % 4) * .04f) / .9f));
            if (t <= 0f || t >= 1f) continue;
            double a = i * Math.PI * 2 / MOTES;
            float dist = 8 + (26 + (i % 3) * 4) * t;
            float x = 54 + (float) Math.cos(a) * dist, y = 54 + (float) Math.sin(a) * dist - 4 * t;
            float r = .8f + 1.4f * t;
            int alpha = Math.round(255 * (t < .2f ? t / .2f : 1f - (t - .2f) / .8f));
            mote.setShader(new RadialGradient(x, y, r, new int[]{0xFFB9FFF4, 0xFF3FD6CF, 0x003FD6CF},
                    new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
            mote.setAlpha(alpha);
            canvas.drawCircle(x, y, r, mote);
        }
    }

    private void drawNotch(Canvas canvas, int index, float scale, Paint paint) {
        canvas.save();
        canvas.rotate(index * 90, 54, 54);
        canvas.scale(scale, scale, 54, 25.5f);
        notch.reset();
        notch.moveTo(54, 22);
        notch.lineTo(59, 29);
        notch.lineTo(49, 29);
        notch.close();
        canvas.drawPath(notch, paint);
        canvas.restore();
    }
}
