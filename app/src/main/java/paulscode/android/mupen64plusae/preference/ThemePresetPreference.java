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

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import androidx.preference.ListPreference;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity.OnPreferenceDialogListener;
import paulscode.android.mupen64plusae.ui.UiTheme;

import java.util.List;

/**
 * Applies a complete colorscheme (terminal-style preset) with one tap.
 * Shows a swatch preview of each preset's palette in the picker dialog.
 */
@SuppressWarnings({"unused", "RedundantSuppression"})
public class ThemePresetPreference extends ListPreference implements OnPreferenceDialogListener {

    public ThemePresetPreference(Context context) {
        super(context);
    }

    public ThemePresetPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    public void onPrepareDialogBuilder(Context context, androidx.appcompat.app.AlertDialog.Builder builder) {
        final List<UiTheme.Preset> presets = UiTheme.PRESETS;
        final String[] keys = new String[presets.size()];
        final CharSequence[] names = new CharSequence[presets.size()];
        for (int i = 0; i < presets.size(); i++) {
            keys[i] = presets.get(i).key;
            names[i] = presets.get(i).name;
        }
        setEntries(names);
        setEntryValues(keys);

        ArrayAdapter<UiTheme.Preset> adapter = new ArrayAdapter<UiTheme.Preset>(
                context, R.layout.list_item_preset, R.id.presetName, presets) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                View view = super.getView(position, convertView, parent);
                UiTheme.Preset preset = getItem(position);
                TextView nameView = view.findViewById(R.id.presetName);
                if (nameView != null) {
                    nameView.setText(preset.name);
                }
                ViewGroup swatches = view.findViewById(R.id.presetSwatches);
                int[] colors = {preset.primary, preset.secondary, preset.tertiary, preset.background};
                for (int i = 0; i < colors.length && i < swatches.getChildCount(); i++) {
                    swatches.getChildAt(i).setBackgroundColor(colors[i]);
                }
                return view;
            }
        };

        builder.setTitle(R.string.uiThemePresets_title);
        builder.setAdapter(adapter, (dialog, which) -> {
            UiTheme.Preset preset = presets.get(which);
            UiTheme.get(context).applyPreset(preset);
            dialog.dismiss();
            // Refresh the settings screen so every summary shows the new values
            if (context instanceof Activity) {
                final Activity activity = (Activity) context;
                new Handler(Looper.getMainLooper()).post(activity::recreate);
            }
        });
        builder.setNegativeButton(android.R.string.cancel, null);
    }

    @Override
    public void onDialogClosed(boolean positiveResult) {
        // Selection is handled by the dialog's item-click listener
    }

    @Override
    public void onBindDialogView(View view, androidx.fragment.app.FragmentActivity associatedActivity) {
        // The dialog is fully built in onPrepareDialogBuilder
    }
}