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
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Spanned;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.EditText;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.DialogFragment;
import androidx.preference.PreferenceManager;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.ui.PickerSliderView;
import paulscode.android.mupen64plusae.ui.RainbowPickerView;
import paulscode.android.mupen64plusae.ui.UiTheme;

/**
 * A fully customizable color picker dialog with a rainbow hue/saturation box,
 * sliders for saturation, brightness, opacity and contrast, plus hex entry.
 *
 * Touch and gamepad (D-pad) input are both supported on every control.
 */
public class ColorPickerDialogFragment extends DialogFragment {

    public interface OnColorSelectedListener {
        /**
         * @param color  the final ARGB color selected
         * @param applyOpacity true when the opacity slider should override the color's alpha
         * @param opacity the opacity value in 0..1 (valid when applyOpacity is true)
         */
        void onColorSelected(int color, boolean applyOpacity, float opacity);
    }

    private static final String ARG_INITIAL_COLOR = "initialColor";
    private static final String ARG_SHOW_OPACITY = "showOpacity";

    private OnColorSelectedListener mListener;
    private int mInitialColor;
    private boolean mShowOpacity;

    private View mPreviewCurrent;
    private View mPreviewNew;
    private RainbowPickerView mRainbowBox;
    private PickerSliderView mHueSlider;
    private PickerSliderView mSaturationSlider;
    private PickerSliderView mValueSlider;
    private PickerSliderView mOpacitySlider;
    private PickerSliderView mContrastSlider;
    private EditText mHexValue;

    private float mOpacity = 1f;
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
        mOpacity = Color.alpha(mInitialColor) / 255.0f;
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
                        int color = mRainbowBox.getColor();
                        color = UiTheme.withAlpha(color, mShowOpacity
                                ? Math.round(mOpacity * 255) : 255);
                        mListener.onColorSelected(color, mShowOpacity, mOpacity);
                    }
                })
                .setNegativeButton(android.R.string.cancel, null)
                .create();

        // Gamepad support: ENTER/A or CENTER on the positive button confirms, B cancels
        dialog.setOnKeyListener((d, keyCode, event) -> {
            if (event.getAction() == KeyEvent.ACTION_DOWN
                    && (keyCode == KeyEvent.KEYCODE_BUTTON_A
                    || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                    || keyCode == KeyEvent.KEYCODE_ENTER)) {
                View positive = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
                if (positive != null && positive.hasFocus()) {
                    positive.performClick();
                    return true;
                }
            }
            return false;
        });

        dialog.setOnShowListener(d -> UiTheme.get(context).applyToDialog(dialog));
        return dialog;
    }

    private void bindViews(View view) {
        mPreviewCurrent = view.findViewById(R.id.colorPreviewCurrent);
        mPreviewNew = view.findViewById(R.id.colorPreviewNew);
        mRainbowBox = view.findViewById(R.id.rainbowBox);
        mHueSlider = view.findViewById(R.id.hueSlider);
        mSaturationSlider = view.findViewById(R.id.saturationSlider);
        mValueSlider = view.findViewById(R.id.valueSlider);
        mOpacitySlider = view.findViewById(R.id.opacitySlider);
        mContrastSlider = view.findViewById(R.id.contrastSlider);
        mHexValue = view.findViewById(R.id.hexValue);
    }

    private void setupControls() {
        mPreviewCurrent.setBackgroundColor(mInitialColor);

        // Clamp hex entry to 6 or 8 hex digits
        mHexValue.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
            for (int i = start; i < end; i++) {
                char c = source.charAt(i);
                if (!Character.isLetterOrDigit(c) && c != '#') {
                    return "";
                }
            }
            return null;
        }});

        mRainbowBox.setColor(mInitialColor);
        mRainbowBox.setShowValue(false);
        mRainbowBox.setOnColorChangedListener(color -> {
            mHueSlider.setValue(mRainbowBox.getHue() / 360f);
            mSaturationSlider.setValue(mRainbowBox.getSaturation());
            mValueSlider.setColor(color);
            updatePreview(color);
        });

        mHueSlider.setMode(PickerSliderView.Mode.HUE);
        mHueSlider.setValue(mRainbowBox.getHue() / 360f);
        mHueSlider.setOnValueChangedListener(value -> {
            mRainbowBox.setColor(Color.HSVToColor(new float[]{
                    value * 360f, mRainbowBox.getSaturation(), mRainbowBox.getValue()}));
            updatePreview(mRainbowBox.getColor());
        });

        mSaturationSlider.setMode(PickerSliderView.Mode.VALUE);
        mSaturationSlider.setColor(0xFFFFFFFF);
        mSaturationSlider.setValue(mRainbowBox.getSaturation());
        mSaturationSlider.setOnValueChangedListener(value -> {
            mRainbowBox.setColor(Color.HSVToColor(new float[]{
                    mRainbowBox.getHue(), value, mRainbowBox.getValue()}));
            updatePreview(mRainbowBox.getColor());
        });

        mValueSlider.setMode(PickerSliderView.Mode.VALUE);
        mValueSlider.setColor(mRainbowBox.getColor());
        mValueSlider.setValue(mRainbowBox.getValue());
        mValueSlider.setOnValueChangedListener(value -> {
            mRainbowBox.setValue(value);
            updatePreview(mRainbowBox.getColor());
        });

        mOpacitySlider.setMode(PickerSliderView.Mode.ALPHA);
        mOpacitySlider.setColor(mRainbowBox.getColor());
        mOpacitySlider.setValue(mOpacity);
        mOpacitySlider.setOnValueChangedListener(value -> {
            mOpacity = value;
            updatePreview(mRainbowBox.getColor());
        });
        mOpacitySlider.setVisibility(mShowOpacity ? View.VISIBLE : View.GONE);
        mOpacitySlider.setEnabled(mShowOpacity);
        mOpacitySlider.setFocusable(mShowOpacity);
        // find the opacity label (previous sibling) and hide it too
        if (mOpacitySlider.getParent() instanceof android.view.ViewGroup) {
            android.view.ViewGroup parent =
                    (android.view.ViewGroup) mOpacitySlider.getParent();
            int index = parent.indexOfChild(mOpacitySlider);
            if (index > 0 && parent.getChildAt(index - 1) instanceof android.widget.TextView) {
                parent.getChildAt(index - 1).setVisibility(mShowOpacity ? View.VISIBLE : View.GONE);
            }
        }

        float contrast = UiTheme.get(requireContext()).contrast();
        mContrastSlider.setMode(PickerSliderView.Mode.CONTRAST);
        mContrastSlider.setValue((contrast - 0.5f) / 1.5f);
        mContrastSlider.setOnValueChangedListener(value -> {
            // Live-preview contrast only; persisted on OK
            float newContrast = 0.5f + value * 1.5f;
            UiTheme.get(requireContext()).prefs().edit()
                    .putInt(UiTheme.KEY_CONTRAST, Math.round(newContrast * 100))
                    .apply();
            UiTheme.get(requireContext()).reload();
            updatePreview(mRainbowBox.getColor());
        });

        mHexValue.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                if (mUpdatingHex) return;
                String text = s.toString().replace("#", "");
                if (text.length() == 6 || text.length() == 8) {
                    try {
                        int color = Color.parseColor("#" + text);
                        mUpdatingHex = true;
                        mRainbowBox.setColor(color);
                        mHueSlider.setValue(mRainbowBox.getHue() / 360f);
                        mSaturationSlider.setValue(mRainbowBox.getSaturation());
                        mValueSlider.setValue(mRainbowBox.getValue());
                        if (text.length() == 8) {
                            mOpacity = Color.alpha(color) / 255.0f;
                            mOpacitySlider.setValue(mOpacity);
                        }
                        mUpdatingHex = false;
                        updatePreview(color);
                    } catch (IllegalArgumentException ignored) {
                    }
                }
            }
        });

        updatePreview(mInitialColor);
    }

    private void updatePreview(int color) {
        int displayed = mShowOpacity ? UiTheme.withAlpha(color, Math.round(mOpacity * 255)) : color;
        mPreviewNew.setBackgroundColor(displayed);
        mUpdatingHex = true;
        mHexValue.setText(UiTheme.toHex(displayed));
        mUpdatingHex = false;
        mValueSlider.setColor(color);
        mOpacitySlider.setColor(color);
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        // Contrast is applied live; if the user cancels, restore the previous value
        UiTheme.get(requireContext()).reload();
    }
}
