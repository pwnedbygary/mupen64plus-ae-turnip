package paulscode.android.mupen64plusae.dialog;

import android.content.Context;

import paulscode.android.mupen64plusae.R;

/**
 * Centralized factory for app dialogs that match the currently selected theme preset.
 *
 * The active {@link UiTheme} runtime-preset colors are applied by callers via applyToDialog() on the
 * created dialog (see ColorPickerDialogFragment, AppCompatPreferenceActivity). This helper only lays down the
 * structural themed styling: frosted-glass background, 24dp rounded corners and app-colored buttons. It is a
 * thin wrapper over an appcompat {@link androidx.appcompat.app.AlertDialog.Builder} pre-styled with
 * {@code R.style.Theme_Mupen64PlusAE_AlertDialog}, so every dialog regardless of the surrounding activity's
 * theme ends up consistent.
 */

public final class ThemedAlertDialog {

    private ThemedAlertDialog() {}

    /** Returns an appcompat AlertDialog.Builder themed to match the app preset (frosted glass + rounded corners). */
    public static androidx.appcompat.app.AlertDialog.Builder newBuilder(Context context) {
        return new androidx.appcompat.app.AlertDialog.Builder(context, R.style.Theme_Mupen64PlusAE_AlertDialog);
    }

    /** Convenience: build and immediately show a themed dialog. */
    public static void show(Context context, CharSequence title, CharSequence message) {
        newBuilder(context).setTitle(title).setMessage(message)
                .setPositiveButton(android.R.string.ok, null).create().show();
    }
}
