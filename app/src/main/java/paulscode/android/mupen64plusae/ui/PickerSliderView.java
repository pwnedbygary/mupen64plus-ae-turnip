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
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.Nullable;

/**
 * A horizontal gradient slider used for hue, value (brightness), opacity and contrast.
 *
 * - Touch: drag the thumb.
 * - Gamepad: D-pad left/right adjusts the value while this view has focus.
 */
public class PickerSliderView extends View {

    public interface OnValueChangedListener {
        void onValueChanged(float value); // 0..1
    }

    private final Paint mTrackPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mThumbPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mFocusPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mOverlayPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public enum Mode {
        HUE,           // full rainbow gradient
        VALUE,         // black -> color
        ALPHA,         // transparent -> color
        CONTRAST       // gray ramp, thumb from left (low) to right (high)
    }

    private Mode mMode = Mode.HUE;
    private float mValue = 1f; // 0..1
    private int mColor = Color.WHITE;
    private OnValueChangedListener mListener;

    public PickerSliderView(Context context) {
        this(context, null);
    }

    public PickerSliderView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        setFocusable(true);
        setFocusableInTouchMode(true);
        mThumbPaint.setStyle(Paint.Style.FILL);
        mOverlayPaint.setStyle(Paint.Style.FILL);
        mFocusPaint.setStyle(Paint.Style.STROKE);
        mFocusPaint.setStrokeWidth(dp(2f));
        mFocusPaint.setColor(0x88FFFFFF);
    }

    public void setMode(Mode mode) {
        mMode = mode;
        invalidate();
    }

    public void setColor(int color) {
        mColor = color;
        invalidate();
    }

    public void setValue(float value) {
        mValue = clamp(value, 0f, 1f);
        invalidate();
    }

    public float getValue() {
        return mValue;
    }

    public void setOnValueChangedListener(OnValueChangedListener listener) {
        mListener = listener;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float width = getWidth();
        float height = getHeight();
        float trackHeight = dp(12f);
        float trackTop = (height - trackHeight) / 2f;
        float trackLeft = dp(12f);
        float trackRight = width - dp(12f);

        if (width <= 0 || height <= 0) return;

        int[] colors;
        switch (mMode) {
            case HUE:
                colors = new int[7];
                for (int i = 0; i < 7; i++) {
                    colors[i] = Color.HSVToColor(new float[]{i * 60f, 1f, 1f});
                }
                break;
            case VALUE:
                colors = new int[]{0xFF000000, mColor | 0xFF000000};
                break;
            case ALPHA:
                colors = new int[]{0x00000000 | (mColor & 0x00FFFFFF), mColor};
                break;
            case CONTRAST:
                colors = new int[]{0xFF404040, 0xFFFFFFFF};
                break;
            default:
                colors = new int[]{0xFF000000, 0xFFFFFFFF};
                break;
        }

        Shader shader = new LinearGradient(trackLeft, 0, trackRight, 0, colors, null, Shader.TileMode.CLAMP);
        mTrackPaint.setShader(shader);
        canvas.drawRoundRect(trackLeft, trackTop, trackRight, trackTop + trackHeight,
                trackHeight / 2f, trackHeight / 2f, mTrackPaint);
        mTrackPaint.setShader(null);

        // Alpha checkerboard backdrop so transparency is visible
        if (mMode == Mode.ALPHA) {
            mOverlayPaint.setColor(0xFF333333);
            canvas.drawRect(trackLeft, trackTop, trackRight, trackTop + trackHeight, mOverlayPaint);
        }

        // Thumb
        float thumbX = trackLeft + (trackRight - trackLeft) * mValue;
        float thumbRadius = dp(12f);
        mThumbPaint.setColor(isFocused() ? 0xFFFFFFFF : 0xEEFFFFFF);
        canvas.drawCircle(thumbX, height / 2f, thumbRadius, mThumbPaint);
        mThumbPaint.setColor(0xFF000000);
        canvas.drawCircle(thumbX, height / 2f, thumbRadius - dp(1.5f), mThumbPaint);
        mThumbPaint.setColor(isFocused() ? 0xFFFFFFFF : 0xEEFFFFFF);
        canvas.drawCircle(thumbX, height / 2f, thumbRadius - dp(4f), mThumbPaint);

        // Focus ring
        if (isFocused()) {
            canvas.drawRect(dp(2), dp(2), width - dp(2), height - dp(2), mFocusPaint);
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
                float trackLeft = dp(12f);
                float trackRight = getWidth() - dp(12f);
                mValue = clamp((event.getX() - trackLeft) / (trackRight - trackLeft), 0f, 1f);
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
        float step = 0.02f;
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_LEFT:
                setValue(mValue - step);
                notifyChanged();
                return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                setValue(mValue + step);
                notifyChanged();
                return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void notifyChanged() {
        if (mListener != null) {
            mListener.onValueChanged(mValue);
        }
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
