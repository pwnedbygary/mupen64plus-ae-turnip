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
package paulscode.android.mupen64plusae.persistent;

import android.content.Context;
import android.os.Bundle;
import android.text.TextUtils;

import paulscode.android.mupen64plusae.R;

import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity;
import paulscode.android.mupen64plusae.util.LocaleContextWrapper;

/**
 * RetroAchievements settings (enable, credentials, hardcore). Values are read
 * by {@link GlobalPrefs}; the running client is driven by
 * {@code paulscode.android.mupen64plusae.jni.RetroAchievementsManager}.
 */
public class RaPrefsActivity extends AppCompatPreferenceActivity
{
    private GlobalPrefs mGlobalPrefs = null;

    @Override
    protected void attachBaseContext(Context newBase) {
        if(TextUtils.isEmpty(LocaleContextWrapper.getLocalCode()))
        {
            super.attachBaseContext(newBase);
        }
        else
        {
            super.attachBaseContext(LocaleContextWrapper.wrap(newBase,LocaleContextWrapper.getLocalCode()));
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        AppData appData = new AppData(this);
        mGlobalPrefs = new GlobalPrefs(this, appData);
    }

    @Override
    protected String getSharedPrefsName() {
        return null;
    }

    @Override
    protected int getSharedPrefsId()
    {
        return R.xml.preferences_ra;
    }

    @Override
    protected void OnPreferenceScreenChange(String key)
    {
        // Manager synchronization (login, hardcore) is wired by the emulation
        // lifecycle; preferences alone do not start the client.
    }
}
