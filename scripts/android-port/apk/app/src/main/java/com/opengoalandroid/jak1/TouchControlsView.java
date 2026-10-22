package com.opengoalandroid.jak1;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.SystemClock;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import java.util.Locale;

/** Lightweight transparent, multi-touch gamepad above SDL's rendering surface. (AI-assisted) */
final class TouchControlsView extends View {
    private final TouchControlsState state = new TouchControlsState();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float scale = 1, offsetX, offsetY;
    private int insetLeft, insetTop, insetRight, insetBottom;
    private boolean controlsVisible = true;
    private Runnable onMenu;
    private long lastFrames, lastSampleTime;
    private String fpsLabel = "FPS: --";
    private final Runnable fpsSample = new Runnable() {
        @Override
        public void run() {
            long now = SystemClock.elapsedRealtime();
            long frames = nativeGetFrameCount();
            long elapsed = now - lastSampleTime;
            long count = frames - lastFrames;
            if (elapsed > 0) {
                float fps = count * 1000f / elapsed;
                fpsLabel = String.format(Locale.US, "%.1f FPS  |  %.1f ms", fps,
                                         count > 0 ? elapsed / (float) count : 0);
                Log.i("OpenGOALPerf", fpsLabel);
            }
            lastFrames = frames;
            lastSampleTime = now;
            invalidate();
            postDelayed(this, 1000);
        }
    };

    private final String hideLabel, showLabel, menuLabel, moveLabel, cameraLabel;
    private boolean showFps = true;

    /** The game's Graphics option "FPS counter". (AI-assisted) */
    void setShowFps(boolean show) {
        showFps = show;
        invalidate();
    }

    TouchControlsView(Context context) {
        super(context);
        setFocusable(false);
        setContentDescription(context.getString(R.string.touch_description));
        hideLabel = context.getString(R.string.touch_hide);
        showLabel = context.getString(R.string.touch_show);
        menuLabel = context.getString(R.string.touch_menu);
        moveLabel = context.getString(R.string.touch_move);
        cameraLabel = context.getString(R.string.touch_camera);
    }

    private static native void nativeSetState(float lx, float ly, float rx, float ry, int buttons);
    private static native long nativeGetFrameCount();

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        lastFrames = nativeGetFrameCount();
        lastSampleTime = SystemClock.elapsedRealtime();
        postDelayed(fpsSample, 1000);
        requestApplyInsets();
    }

    private void publish() {
        nativeSetState(state.axes[0], state.axes[1], state.axes[2], state.axes[3], state.buttons);
        invalidate();
    }

    /** Shows or hides the on-screen pad (the MANDO/OCULTAR toggle stays available). (AI-assisted) */
    void setControlsVisible(boolean visible) {
        if (controlsVisible == visible) return;
        controlsVisible = visible;
        releaseAll();
    }

    /** Called by the MENÚ button, which stays visible even with the pad hidden. (AI-assisted) */
    void setOnMenu(Runnable menu) {
        onMenu = menu;
        invalidate();
    }

    void releaseAll() {
        state.reset();
        publish();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateLayout();
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        if (insets.getDisplayCutout() != null) {
            insetLeft = Math.max(insetLeft, insets.getDisplayCutout().getSafeInsetLeft());
            insetTop = Math.max(insetTop, insets.getDisplayCutout().getSafeInsetTop());
            insetRight = Math.max(insetRight, insets.getDisplayCutout().getSafeInsetRight());
            insetBottom = Math.max(insetBottom, insets.getDisplayCutout().getSafeInsetBottom());
        }
        updateLayout();
        return insets;
    }

    private void updateLayout() {
        // Keep controls away from screen edges/cutouts and preserve sizes on tablets.
        int w = getWidth() - insetLeft - insetRight;
        int h = getHeight() - insetTop - insetBottom;
        scale = Math.max(0.01f, Math.min(w / 1000f, h / 450f));
        offsetX = insetLeft + (w - 1000 * scale) / 2;
        offsetY = insetTop + (h - 450 * scale) / 2;
        releaseAll();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused) releaseAll();
    }

    @Override
    protected void onDetachedFromWindow() {
        removeCallbacks(fpsSample);
        releaseAll();
        super.onDetachedFromWindow();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        float x = (event.getX(index) - offsetX) / scale;
        float y = (event.getY(index) - offsetY) / scale;
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            if (Math.abs(x - 500) < 35 && Math.abs(y - 32) < 22) {
                controlsVisible = !controlsVisible;
                releaseAll();
                performClick();
            } else if (onMenu != null && Math.abs(x - 590) < 35 && Math.abs(y - 32) < 22) {
                releaseAll();
                onMenu.run();
            } else if (controlsVisible) {
                state.down(event.getPointerId(index), x, y);
            }
        } else if (action == MotionEvent.ACTION_MOVE && controlsVisible) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                state.move(event.getPointerId(i), (event.getX(i) - offsetX) / scale,
                           (event.getY(i) - offsetY) / scale);
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            state.up(event.getPointerId(index));
        } else if (action == MotionEvent.ACTION_CANCEL) {
            state.reset();
        }
        publish();
        // Do not also generate SDL mouse clicks from fingers used for the gamepad.
        return true;
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.save();
        canvas.translate(offsetX, offsetY);
        canvas.scale(scale, scale);
        if (showFps) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(155, 10, 18, 28));
            canvas.drawRoundRect(182, 15, 340, 47, 8, 8, paint);
            text(canvas, fpsLabel, 261, 31, 12, Color.rgb(160, 255, 170));
        }
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(Color.argb(85, 10, 18, 28));
        canvas.drawRoundRect(465, 10, 535, 54, 12, 12, paint);
        text(canvas, controlsVisible ? hideLabel : showLabel, 500, 32, 10, Color.argb(180, 255, 255, 255));
        if (onMenu != null) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(85, 10, 18, 28));
            canvas.drawRoundRect(555, 10, 625, 54, 12, 12, paint);
            text(canvas, menuLabel, 590, 32, 10, Color.argb(180, 255, 255, 255));
        }
        if (controlsVisible) {
            for (TouchControlsState.Control control : state.controls) {
                if (control.code < 0) {
                    drawStick(canvas, control);
                } else {
                    drawButton(canvas, control);
                }
            }
        }
        canvas.restore();
    }

    private void circle(Canvas canvas, float x, float y, float radius, boolean pressed) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(pressed ? Color.argb(135, 90, 155, 215) : Color.argb(65, 12, 20, 30));
        canvas.drawCircle(x, y, radius, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.5f);
        paint.setColor(Color.argb(pressed ? 230 : 125, 230, 240, 255));
        canvas.drawCircle(x, y, radius, paint);
    }

    private void drawStick(Canvas canvas, TouchControlsState.Control c) {
        int axis = c.code == TouchControlsState.LEFT_STICK ? 0 : 2;
        circle(canvas, c.x, c.y, c.radius, false);
        circle(canvas, c.x + state.axes[axis] * c.radius * 0.55f,
               c.y + state.axes[axis + 1] * c.radius * 0.55f, c.radius * 0.40f,
               state.axes[axis] != 0 || state.axes[axis + 1] != 0);
        text(canvas, c.code == TouchControlsState.LEFT_STICK ? moveLabel : cameraLabel, c.x, c.y + c.radius + 14, 9, Color.argb(155, 240, 245, 255));
    }

    private void drawButton(Canvas canvas, TouchControlsState.Control c) {
        circle(canvas, c.x, c.y, c.radius, (state.buttons & (1 << c.code)) != 0);
        int color = Color.argb(205, 240, 245, 255);
        if (c.code == TouchControlsState.CROSS) color = Color.argb(230, 130, 190, 255);
        if (c.code == TouchControlsState.CIRCLE) color = Color.argb(230, 255, 145, 145);
        if (c.code == TouchControlsState.SQUARE) color = Color.argb(230, 245, 165, 230);
        if (c.code == TouchControlsState.TRIANGLE) color = Color.argb(230, 140, 245, 185);
        float size = c.code <= TouchControlsState.TRIANGLE ? 28 : c.label.length() > 3 ? 9 : 14;
        text(canvas, c.label, c.x, c.y, size, color);
    }

    private void text(Canvas canvas, String label, float x, float y, float size, int color) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        paint.setTextSize(size);
        paint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(label, x, y - (paint.ascent() + paint.descent()) / 2, paint);
    }
}
