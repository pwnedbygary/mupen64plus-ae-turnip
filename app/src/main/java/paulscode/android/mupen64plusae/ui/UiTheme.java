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
import android.graphics.drawable.LayerDrawable;
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

import java.util.Arrays;
import java.util.List;

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
    public static final String KEY_CARD_GLOW = "uiThemeCardGlow";        // 0..100 (%)
    public static final String KEY_RESET = "uiThemeReset";
    public static final String KEY_PRESET = "uiThemePreset";

    /**
     * A named colorscheme that fills every theme slot at once. Modeled after
     * popular terminal colorschemes.
     */
    public static final class Preset {
        public final String key;
        public final String name;
        public final int primary;
        public final int secondary;
        public final int tertiary;
        public final int error;
        public final int background;
        public final int surface;
        public final int surfaceVariant;
        public final int onSurface;
        public final int onSurfaceVariant;
        public final int glassOpacity; // 20..100
        public final int contrast;     // 50..200 (%)
        public final int cardGlow;     // 0..100 (%)

        private Preset(String key, String name,
                       int primary, int secondary, int tertiary, int error,
                       int background, int surface, int surfaceVariant,
                       int onSurface, int onSurfaceVariant,
                       int glassOpacity, int contrast, int cardGlow) {
            this.key = key;
            this.name = name;
            this.primary = primary;
            this.secondary = secondary;
            this.tertiary = tertiary;
            this.error = error;
            this.background = background;
            this.surface = surface;
            this.surfaceVariant = surfaceVariant;
            this.onSurface = onSurface;
            this.onSurfaceVariant = onSurfaceVariant;
            this.glassOpacity = glassOpacity;
            this.contrast = contrast;
            this.cardGlow = cardGlow;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    /**
     * Built-in colorschemes. Keys are stable (persisted in uiThemePreset);
     * keep them unique.
     */
    public static final List<Preset> PRESETS = Arrays.asList(
            new Preset("default", "Default (Teal)",
                    0xFF00DFDF, 0xFF7CC4C4, 0xFFC8E6E6, 0xFFF2B8B5,
                    0xFF121212, 0xFF1E1E1E, 0xFF2A2A2A, 0xFFE6E1E5, 0xFFCAC4CC,
                    85, 100, 45),
            new Preset("romm", "ROMM (Neon Purple)",
                    0xFF8B5CF6, 0xFF6C5CE7, 0xFFB8A9FF, 0xFFFF6B6B,
                    0xFF0A0A0A, 0xFF15151C, 0xFF20202A, 0xFFFFFFFF, 0xFFB0B0BC,
                    95, 100, 85),
            new Preset("synthwave", "Synthwave '84",
                    0xFFFF1493, 0xFF00F9FF, 0xFFFFE600, 0xFFFF3B30,
                    0xFF140827, 0xFF220E40, 0xFF30155A, 0xFFFFFFFF, 0xFFD2BFF0,
                    90, 100, 95),
            new Preset("cyberpunk", "Cyberpunk Neon",
                    0xFF00F0FF, 0xFFF000FF, 0xFFF8E71C, 0xFFFF003C,
                    0xFF090217, 0xFF140A2E, 0xFF221347, 0xFFE0F7FA, 0xFFA5B8D8,
                    90, 100, 95),
            new Preset("atomic_purple", "Atomic Purple (N64)",
                    0xFF9D4EDD, 0xFF06D6A0, 0xFFC77DFF, 0xFFEF476F,
                    0xFF100720, 0xFF1C0E38, 0xFF2B1652, 0xFFEDE7F6, 0xFFBFAADB,
                    80, 100, 90),
            new Preset("jungle_green", "Jungle Green (N64)",
                    0xFF00E676, 0xFFFFD600, 0xFF00E5FF, 0xFFFF5252,
                    0xFF07140B, 0xFF0F2617, 0xFF193B24, 0xFFF1F8E9, 0xFFA5D6A7,
                    85, 100, 90),
            new Preset("ice_blue", "Ice Blue (N64)",
                    0xFF00F5D4, 0xFF00BBF9, 0xFFE0FBFC, 0xFFFF4D6D,
                    0xFF04101C, 0xFF0B1E33, 0xFF142E4C, 0xFFE0F2FE, 0xFF7DD3FC,
                    85, 100, 90),
            new Preset("fire_orange", "Fire Orange (N64)",
                    0xFFFF6B35, 0xFFF7B801, 0xFFFF3D00, 0xFFD62828,
                    0xFF140703, 0xFF241008, 0xFF381B10, 0xFFFFF3E0, 0xFFFFB74D,
                    85, 100, 90),
            new Preset("smoke_black", "Smoke Black (N64)",
                    0xFFE2E8F0, 0xFF718096, 0xFF63B3ED, 0xFFE53E3E,
                    0xFF0A0A0C, 0xFF16171B, 0xFF23252B, 0xFFF8FAFC, 0xFF94A3B8,
                    80, 100, 75),
            new Preset("majoras_mask", "Majora's Mask",
                    0xFFB100FF, 0xFFFF0055, 0xFFFFE500, 0xFF00D4FF,
                    0xFF0E041C, 0xFF1B0A33, 0xFF2B124F, 0xFFFAF5FF, 0xFFD8B4FE,
                    90, 100, 95),
            new Preset("ocarina_gold", "Ocarina Gold",
                    0xFFFFD700, 0xFF10B981, 0xFF3B82F6, 0xFFDC2626,
                    0xFF141006, 0xFF241D0D, 0xFF382E16, 0xFFFEF9C3, 0xFFFDE047,
                    85, 100, 85),
            new Preset("fzero", "F-Zero Mute City",
                    0xFF3B82F6, 0xFFF43F5E, 0xFFFBBF24, 0xFFEF4444,
                    0xFF060A18, 0xFF0E1733, 0xFF1A2854, 0xFFF8FAFC, 0xFF93C5FD,
                    90, 100, 90),
            new Preset("starfox", "Star Fox Sector X",
                    0xFF00D2FF, 0xFFFF7A00, 0xFFE2E8F0, 0xFFFF2A2A,
                    0xFF030814, 0xFF0A152B, 0xFF14244A, 0xFFE0F2FE, 0xFF7DD3FC,
                    85, 100, 90),
            new Preset("matrix", "Matrix Terminal",
                    0xFF00FF66, 0xFF33CC66, 0xFFB3FFB3, 0xFFFF3333,
                    0xFF020A04, 0xFF05170A, 0xFF0D2913, 0xFFE0FFE5, 0xFF66BB6A,
                    90, 100, 90),
            new Preset("gameboy", "Game Boy Classic",
                    0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF8B0000,
                    0xFF0D1B0D, 0xFF172C17, 0xFF223E22, 0xFF9BBC0F, 0xFF8BAC0F,
                    90, 100, 80),
            new Preset("sakura", "Sakura Bloom",
                    0xFFFF70A6, 0xFFFF9770, 0xFFFFD670, 0xFFE63946,
                    0xFF1A0C16, 0xFF281422, 0xFF3B1E32, 0xFFFFF0F5, 0xFFF4B8DA,
                    85, 100, 85),
            new Preset("oled_black", "OLED Pure Black",
                    0xFFFFFFFF, 0xFF38BDF8, 0xFF818CF8, 0xFFEF4444,
                    0xFF000000, 0xFF0D0D0D, 0xFF1A1A1A, 0xFFFFFFFF, 0xFFA1A1AA,
                    95, 100, 80),
            new Preset("tokyonight", "Tokyo Night",
                    0xFF7AA2F7, 0xFF7DCFFF, 0xFFBB9AF7, 0xFFF7768E,
                    0xFF1A1B26, 0xFF16161E, 0xFF292E42, 0xFFC0CAF5, 0xFFA9B1D6,
                    85, 100, 65),
            new Preset("dracula", "Dracula",
                    0xFFBD93F9, 0xFFFF79C6, 0xFF8BE9FD, 0xFFFF5555,
                    0xFF282A36, 0xFF343746, 0xFF44475A, 0xFFF8F8F2, 0xFF6272A4,
                    85, 100, 70),
            new Preset("catppuccin", "Catppuccin Mocha",
                    0xFF89B4FA, 0xFFA6E3A1, 0xFFF5C2E7, 0xFFF38BA8,
                    0xFF1E1E2E, 0xFF313244, 0xFF45475A, 0xFFCDD6F4, 0xFFA6ADC8,
                    85, 100, 60),
            new Preset("nord", "Nord",
                    0xFF88C0D0, 0xFF81A1C1, 0xFFB48EAD, 0xFFBF616A,
                    0xFF2E3440, 0xFF3B4252, 0xFF434C5E, 0xFFECEFF4, 0xFFD8DEE9,
                    85, 100, 45),
            new Preset("monokai", "Monokai",
                    0xFFA6E22E, 0xFF66D9EF, 0xFFE6DB74, 0xFFF92672,
                    0xFF272822, 0xFF2B2C26, 0xFF3E3D32, 0xFFF8F8F2, 0xFF75715E,
                    85, 100, 65),
            new Preset("onedark", "One Dark",
                    0xFF61AFEF, 0xFF98C379, 0xFFE5C07B, 0xFFE06C75,
                    0xFF282C34, 0xFF21252B, 0xFF2C313A, 0xFFABB2BF, 0xFF5C6370,
                    85, 100, 50),
            new Preset("gruvbox", "Gruvbox Dark",
                    0xFFFE8019, 0xFFFABD2F, 0xFF8EC07C, 0xFFFB4934,
                    0xFF282828, 0xFF3C3836, 0xFF504945, 0xFFEBDDB2, 0xFFA89984,
                    85, 100, 55),
            new Preset("solarized", "Solarized Dark",
                    0xFF268BD2, 0xFF2AA198, 0xFFB58900, 0xFFDC322F,
                    0xFF002B36, 0xFF073642, 0xFF12414C, 0xFF93A1A1, 0xFF657B83,
                    85, 100, 40),
            new Preset("githubdark", "GitHub Dark",
                    0xFF58A6FF, 0xFF3FB950, 0xFFD29922, 0xFFF85149,
                    0xFF0D1117, 0xFF161B22, 0xFF21262D, 0xFFC9D1D9, 0xFF8B949E,
                    85, 100, 50),
            new Preset("adwaita", "Adwaita Dark",
                    0xFF3584E4, 0xFF33D17A, 0xFF9141AC, 0xFFFF7B63,
                    0xFF1E1E1E, 0xFF2E3436, 0xFF3D3846, 0xFFFFFFFF, 0xFF9A9996,
                    85, 100, 45));

    /** Look up a preset by key; unknown keys fall back to the default palette. */
    public static Preset getPreset(String key) {
        if (key != null) {
            for (Preset preset : PRESETS) {
                if (preset.key.equals(key)) {
                    return preset;
                }
            }
        }
        return PRESETS.get(0);
    }

    /** Persist a colorscheme into every theme slot and reload. */
    public void applyPreset(Preset preset) {
        mPrefs.edit()
                .putString(KEY_PRIMARY, toHex(preset.primary))
                .putString(KEY_SECONDARY, toHex(preset.secondary))
                .putString(KEY_TERTIARY, toHex(preset.tertiary))
                .putString(KEY_ERROR, toHex(preset.error))
                .putString(KEY_BACKGROUND, toHex(preset.background))
                .putString(KEY_SURFACE, toHex(preset.surface))
                .putString(KEY_SURFACE_VARIANT, toHex(preset.surfaceVariant))
                .putString(KEY_ON_SURFACE, toHex(preset.onSurface))
                .putString(KEY_ON_SURFACE_VARIANT, toHex(preset.onSurfaceVariant))
                .putInt(KEY_GLASS_OPACITY, preset.glassOpacity)
                .putInt(KEY_CONTRAST, preset.contrast)
                .putInt(KEY_CARD_GLOW, preset.cardGlow)
                .putString(KEY_PRESET, preset.key)
                .apply();
        reload();
    }

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
    private float mCardGlow;     // 0..1

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
        mCardGlow = clamp(mPrefs.getInt(KEY_CARD_GLOW, 45) / 100.0f, 0f, 1f);

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
    public float cardGlow() { return mCardGlow; }

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

    /**
     * Neon outline for game cards: a soft outer ring plus a crisp inner ring,
     * both in the primary accent. Returns null when glow is disabled.
     */
    public Drawable cardGlowDrawable(float radiusDp) {
        return new NeonGlowDrawable(mPrimary, radiusDp, getDisplayDensity(), mCardGlow, mGlassOpacity);
    }

    private float getDisplayDensity() {
        return mContext != null ? mContext.getResources().getDisplayMetrics().density : 1f;
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

        // Refresh the neon glow overlay on gallery game cards
        if (view.getId() == R.id.galleryCardGlow) {
            android.graphics.drawable.Drawable glow = cardGlowDrawable(20);
            if (glow != null) {
                view.setBackground(glow);
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
                .remove(KEY_CARD_GLOW)
                .remove(KEY_PRESET)
                .apply();
        reload();
    }

    public static void resetSingleton() {
        sInstance = null;
    }
}
