/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
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
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.view.WindowManager;

import androidx.preference.EditTextPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceManager;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.dialog.ThemedAlertDialog;
import paulscode.android.mupen64plusae.jni.RaSignInRules;
import paulscode.android.mupen64plusae.jni.RetroAchievementsManager;
import paulscode.android.mupen64plusae.persistent.AppData;
import paulscode.android.mupen64plusae.persistent.GlobalPrefs;

/**
 * "Connect" row on the RetroAchievements screen. Attempts a login with the entered
 * credentials, or with the saved session token once the password has been deleted, and
 * reports the server's answer, so the user is not left guessing whether their username and
 * password are correct. On success the session token is stored and the password deleted, so
 * subsequent game sessions log in with the token.
 */
public class LoginPreference extends Preference implements RetroAchievementsManager.Listener
{
    private RaSignInRules.Credential mCredential = RaSignInRules.Credential.NONE;

    public LoginPreference( Context context )
    {
        super( context );
        init();
    }

    public LoginPreference( Context context, AttributeSet attrs )
    {
        super( context, attrs );
        init();
    }

    public LoginPreference( Context context, AttributeSet attrs, int defStyleAttr )
    {
        super( context, attrs, defStyleAttr );
        init();
    }

    private void init()
    {
        setOnPreferenceClickListener( preference -> {
            connect();
            return true;
        } );
    }

    private void connect()
    {
        Context context = getContext();
        GlobalPrefs prefs = new GlobalPrefs( context, new AppData( context ) );
        String username = prefs.retroAchievementsUsername;
        String password = prefs.retroAchievementsPassword;
        String token = prefs.retroAchievementsToken;

        RaSignInRules.Credential credential =
                RaSignInRules.connectCredential( username, password, token );
        if( credential == RaSignInRules.Credential.NONE )
        {
            show( context, context.getString( R.string.retroAchievementsConnect_missing ) );
            return;
        }

        RetroAchievementsManager ra = RetroAchievementsManager.getInstance();
        if( !ra.initialize() )
        {
            show( context, context.getString( R.string.retroAchievementsConnect_error,
                    ra.getLastError() ) );
            return;
        }

        mCredential = credential;
        ra.addListener( this );
        boolean started = credential == RaSignInRules.Credential.PASSWORD
                ? ra.login( username, password )
                : ra.loginWithToken( username, token );
        if( !started )
        {
            ra.removeListener( this );
            show( context, context.getString( R.string.retroAchievementsConnect_error,
                    context.getString( R.string.retroAchievementsConnect_noRequest ) ) );
        }
    }

    @Override
    public void onRaEvent( String type, String json )
    {
        // Not needed for a connection test
    }

    @Override
    public void onRaAsyncResult( int result, String errorMessage )
    {
        RetroAchievementsManager ra = RetroAchievementsManager.getInstance();
        ra.removeListener( this );

        Context context = getContext();
        if( result == RetroAchievementsManager.RESULT_OK )
        {
            String token = ra.getUserToken();
            if( !TextUtils.isEmpty( token ) )
            {
                SharedPreferences preferences =
                        PreferenceManager.getDefaultSharedPreferences( context );
                boolean forgetPassword = RaSignInRules.forgetsPassword( token, preferences
                        .getString( GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_PASSWORD, null ) );
                SharedPreferences.Editor editor = preferences.edit()
                        .putString( GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_TOKEN, token );
                if( forgetPassword )
                {
                    editor.remove( GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_PASSWORD );
                }
                editor.commit();

                if( forgetPassword )
                {
                    clearPasswordField();
                }
            }

            String name = ra.getUserName();
            if( TextUtils.isEmpty( name ) )
            {
                GlobalPrefs prefs = new GlobalPrefs( context, new AppData( context ) );
                name = prefs.retroAchievementsUsername;
            }
            show( context, context.getString( R.string.retroAchievementsConnect_ok, name ) );
        }
        else if( RaSignInRules.tokenRejected( mCredential, result ) )
        {
            show( context, context.getString( R.string.retroAchievementsConnect_error,
                    context.getString( R.string.ra_passwordNeeded ) ) );
        }
        else
        {
            show( context, context.getString( R.string.retroAchievementsConnect_error,
                    errorMessage != null ? errorMessage : "" ) );
        }
    }

    // The field keeps the text it last showed, and confirming its dialog would save the deleted
    // password again.
    private void clearPasswordField()
    {
        PreferenceManager manager = getPreferenceManager();
        Preference field = manager != null
                ? manager.findPreference( GlobalPrefs.KEY_RETRO_ACHIEVEMENTS_PASSWORD ) : null;
        if( field instanceof EditTextPreference )
        {
            ( (EditTextPreference) field ).setText( null );
        }
    }

    @Override
    public void onDetached()
    {
        super.onDetached();

        // The result can arrive after this row's screen is gone; stop listening so a destroyed
        // activity is never touched and this instance is not retained by the process-wide client.
        // peekInstance() avoids constructing the client (and loading the native library) just to
        // detach when Connect was never used.
        RetroAchievementsManager ra = RetroAchievementsManager.peekInstance();
        if( ra != null )
        {
            ra.removeListener( this );
        }
    }

    private void show( Context context, String message )
    {
        // The RA listener can deliver its result long after the click. If the user left or
        // rotated the screen meanwhile, the host activity is finishing/destroyed and building
        // a dialog on it throws BadTokenException.
        Activity activity = findHostActivity( context );
        if( activity != null && ( activity.isFinishing() || activity.isDestroyed() ) )
        {
            return;
        }

        try
        {
            ThemedAlertDialog.show( context,
                    context.getString( R.string.retroAchievementsConnect_title ), message );
        }
        catch( WindowManager.BadTokenException e )
        {
            // The host activity was destroyed between the liveness check and show(); drop quietly.
        }
    }

    /** @return the Activity hosting this preference, unwrapping theme contexts, or null. */
    private static Activity findHostActivity( Context context )
    {
        while( context instanceof ContextWrapper )
        {
            if( context instanceof Activity )
            {
                return (Activity) context;
            }
            context = ((ContextWrapper) context).getBaseContext();
        }
        return null;
    }
}
