package paulscode.android.mupen64plusae.persistent;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;

import androidx.core.content.ContextCompat;
import androidx.preference.PreferenceManager;

/**
 * Passes a RetroAchievements sign-in that one of the app's processes saved to its other process.
 * Each process keeps its own copy of the settings and writes the whole copy whenever it saves a
 * value, so a copy that missed the new session token and the deleted password would bring the old
 * ones back.
 */
public class RaCredentialSync extends BroadcastReceiver {

    private static final String ACTION_SUFFIX = ".RA_SIGN_IN_SAVED";
    private static final String EXTRA_TOKEN = "token";
    private static final String EXTRA_PASSWORD_DELETED = "passwordDeleted";

    /** Starts listening, for the life of this process, for sign-ins another process saves. */
    public static void register(Context context) {
        ContextCompat.registerReceiver(context, new RaCredentialSync(),
                new IntentFilter(action(context)), ContextCompat.RECEIVER_NOT_EXPORTED);
    }

    /**
     * Tells the app's other process about a sign-in this process has saved.
     *
     * @param token the session token that was saved
     * @param passwordDeleted whether the same save deleted the password
     */
    public static void send(Context context, String token, boolean passwordDeleted) {
        // When the sending app targets Android 14 or later, Android 14 leaves not-exported
        // receivers such as this one out of a broadcast that names no package.
        context.sendBroadcast(new Intent(action(context))
                .setPackage(context.getPackageName())
                .putExtra(EXTRA_TOKEN, token)
                .putExtra(EXTRA_PASSWORD_DELETED, passwordDeleted));
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        apply(PreferenceManager.getDefaultSharedPreferences(context),
                intent.getStringExtra(EXTRA_TOKEN),
                intent.getBooleanExtra(EXTRA_PASSWORD_DELETED, false));
    }

    /**
     * Makes this process's copy of the settings hold the saved sign-in. The password is deleted
     * only together with a token, so the copy never ends up with neither.
     *
     * @param preferences this process's settings
     * @param token the session token the other process saved
     * @param passwordDeleted whether the other process deleted the password
     * @return whether the copy changed and was saved
     */
    static boolean apply(SharedPreferences preferences, String token, boolean passwordDeleted) {
        if (token == null || token.isEmpty()) {
            return false;
        }
        boolean newToken = !token.equals(
                preferences.getString(GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_TOKEN, null));
        boolean deletePassword = passwordDeleted
                && preferences.contains(GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_PASSWORD);
        if (!newToken && !deletePassword) {
            return false;
        }
        SharedPreferences.Editor editor = preferences.edit()
                .putString(GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_TOKEN, token);
        if (deletePassword) {
            editor.remove(GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_PASSWORD);
        }
        editor.apply();
        return true;
    }

    private static String action(Context context) {
        return context.getPackageName() + ACTION_SUFFIX;
    }
}
