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
package paulscode.android.mupen64plusae.dialog;

import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.EditText;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.DialogFragment;

import com.skydoves.colorpickerview.AlphaTileView;
import com.skydoves.colorpickerview.ColorEnvelope;
import com.skydoves.colorpickerview.ColorPickerView;
import com.skydoves.colorpickerview.listeners.ColorEnvelopeListener;
import com.skydoves.colorpickerview.sliders.AlphaSlideBar;
import com.skydoves.colorpickerview.sliders.BrightnessSlideBar;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.ui.UiTheme;

/**
 * Modern, aesthetic color picker dialog powered by Skydoves ColorPickerView.
 * Supports HSV color wheel, brightness, alpha/opacity sliders, hex entry, D-pad,
 * and direct analog-stick control for intuitive 360-degree color selection.
 */
public class ColorPickerDialogFragment extends DialogFragment {

    public interface OnColorSelectedListener {
        void onColorSelected(int color, boolean applyOpacity, float opacity);
    }

    private static final String ARG_INITIAL_COLOR = "initialColor";
    private static final String ARG_SHOW_OPACITY = "showOpacity";

    private OnColorSelectedListener mListener;
    private int mInitialColor;
    private boolean mShowOpacity;

    private AlphaTileView mPreviewCurrent;
    private AlphaTileView mPreviewNew;
    private ColorPickerView mColorPickerView;
    private BrightnessSlideBar mBrightnessSlideBar;
    private AlphaSlideBar mAlphaSlideBar;
    private View mAlphaContainer;
    private EditText mHexValue;

    private int mCurrentColor;
    private boolean mUpdatingHex = false;

    public static ColorPickerDialogFragment newInstance(int initialColor, boolean showOpacity,
            OnColorSelectedListener listener) {
        ColorPickerDialogFragment fragment = new ColorPickerDialogFragment();
        Bundle args = new Bundle();
        args.putInt(ARG_INITIAL_COLOR, initialColor);
        args.putBoolean(ARG_SHOW_OPACITY, showOpacity);
        fragment.setArguments(args);
        fragment.mListener = listener;
        return fragment;
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setStyle(DialogFragment.STYLE_NORMAL, R.style.Theme_Mupen64PlusAE_AlertDialog);
        if (getArguments() != null) {
            mInitialColor = getArguments().getInt(ARG_INITIAL_COLOR, 0xFF00DFDF);
            mShowOpacity = getArguments().getBoolean(ARG_SHOW_OPACITY, true);
        }
        mCurrentColor = mInitialColor;
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(@Nullable Bundle savedInstanceState) {
        Context context = requireContext();
        View view = LayoutInflater.from(context)
                .inflate(R.layout.dialog_color_picker, null);
        bindViews(view);
        setupControls();

        AlertDialog dialog = new AlertDialog.Builder(context)
                .setTitle(R.string.uiTheme_pickColor)
                .setView(view)
                .setPositiveButton(android.R.string.ok, (d, which) -> {
                    if (mListener != null) {
                        int finalColor = mShowOpacity ? mCurrentColor : (0xFF000000 | (mCurrentColor & 0x00FFFFFF));
                        float opacity = Color.alpha(finalColor) / 255.0f;
                        mListener.onColorSelected(finalColor, mShowOpacity, opacity);
                    }
                })
                .setNegativeButton(android.R.string.cancel, null)
                .create();

        // Gamepad key support: 'A', 'Start', 'Enter', or D-pad Center immediately confirms 'OK'.
        // 'B' or 'Back' cancels.
        dialog.setOnKeyListener((d, keyCode, event) -> {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (keyCode == KeyEvent.KEYCODE_BUTTON_A
                        || keyCode == KeyEvent.KEYCODE_BUTTON_START
                        || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                        || keyCode == KeyEvent.KEYCODE_ENTER
                        || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER) {
                    View positive = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
                    if (positive != null) {
                        positive.performClick();
                        return true;
                    }
                } else if (keyCode == KeyEvent.KEYCODE_BUTTON_B
                        || keyCode == KeyEvent.KEYCODE_BACK
                        || keyCode == KeyEvent.KEYCODE_ESCAPE) {
                    View negative = dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
                    if (negative != null) {
                        negative.performClick();
                        return true;
                    }
                }
            }
            return false;
        });

        dialog.setOnShowListener(d -> {
            UiTheme.get(context).applyToDialog(dialog);
            Window window = dialog.getWindow();
            if (window != null) {
                float density = context.getResources().getDisplayMetrics().density;
                int screenWidth = context.getResources().getDisplayMetrics().widthPixels;
                int targetWidth = Math.round(540 * density);
                window.setLayout(Math.min(targetWidth, (int)(screenWidth * 0.92f)), ViewGroup.LayoutParams.WRAP_CONTENT);
            }
        });

        return dialog;
    }

    private void bindViews(View view) {
        mPreviewCurrent = view.findViewById(R.id.colorPreviewCurrent);
        mPreviewNew = view.findViewById(R.id.colorPreviewNew);
        mColorPickerView = view.findViewById(R.id.colorPickerView);
        mBrightnessSlideBar = view.findViewById(R.id.brightnessSlideBar);
        mAlphaSlideBar = view.findViewById(R.id.alphaSlideBar);
        mAlphaContainer = view.findViewById(R.id.alphaSlideBarContainer);
        mHexValue = view.findViewById(R.id.hexValue);
    }

    private void setupControls() {
        mPreviewCurrent.setPaintColor(mInitialColor);
        mPreviewNew.setPaintColor(mInitialColor);

        mColorPickerView.attachBrightnessSlider(mBrightnessSlideBar);

        if (mShowOpacity) {
            mColorPickerView.attachAlphaSlider(mAlphaSlideBar);
            if (mAlphaContainer != null) mAlphaContainer.setVisibility(View.VISIBLE);
        } else {
            if (mAlphaContainer != null) mAlphaContainer.setVisibility(View.GONE);
        }

        mColorPickerView.setInitialColor(mInitialColor);

        mColorPickerView.setColorListener((ColorEnvelopeListener) (envelope, fromUser) -> {
            mCurrentColor = envelope.getColor();
            mPreviewNew.setPaintColor(mCurrentColor);
            if (!mUpdatingHex) {
                mUpdatingHex = true;
                mHexValue.setText(mShowOpacity ? UiTheme.toHex(mCurrentColor) : ("#" + envelope.getHexCode().substring(2)));
                mUpdatingHex = false;
            }
        });

        // Clamp hex entry to hex chars
        mHexValue.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
            for (int i = start; i < end; i++) {
                char c = source.charAt(i);
                if (!Character.isLetterOrDigit(c) && c != '#') {
                    return "";
                }
            }
            return null;
        }});

        mHexValue.setText(UiTheme.toHex(mInitialColor));

        mHexValue.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {}

            @Override
            public void afterTextChanged(Editable s) {
                if (mUpdatingHex) return;
                String text = s.toString().trim();
                if (text.startsWith("#")) text = text.substring(1);
                if (text.length() == 6 || text.length() == 8) {
                    try {
                        int parsed = Color.parseColor("#" + text);
                        mUpdatingHex = true;
                        mCurrentColor = parsed;
                        mColorPickerView.setInitialColor(parsed);
                        mPreviewNew.setPaintColor(parsed);
                        mUpdatingHex = false;
                    } catch (IllegalArgumentException ignored) {}
                }
            }
        });
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        UiTheme.get(requireContext()).reload();
    }
}
