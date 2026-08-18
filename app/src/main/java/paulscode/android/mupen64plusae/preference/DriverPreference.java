/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * Copyright (C) 2012 Paul Lamb
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 2 of the
 * License, or (at your option) any later version.
 *
 * Mupen64PlusAE is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details. You should have received a copy of the GNU
 * General Public License along with Mupen64PlusAE. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: littleguy77
 */
package paulscode.android.mupen64plusae.preference;

import android.content.Context;
import android.net.Uri;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.view.View;
import android.widget.ArrayAdapter;

import androidx.appcompat.app.AlertDialog.Builder;
import androidx.fragment.app.FragmentActivity;
import androidx.preference.ListPreference;

import paulscode.android.mupen64plusae.R;

import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity.OnPreferenceDialogListener;

@SuppressWarnings({"unused", "RedundantSuppression"})
public class DriverPreference extends ListPreference implements OnPreferenceDialogListener
{
    public interface OnImportDriver {

        /**
         * Called when the user wants to import a driver zip
         */
        void importDriver();
    }

    private OnImportDriver mImportCallback = null;

    public DriverPreference(Context context )
    {
        super( context );
    }

    public DriverPreference(Context context, AttributeSet attrs )
    {
        super( context, attrs );
    }

    @Override
    public void onPrepareDialogBuilder( Context context, Builder builder )
    {
        populateDriverOptions(context);

        ArrayAdapter<CharSequence> adapter = new ArrayAdapter<>(
            context, R.layout.list_preference, getEntries());

        int currentIndex = findIndexOfValue(getCurrentValue());
        builder.setTitle(getTitle());
        builder.setSingleChoiceItems(adapter, currentIndex, (dialog, item) -> {
            setValue(getEntryValues()[item].toString());
            dialog.dismiss();
        });
        builder.setPositiveButton( R.string.gpuDriver_import, (dialog, which) -> {
            dialog.dismiss();
            if (mImportCallback != null) {
                mImportCallback.importDriver();
            }
        });
    }

    public void setOnImportDriverCallback(OnImportDriver onImportDriverCallback) {
        mImportCallback = onImportDriverCallback;
    }

    /**
     * Populate the list of installed drivers
     */
    public void populateDriverOptions(Context context)
    {
        ArrayList<CharSequence> entriesList = new ArrayList<>();
        ArrayList<CharSequence> valuesList = new ArrayList<>();

        entriesList.add(context.getString(R.string.gpuDriver_default));
        valuesList.add("");

        File driverDir = getDriverDir(context);
        File[] drivers = driverDir.listFiles(File::isDirectory);
        if (drivers != null) {
            for (File driver : drivers) {
                entriesList.add(driver.getName());
                valuesList.add(driver.getName());
            }
        }

        setEntries(entriesList.toArray(new CharSequence[0]));
        setEntryValues(valuesList.toArray(new CharSequence[0]));
        setValue(getPersistedString(""));
    }

    public String getCurrentValue()
    {
        return getPersistedString( "" );
    }

    /**
     * Directory where imported drivers are stored
     */
    public static File getDriverDir(Context context)
    {
        return new File(context.getFilesDir(), "driver");
    }

    /**
     * Sanitize a driver name for use as a directory name
     */
    public static String sanitizeDriverName(String driverName)
    {
        String sanitized = driverName.replaceAll("[^a-zA-Z0-9._-]", "_");
        return sanitized.length() > 0 ? sanitized : "driver";
    }

    /**
     * Import a driver zip, returning {sanitizedDriverName, libraryName} on success or null on failure
     */
    public static String[] importDriver(Context context, Uri uri)
    {
        String driverName = null;
        String libraryName = null;

        try (InputStream inputStream = context.getContentResolver().openInputStream(uri);
             ZipInputStream zipStream = new ZipInputStream(inputStream)) {

            ZipEntry entry;
            while ((entry = zipStream.getNextEntry()) != null) {
                String name = entry.getName();
                if (name == null || name.contains("..")) {
                    continue;
                }

                if (!entry.isDirectory() && name.equals("meta.json")) {
                    StringBuilder jsonBuilder = new StringBuilder();
                    byte[] buffer = new byte[4096];
                    int read;
                    while ((read = zipStream.read(buffer)) > 0) {
                        jsonBuilder.append(new String(buffer, 0, read));
                    }
                    JSONObject meta = new JSONObject(jsonBuilder.toString());
                    driverName = meta.optString("driverName", "");
                    libraryName = meta.optString("libraryName", "");
                }
            }
        } catch (Exception e) {
            return null;
        }

        if (TextUtils.isEmpty(driverName) || TextUtils.isEmpty(libraryName)) {
            return null;
        }

        String sanitizedName = sanitizeDriverName(driverName);
        File destination = new File(getDriverDir(context), sanitizedName);

        try (InputStream inputStream = context.getContentResolver().openInputStream(uri);
             ZipInputStream zipStream = new ZipInputStream(inputStream)) {

            ZipEntry entry;
            while ((entry = zipStream.getNextEntry()) != null) {
                String name = entry.getName();
                if (name == null || name.contains("..") || entry.isDirectory()) {
                    continue;
                }

                File outputFile = new File(destination, name);
                File parentDir = outputFile.getParentFile();
                if (parentDir != null) {
                    parentDir.mkdirs();
                }

                try (FileOutputStream outputStream = new FileOutputStream(outputFile)) {
                    byte[] buffer = new byte[4096];
                    int read;
                    while ((read = zipStream.read(buffer)) > 0) {
                        outputStream.write(buffer, 0, read);
                    }
                }
            }
        } catch (IOException e) {
            return null;
        }

        if (!new File(destination, libraryName).isFile()) {
            return null;
        }

        return new String[] { sanitizedName, libraryName };
    }

    @Override
    public void onBindDialogView(View view, FragmentActivity associatedActivity)
    {
        //Nothing to do here
    }

    @Override
    public void onDialogClosed(boolean result)
    {
        //Nothing to do here
    }
}