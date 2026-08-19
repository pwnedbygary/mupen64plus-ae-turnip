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
package paulscode.android.mupen64plusae.preference;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.TypedArray;
import android.util.AttributeSet;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.FragmentActivity;
import androidx.preference.Preference;
import androidx.preference.PreferenceManager;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.dialog.ColorPickerDialogFragment;
import paulscode.android.mupen64plusae.ui.UiTheme;

/**
 * A preference that opens the custom color picker dialog. Stores the picked ARGB color
 * as a hex string in shared preferences, keyed by the preference's {@code key}.
 */
public class ColorPickerPreference extends Preference {

    private final boolean mShowOpacity;

    public ColorPickerPreference(Context context) {
        this(context, null);
    }

    public ColorPickerPreference(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public ColorPickerPreference(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        boolean showOpacity = true;
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.ColorPickerPreference);
            showOpacity = a.getBoolean(R.styleable.ColorPickerPreference_showOpacity, true);
            a.recycle();
        }
        mShowOpacity = showOpacity;
        updateSummary();
    }

    @Override
    protected void onSetInitialValue(@Nullable Object defaultValue) {
        super.onSetInitialValue(defaultValue);
        updateSummary();
    }

    private void updateSummary() {
        int color = getPersistedColor();
        setSummary(UiTheme.toHex(color));
    }

    /** Creates the color picker dialog fragment, persisting on OK (called by AppCompatPreferenceActivity). */
    public ColorPickerDialogFragment createPickerFragment() {
        int initial = getPersistedColor();
        return ColorPickerDialogFragment.newInstance(
                initial, mShowOpacity, (color, applyOpacity, opacity) -> {
                    persistString(UiTheme.toHex(color));
                    UiTheme.get(getContext()).reload();
                    updateSummary();
                    notifyChanged();
                });
    }

    private int getPersistedColor() {
        String hex = getPersistedString("");
        if (hex.isEmpty()) {
            return getDefaultColor();
        }
        return UiTheme.parseColor(hex, getDefaultColor());
    }

    private int getDefaultColor() {
        String key = getKey();
        if (key == null) return 0xFF00DFDF;
        switch (key) {
            case UiTheme.KEY_PRIMARY: return 0xFF00DFDF;
            case UiTheme.KEY_SECONDARY: return 0xFF7CC4C4;
            case UiTheme.KEY_TERTIARY: return 0xFFC8E6E6;
            case UiTheme.KEY_BACKGROUND: return 0xFF121212;
            case UiTheme.KEY_SURFACE: return 0xFF1E1E1E;
            case UiTheme.KEY_ON_SURFACE: return 0xFFE6E1E5;
            case UiTheme.KEY_SURFACE_VARIANT: return 0xFF2A2A2A;
            case UiTheme.KEY_ON_SURFACE_VARIANT: return 0xFFCAC4CC;
            case UiTheme.KEY_ERROR: return 0xFFF2B8B5;
            default: return 0xFF00DFDF;
        }
    }

    public static int getColor(Context context, String key) {
        int fallback = getFallback(key);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        return UiTheme.parseColor(prefs.getString(key, UiTheme.toHex(fallback)), fallback);
    }

    private static int getFallback(String key) {
        if (key == null) return 0xFF00DFDF;
        switch (key) {
            case UiTheme.KEY_PRIMARY: return 0xFF00DFDF;
            case UiTheme.KEY_SECONDARY: return 0xFF7CC4C4;
            case UiTheme.KEY_TERTIARY: return 0xFFC8E6E6;
            case UiTheme.KEY_BACKGROUND: return 0xFF121212;
            case UiTheme.KEY_SURFACE: return 0xFF1E1E1E;
            case UiTheme.KEY_ON_SURFACE: return 0xFFE6E1E5;
            case UiTheme.KEY_SURFACE_VARIANT: return 0xFF2A2A2A;
            case UiTheme.KEY_ON_SURFACE_VARIANT: return 0xFFCAC4CC;
            case UiTheme.KEY_ERROR: return 0xFFF2B8B5;
            default: return 0xFF00DFDF;
        }
    }
}
