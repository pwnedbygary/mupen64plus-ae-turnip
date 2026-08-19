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

import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.RadioButton;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import androidx.appcompat.widget.AppCompatButton;
import androidx.appcompat.widget.AppCompatCheckBox;
import androidx.appcompat.widget.SwitchCompat;
import androidx.appcompat.widget.Toolbar;
import androidx.preference.PreferenceManager;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.floatingactionbutton.FloatingActionButton;
import com.google.android.material.navigation.NavigationView;

import paulscode.android.mupen64plusae.GameSidebar;
import paulscode.android.mupen64plusae.R;

/**
 * Runtime-customizable UI theme. All color slots are persisted in shared preferences and
 * applied to view hierarchies at runtime, so users can restyle the entire UI without
 * rebuilding resources.
 *
 * Color slots: primary, secondary, tertiary, background, surface, onSurface, surfaceVariant,
 * error, plus a global glass opacity and a text contrast factor.
 */
public final class UiTheme {

    // Preference keys (persisted in the app's default shared preferences)
    public static final String KEY_PRIMARY = "uiThemePrimary";
    public static final String KEY_SECONDARY = "uiThemeSecondary";
    public static final String KEY_TERTIARY = "uiThemeTertiary";
    public static final String KEY_BACKGROUND = "uiThemeBackground";
    public static final String KEY_SURFACE = "uiThemeSurface";
    public static final String KEY_ON_SURFACE = "uiThemeOnSurface";
    public static final String KEY_SURFACE_VARIANT = "uiThemeSurfaceVariant";
    public static final String KEY_ON_SURFACE_VARIANT = "uiThemeOnSurfaceVariant";
    public static final String KEY_ERROR = "uiThemeError";
    public static final String KEY_GLASS_OPACITY = "uiThemeGlassOpacity"; // 0..100
    public static final String KEY_CONTRAST = "uiThemeContrast";         // 50..200 (%)
    public static final String KEY_RESET = "uiThemeReset";

    private static UiTheme sInstance;

    private final SharedPreferences mPrefs;
    private final Context mContext;

    private int mPrimary;
    private int mSecondary;
    private int mTertiary;
    private int mBackground;
    private int mSurface;
    private int mOnSurface;
    private int mSurfaceVariant;
    private int mOnSurfaceVariant;
    private int mError;
    private float mGlassOpacity; // 0..1
    private float mContrast;     // 0.5..2.0

    private UiTheme(Context context) {
        mContext = context.getApplicationContext();
        mPrefs = PreferenceManager.getDefaultSharedPreferences(mContext);
        reload();
    }

    public static synchronized UiTheme get(Context context) {
        if (sInstance == null) {
            sInstance = new UiTheme(context);
        }
        return sInstance;
    }

    /** Re-read all color slots from preferences. Call after any UI color changes. */
    public synchronized void reload() {
        mPrimary = parseColor(mPrefs.getString(KEY_PRIMARY, "#FF00DFDF"), 0xFF00DFDF);
        mSecondary = parseColor(mPrefs.getString(KEY_SECONDARY, "#FF7CC4C4"), 0xFF7CC4C4);
        mTertiary = parseColor(mPrefs.getString(KEY_TERTIARY, "#FFC8E6E6"), 0xFFC8E6E6);
        mBackground = parseColor(mPrefs.getString(KEY_BACKGROUND, "#FF121212"), 0xFF121212);
        mSurface = parseColor(mPrefs.getString(KEY_SURFACE, "#FF1E1E1E"), 0xFF1E1E1E);
        mOnSurface = parseColor(mPrefs.getString(KEY_ON_SURFACE, "#FFE6E1E5"), 0xFFE6E1E5);
        mSurfaceVariant = parseColor(mPrefs.getString(KEY_SURFACE_VARIANT, "#FF2A2A2A"), 0xFF2A2A2A);
        mOnSurfaceVariant = parseColor(mPrefs.getString(KEY_ON_SURFACE_VARIANT, "#FFCAC4CC"), 0xFFCAC4CC);
        mError = parseColor(mPrefs.getString(KEY_ERROR, "#FFF2B8B5"), 0xFFF2B8B5);
        mGlassOpacity = clamp(mPrefs.getInt(KEY_GLASS_OPACITY, 85) / 100.0f, 0.2f, 1.0f);
        mContrast = clamp(mPrefs.getInt(KEY_CONTRAST, 100) / 100.0f, 0.5f, 2.0f);

        // Contrast adjustment: push onSurface toward white or black depending on background
        mOnSurface = applyContrast(mOnSurface, mBackground, mContrast);
        mOnSurfaceVariant = applyContrast(mOnSurfaceVariant, mBackground, mContrast);
    }

    public static int parseColor(String hex, int fallback) {
        if (hex == null) return fallback;
        try {
            return Color.parseColor(hex.startsWith("#") ? hex : "#" + hex);
        } catch (IllegalArgumentException e) {
            return fallback;
        }
    }

    public static String toHex(int color) {
        return String.format("#%08X", color);
    }

    // ---------------------------------------------------------------------
    // Public accessors
    // ---------------------------------------------------------------------

    public int primary() { return mPrimary; }
    public int secondary() { return mSecondary; }
    public int tertiary() { return mTertiary; }
    public int background() { return mBackground; }
    public int surface() { return mSurface; }
    public int onSurface() { return mOnSurface; }
    public int surfaceVariant() { return mSurfaceVariant; }
    public int onSurfaceVariant() { return mOnSurfaceVariant; }
    public int error() { return mError; }
    public float glassOpacity() { return mGlassOpacity; }
    public float contrast() { return mContrast; }

    public SharedPreferences prefs() { return mPrefs; }

    /** Contrast-aware text color that always reads well on the given background. */
    public int textOn(int background) {
        float bgLum = luminance(background);
        int base = bgLum > 0.5f ? Color.BLACK : Color.WHITE;
        int blended = blend(mOnSurface, base, Math.abs(mContrast - 1.0f) * 2.0f);
        return blended;
    }

    // ---------------------------------------------------------------------
    // Dynamic drawables
    // ---------------------------------------------------------------------

    /** Rounded frosted-glass rectangle built from the current surface + glass opacity. */
    public GradientDrawable glassRect(float radiusDp, int strokeDp, boolean subtleStroke) {
        int strokeColor = withAlpha(Color.WHITE, subtleStroke ? 12 : 20);
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.RECTANGLE);
        drawable.setCornerRadius(dp(radiusDp));
        drawable.setColor(withAlpha(mSurface, Math.round(mGlassOpacity * 255)));
        drawable.setStroke(Math.round(dp(strokeDp)), strokeColor);
        return drawable;
    }

    /** Frosted fill for cards, dialogs and the sidebar (no stroke). */
    public GradientDrawable glassFill(float radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setShape(GradientDrawable.RECTANGLE);
        drawable.setCornerRadius(dp(radiusDp));
        drawable.setColor(withAlpha(mSurface, Math.round(mGlassOpacity * 255)));
        return drawable;
    }

    // ---------------------------------------------------------------------
    // Component color state lists
    // ---------------------------------------------------------------------

    public ColorStateList switchTrackColors() {
        int enabled = withAlpha(mPrimary, 0x66);
        int disabled = withAlpha(mOnSurface, 0x1F);
        return new ColorStateList(
                new int[][] {
                        new int[]{android.R.attr.state_checked},
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{enabled, disabled, withAlpha(mOnSurface, 0x33)});
    }

    public ColorStateList switchThumbColors() {
        int on = mPrimary;
        int off = blend(mOnSurface, mSurface, 0.5f);
        return new ColorStateList(
                new int[][] {
                        new int[]{android.R.attr.state_checked},
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{on, withAlpha(mOnSurface, 0x66), off});
    }

    public ColorStateList checkboxRadioColors() {
        int checked = mPrimary;
        int disabled = withAlpha(mOnSurface, 0x33);
        return new ColorStateList(
                new int[][] {
                        new int[]{android.R.attr.state_checked},
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{checked, disabled, withAlpha(mOnSurfaceVariant, 0x99)});
    }

    public ColorStateList navItemColors() {
        return new ColorStateList(
                new int[][] {
                        new int[]{android.R.attr.state_checked},
                        new int[]{android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{mPrimary, mOnSurface, mOnSurfaceVariant});
    }

    public ColorStateList navIconColors() {
        return new ColorStateList(
                new int[][] {
                        new int[]{android.R.attr.state_checked},
                        new int[]{android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{mPrimary, mOnSurfaceVariant, mOnSurfaceVariant});
    }

    public ColorStateList buttonColors() {
        return new ColorStateList(
                new int[][] {
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{withAlpha(mPrimary, 0x66), mPrimary});
    }

    public ColorStateList seekBarColors() {
        return new ColorStateList(
                new int[][] {
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{}
                },
                new int[]{withAlpha(mPrimary, 0x66), mPrimary});
    }

    // ---------------------------------------------------------------------
    // Applying to hierarchies
    // ---------------------------------------------------------------------

    /** Apply the current theme to an activity's decor view (call after setContentView / onResume). */
    public void applyToActivity(android.app.Activity activity) {
        if (activity == null) return;
        Window window = activity.getWindow();
        if (window != null) {
            window.setStatusBarColor(mBackground);
            window.setNavigationBarColor(mBackground);
        }
        View decor = window != null ? window.getDecorView() : null;
        if (decor != null) {
            applyToView(decor);
        }
    }

    /** Apply the current theme to an open dialog's window. */
    public void applyToDialog(Dialog dialog) {
        if (dialog == null || dialog.getWindow() == null) return;
        View decor = dialog.getWindow().getDecorView();
        if (decor != null) {
            applyToView(decor);
        }
    }

    private void applyToView(View view) {
        if (view == null) return;

        // ---- Components that carry the primary accent ----
        if (view instanceof SwitchCompat || view instanceof Switch) {
            SwitchCompat switchCompat = (SwitchCompat) view;
            switchCompat.setTrackTintList(switchTrackColors());
            switchCompat.setThumbTintList(switchThumbColors());
        } else if (view instanceof AppCompatCheckBox || view instanceof CheckBox) {
            if (view instanceof AppCompatCheckBox) {
                ((AppCompatCheckBox) view).setButtonTintList(checkboxRadioColors());
            }
        } else if (view instanceof RadioButton) {
            ((RadioButton) view).setButtonTintList(checkboxRadioColors());
        } else if (view instanceof FloatingActionButton) {
            FloatingActionButton fab = (FloatingActionButton) view;
            fab.setBackgroundTintList(new ColorStateList(new int[][]{new int[]{}}, new int[]{mPrimary}));
        } else if (view instanceof MaterialButton) {
            MaterialButton button = (MaterialButton) view;
            button.setBackgroundTintList(buttonColors());
            button.setTextColor(contrastText(mPrimary));
        } else if (view instanceof AppCompatButton || view instanceof Button) {
            view.setBackgroundTintList(buttonColors());
            if (view instanceof TextView) {
                ((TextView) view).setTextColor(contrastText(mPrimary));
            }
        } else if (view instanceof SeekBar) {
            SeekBar seekBar = (SeekBar) view;
            seekBar.setProgressTintList(seekBarColors());
            seekBar.setThumbTintList(seekBarColors());
        } else if (view instanceof Toolbar) {
            Toolbar toolbar = (Toolbar) view;
            toolbar.setTitleTextColor(mOnSurface);
            toolbar.setSubtitleTextColor(mOnSurfaceVariant);
            android.graphics.drawable.Drawable navIcon = toolbar.getNavigationIcon();
            if (navIcon != null) {
                navIcon = navIcon.mutate();
                androidx.core.graphics.drawable.DrawableCompat.setTint(navIcon, mOnSurface);
                toolbar.setNavigationIcon(navIcon);
            }
            toolbar.setBackground(glassRect(0, 0, false));
        } else if (view instanceof NavigationView) {
            NavigationView nav = (NavigationView) view;
            nav.setItemTextColor(navItemColors());
            nav.setItemIconTintList(navIconColors());
        } else if (view instanceof GameSidebar) {
            styleSidebar((GameSidebar) view);
        } else if (view instanceof ImageButton) {
            // Drawer / toolbar icon buttons keep their drawable but tinted with the accent
            try {
                ((ImageButton) view).setColorFilter(mOnSurface);
            } catch (Exception ignored) {
            }
        }

        // ---- Text tinting for known static colors ----
        if (view instanceof TextView) {
            TextView textView = (TextView) view;
            tintTextView(textView);
        }

        // ---- Replace frosted-glass static backgrounds with dynamic ones ----
        replaceGlassBackground(view);

        // ---- Recurse ----
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                applyToView(group.getChildAt(i));
            }
        }
    }

    private void styleSidebar(GameSidebar sidebar) {
        // The sidebar rows inherit theme colors; recurse into children only
        ViewGroup group = (ViewGroup) sidebar;
        for (int i = 0; i < group.getChildCount(); i++) {
            applyToView(group.getChildAt(i));
        }
    }

    private void tintTextView(TextView textView) {
        if (textView.getTextColors() == null) return;
        int current = textView.getCurrentTextColor();

        // Compare against the static colors that layouts may use directly
        if (current == 0xFFE6E1E5 || current == 0xFFE6E1E5) {
            textView.setTextColor(mOnSurface);
        } else if (current == 0xFFCAC4CC) {
            textView.setTextColor(mOnSurfaceVariant);
        } else if (current == 0xFF00DFDF) {
            textView.setTextColor(mPrimary);
        }
    }

    private void replaceGlassBackground(View view) {
        Drawable background = view.getBackground();
        if (background == null) return;

        // If the background is one of our static frosted glass drawables, swap in the
        // dynamic equivalent so opacity changes apply immediately.
        int resId = mContext.getResources().getIdentifier(
                "bg_frosted_glass", "drawable", mContext.getPackageName());
        if (resId != 0) {
            Drawable.ConstantState staticState =
                    mContext.getDrawable(resId).getConstantState();
            if (background.getConstantState() != null
                    && background.getConstantState().equals(staticState)) {
                view.setBackground(glassRect(16, 1, true));
            }
        }
    }

    // ---------------------------------------------------------------------
    // Color math
    // ---------------------------------------------------------------------

    public static int withAlpha(int color, int alpha) {
        return (color & 0x00FFFFFF) | (clamp(alpha, 0, 255) << 24);
    }

    public static int blend(int from, int to, float t) {
        t = clamp(t, 0f, 1f);
        int a = Math.round(Color.alpha(from) + (Color.alpha(to) - Color.alpha(from)) * t);
        int r = Math.round(Color.red(from) + (Color.red(to) - Color.red(from)) * t);
        int g = Math.round(Color.green(from) + (Color.green(to) - Color.green(from)) * t);
        int b = Math.round(Color.blue(from) + (Color.blue(to) - Color.blue(from)) * t);
        return Color.argb(a, r, g, b);
    }

    public static float luminance(int color) {
        double r = Color.red(color) / 255.0;
        double g = Color.green(color) / 255.0;
        double b = Color.blue(color) / 255.0;
        return (float) (0.2126 * r + 0.7152 * g + 0.0722 * b);
    }

    /** Pick black or white for maximum readability on a given background. */
    public static int contrastText(int background) {
        return luminance(background) > 0.5f ? 0xFF000000 : 0xFFFFFFFF;
    }

    private int applyContrast(int onColor, int bgColor, float contrast) {
        float bgLum = luminance(bgColor);
        int opposite = bgLum > 0.5f ? Color.BLACK : Color.WHITE;
        float amount = clamp((contrast - 1.0f) * 2.0f, -1f, 1f);
        if (amount >= 0) {
            return blend(onColor, opposite, amount);
        }
        // Below 100%: pull the text toward the background (lower contrast)
        return blend(onColor, bgColor, -amount);
    }

    private float dp(float value) {
        return value * mContext.getResources().getDisplayMetrics().density;
    }

    private static float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    /** Reset all theme colors to the default palette. */
    public void resetToDefaults() {
        mPrefs.edit()
                .remove(KEY_PRIMARY)
                .remove(KEY_SECONDARY)
                .remove(KEY_TERTIARY)
                .remove(KEY_BACKGROUND)
                .remove(KEY_SURFACE)
                .remove(KEY_ON_SURFACE)
                .remove(KEY_SURFACE_VARIANT)
                .remove(KEY_ON_SURFACE_VARIANT)
                .remove(KEY_ERROR)
                .remove(KEY_GLASS_OPACITY)
                .remove(KEY_CONTRAST)
                .apply();
        reload();
    }

    public static void resetSingleton() {
        sInstance = null;
    }
}
