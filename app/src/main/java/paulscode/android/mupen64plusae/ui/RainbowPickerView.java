/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * Copyright (C) 2012 Paul Lamb
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Mupen64PlusAE is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with Mupen64PlusAE. If
 * not, see <http://www.gnu.org/licenses/>.
 */
package paulscode.android.mupen64plusae.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Shader;
import android.graphics.LinearGradient;
import android.graphics.SweepGradient;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.Nullable;

/**
 * A two-dimensional hue/saturation picker rendered as a rainbow box.
 *
 * - Touch: drag anywhere in the box to move the selection.
 * - Gamepad: D-pad left/right changes hue, up/down changes saturation.
 *
 * The box shows the full hue spectrum horizontally and saturation vertically,
 * with the current value (brightness) applied as a black overlay.
 */
public class RainbowPickerView extends View {

    public interface OnColorChangedListener {
        void onColorChanged(int color);
    }

    private static final float DPAD_STEP = 0.01f;
    private static final float TOUCH_TOLERANCE = 0.0f;

    private final Paint mBoxPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mPointerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mFocusPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private float mHue = 180f;       // 0..360
    private float mSaturation = 1f;  // 0..1
    private float mValue = 1f;       // 0..1
    private boolean mShowValue = true;

    private OnColorChangedListener mListener;
    private float mFocusAlpha = 0f;

    public RainbowPickerView(Context context) {
        this(context, null);
    }

    public RainbowPickerView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        setFocusable(true);
        setFocusableInTouchMode(true);
        mBoxPaint.setStyle(Paint.Style.FILL);
        mPointerPaint.setStyle(Paint.Style.STROKE);
        mPointerPaint.setStrokeWidth(dp(2.5f));
        mPointerPaint.setColor(0xFFFFFFFF);
        mFocusPaint.setStyle(Paint.Style.STROKE);
        mFocusPaint.setStrokeWidth(dp(2f));
        mFocusPaint.setColor(0x88FFFFFF);
        mFocusPaint.setShadowLayer(dp(4), 0, 0, 0xCC000000);
    }

    public void setOnColorChangedListener(OnColorChangedListener listener) {
        mListener = listener;
    }

    /** @param color ARGB color to initialize the picker from */
    public void setColor(int color) {
        float[] hsv = new float[3];
        Color.colorToHSV(color, hsv);
        mHue = hsv[0];
        mSaturation = hsv[1];
        mValue = hsv[2];
        invalidate();
    }

    public int getColor() {
        return Color.HSVToColor(new float[]{mHue, mSaturation, mValue});
    }

    public float getHue() { return mHue; }
    public float getSaturation() { return mSaturation; }
    public float getValue() { return mValue; }

    public void setValue(float value) {
        mValue = clamp(value, 0f, 1f);
        invalidate();
    }

    /** When false, the value (brightness) overlay is hidden so hue/saturation are fully visible. */
    public void setShowValue(boolean showValue) {
        mShowValue = showValue;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float width = getWidth();
        float height = getHeight();
        if (width <= 0 || height <= 0) return;

        // Base: full saturation rainbow (hue along X)
        int[] colors = new int[360];
        for (int i = 0; i < 360; i++) {
            colors[i] = Color.HSVToColor(new float[]{i, 1f, 1f});
        }
        Shader shader = new LinearGradient(0, 0, width, 0, colors, null, Shader.TileMode.CLAMP);
        mBoxPaint.setShader(shader);
        canvas.drawRect(0, 0, width, height, mBoxPaint);
        mBoxPaint.setShader(null);

        // Saturation: fade to white from top (sat=0) to bottom (sat=1)
        Shader satShader = new LinearGradient(0, 0, 0, height,
                new int[]{0xFFFFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP);
        mBoxPaint.setShader(satShader);
        canvas.drawRect(0, 0, width, height, mBoxPaint);
        mBoxPaint.setShader(null);

        // Value (brightness): black overlay from bottom
        if (mShowValue && mValue < 1f) {
            Shader valShader = new LinearGradient(0, height * (1f - mValue), 0, height,
                    new int[]{0x00000000, 0xFF000000}, null, Shader.TileMode.CLAMP);
            mBoxPaint.setShader(valShader);
            canvas.drawRect(0, height * (1f - mValue), width, height, mBoxPaint);
            mBoxPaint.setShader(null);
        }

        // Selection pointer
        float x = (mHue / 360f) * width;
        float y = (1f - mSaturation) * height;
        float radius = dp(8f);
        mPointerPaint.setColor(isFocused() ? 0xFFFFFFFF : 0xAAFFFFFF);
        canvas.drawCircle(x, y, radius, mPointerPaint);
        mPointerPaint.setColor(0xFF000000);
        canvas.drawCircle(x, y, radius, mPointerPaint);
        mPointerPaint.setColor(isFocused() ? 0xFFFFFFFF : 0xAAFFFFFF);
        canvas.drawCircle(x, y, radius - dp(2f), mPointerPaint);

        // Focus ring
        if (isFocused()) {
            canvas.drawRect(dp(2), dp(2), width - dp(2), height - dp(2), mFocusPaint);
        }
    }

    private void setHue(float hue) {
        mHue = ((hue % 360f) + 360f) % 360f;
        notifyChanged();
        invalidate();
    }

    private void setSaturation(float saturation) {
        mSaturation = clamp(saturation, 0f, 1f);
        notifyChanged();
        invalidate();
    }

    private void notifyChanged() {
        if (mListener != null) {
            mListener.onColorChanged(getColor());
        }
    }

    // ---------------------------------------------------------------------
    // Touch input
    // ---------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
                float x = clamp(event.getX(), 0, getWidth());
                float y = clamp(event.getY(), 0, getHeight());
                mHue = (x / getWidth()) * 360f;
                mSaturation = 1f - (y / getHeight());
                notifyChanged();
                invalidate();
                return true;
            case MotionEvent.ACTION_UP:
                performClick();
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    // ---------------------------------------------------------------------
    // Gamepad / keyboard input
    // ---------------------------------------------------------------------

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_LEFT:
                setHue(mHue - 2f);
                return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                setHue(mHue + 2f);
                return true;
            case KeyEvent.KEYCODE_DPAD_UP:
                setSaturation(mSaturation + DPAD_STEP);
                return true;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                setSaturation(mSaturation - DPAD_STEP);
                return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    protected void onFocusChanged(boolean gainFocus, int direction, @Nullable android.graphics.Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
        invalidate();
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }

    private static float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }
}
