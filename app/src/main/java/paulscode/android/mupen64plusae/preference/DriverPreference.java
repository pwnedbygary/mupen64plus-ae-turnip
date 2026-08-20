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
import android.content.SharedPreferences;
import android.net.Uri;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.CheckedTextView;
import android.widget.TextView;

import androidx.preference.ListPreference;

import paulscode.android.mupen64plusae.R;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity.OnPreferenceDialogListener;
import paulscode.android.mupen64plusae.util.FileUtil;
import paulscode.android.mupen64plusae.util.Notifier;

@SuppressWarnings({"unused", "RedundantSuppression"})
public class DriverPreference extends ListPreference implements OnPreferenceDialogListener
{
    public interface OnImportDriver {

        /**
         * Called when the user wants to import a driver zip
         */
        void importDriver();
    }

    public interface OnDownloadDriver {

        /**
         * Called when the user wants to download a driver zip
         */
        void downloadDriver();
    }

    private OnImportDriver mImportCallback = null;
    private OnDownloadDriver mDownloadCallback = null;

    public DriverPreference(Context context )
    {
        super( context );
    }

    public DriverPreference(Context context, AttributeSet attrs )
    {
        super( context, attrs );
    }

    @Override
    public void onPrepareDialogBuilder( Context context, androidx.appcompat.app.AlertDialog.Builder builder )
    {
        populateDriverOptions(context);

        // Custom title with device GPU info as a subtitle
        View titleView = View.inflate(context, R.layout.dialog_title_driver, null);
        TextView subtitleView = titleView.findViewById(R.id.subtitle);
        String gpuModel = getGpuModel();
        if (!TextUtils.isEmpty(gpuModel)) {
            subtitleView.setText(context.getString(R.string.gpuDriver_deviceInfo, gpuModel));
        } else {
            subtitleView.setVisibility(View.GONE);
        }
        builder.setCustomTitle(titleView);

        ArrayAdapter<DriverRow> adapter = new DriverRowAdapter(context, mDriverRows);

        int currentIndex = findIndexOfValue(getCurrentValue());
        builder.setSingleChoiceItems(adapter, currentIndex, (dialog, item) -> {
            if (mDriverRows.get(item).isHeader) {
                return;
            }
            String val = getEntryValues()[item].toString();
            if (callChangeListener(val)) {
                setValue(val);
                syncGlobalDriverPrefs();
                notifyChanged();
            }
            dialog.dismiss();
        });
        builder.setPositiveButton( R.string.gpuDriver_import, (dialog, which) -> {
            dialog.dismiss();
            if (mImportCallback != null) {
                mImportCallback.importDriver();
            }
        });
        builder.setNegativeButton( R.string.gpuDriver_download, (dialog, which) -> {
            dialog.dismiss();
            if (mDownloadCallback != null) {
                mDownloadCallback.downloadDriver();
            }
        });
        builder.setNeutralButton( R.string.gpuDriver_delete, (dialog, which) -> {
            dialog.dismiss();
            deleteSelectedDriver();
        });
    }

    public void setOnImportDriverCallback(OnImportDriver onImportDriverCallback) {
        mImportCallback = onImportDriverCallback;
    }

    public void setOnDownloadDriverCallback(OnDownloadDriver onDownloadDriverCallback) {
        mDownloadCallback = onDownloadDriverCallback;
    }

    /**
     * Populate the list of installed drivers, grouped under section headers.
     * The stock system driver is always offered as the first choice.
     */
    public void populateDriverOptions(Context context)
    {
        ArrayList<CharSequence> entriesList = new ArrayList<>();
        ArrayList<CharSequence> valuesList = new ArrayList<>();
        mDriverRows = new ArrayList<>();

        entriesList.add(context.getString(R.string.gpuDriver_sectionSystem).toUpperCase());
        valuesList.add(HEADER_SENTINEL);
        mDriverRows.add(new DriverRow(
                context.getString(R.string.gpuDriver_sectionSystem).toUpperCase(), "", true));

        entriesList.add(context.getString(R.string.gpuDriver_default));
        valuesList.add("");
        mDriverRows.add(new DriverRow(context.getString(R.string.gpuDriver_default), "", false));

        File driverDir = getDriverDir(context);
        File[] drivers = driverDir.listFiles(File::isDirectory);
        if (drivers != null && drivers.length > 0) {
            entriesList.add(context.getString(R.string.gpuDriver_sectionInstalled).toUpperCase());
            valuesList.add(HEADER_SENTINEL);
            mDriverRows.add(new DriverRow(
                    context.getString(R.string.gpuDriver_sectionInstalled).toUpperCase(), "", true));

            for (File driver : drivers) {
                String name = driver.getName();
                entriesList.add(name);
                valuesList.add(name);
                mDriverRows.add(new DriverRow(name, getDriverDetails(context, driver), false));
            }
        }

        setEntries(entriesList.toArray(new CharSequence[0]));
        setEntryValues(valuesList.toArray(new CharSequence[0]));
        setValue(getPersistedString(""));
    }

    /**
     * Adapter row data for a single installed driver
     */
    private static class DriverRow {
        final String name;
        final String details;
        final boolean isHeader;

        DriverRow(String name, String details, boolean isHeader) {
            this.name = name;
            this.details = details;
            this.isHeader = isHeader;
        }
    }

    private static class DriverRowAdapter extends ArrayAdapter<DriverRow>
    {
        DriverRowAdapter(Context context, List<DriverRow> rows)
        {
            super(context, R.layout.list_item_driver, rows);
        }

        @Override
        public int getViewTypeCount()
        {
            return 2;
        }

        @Override
        public int getItemViewType(int position)
        {
            return getItem(position).isHeader ? 0 : 1;
        }

        @Override
        public boolean areAllItemsEnabled()
        {
            return false;
        }

        @Override
        public boolean isEnabled(int position)
        {
            return !getItem(position).isHeader;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent)
        {
            DriverRow row = getItem(position);
            if (row.isHeader) {
                if (convertView == null || convertView.getTag() == null) {
                    convertView = LayoutInflater.from(getContext()).inflate(
                            R.layout.list_item_driver_header, parent, false);
                    convertView.setTag(Boolean.TRUE);
                }
                ((android.widget.TextView) convertView).setText(row.name);
                return convertView;
            }

            if (convertView == null || convertView.getTag() != null) {
                convertView = LayoutInflater.from(getContext()).inflate(R.layout.list_item_driver, parent, false);
                convertView.setTag(null);
            }

            CheckedTextView text1 = (CheckedTextView) convertView;
            if (TextUtils.isEmpty(row.details)) {
                text1.setText(row.name);
            } else {
                String text = row.name + "\n" + row.details;
                SpannableString spannable = new SpannableString(text);
                spannable.setSpan(new ForegroundColorSpan(0x8AFFFFFF), row.name.length() + 1, text.length(),
                        Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                spannable.setSpan(new RelativeSizeSpan(0.85f), row.name.length() + 1, text.length(),
                        Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                text1.setText(spannable);
            }
            return convertView;
        }
    }

    private static final String HEADER_SENTINEL = "\u0000header";

    private List<DriverRow> mDriverRows = new ArrayList<>();

    @Override
    public void onDialogClosed(boolean positiveResult) {
        // Selection is handled by the AlertDialog's single-choice listener
    }

    @Override
    public void onBindDialogView(View view, androidx.fragment.app.FragmentActivity associatedActivity) {
        // The dialog is fully built in onPrepareDialogBuilder
    }

    /**
     * Read version/minApi details from an installed driver's meta.json
     */
    public static String getDriverDetails(Context context, File driverDir)
    {
        File metaFile = new File(driverDir, "meta.json");
        ArrayList<String> parts = new ArrayList<>();
        if (metaFile.isFile()) {
            try (InputStream inputStream = new FileInputStream(metaFile)) {
                String json = new String(readAll(inputStream), StandardCharsets.UTF_8);
                JSONObject meta = new JSONObject(json);
                String driverVersion = meta.optString("driverVersion", "");
                String libraryName = meta.optString("libraryName", "");
                String minApi = meta.optString("minApi", "");
                if (!TextUtils.isEmpty(driverVersion)) {
                    parts.add(driverVersion);
                }
                if (!TextUtils.isEmpty(minApi)) {
                    parts.add(context.getString(R.string.gpuDriver_minApi, Integer.parseInt(minApi)));
                }
                if (!TextUtils.isEmpty(libraryName)) {
                    parts.add(libraryName);
                }
            } catch (Exception ignored) {
            }
        }

        String benchmarkResult = androidx.preference.PreferenceManager.getDefaultSharedPreferences(context)
                .getString("gpuDriverBenchmarkResult_" + driverDir.getName(), "");
        if (!TextUtils.isEmpty(benchmarkResult)) {
            parts.add(context.getString(R.string.gpuDriver_benchmarkResultShort, benchmarkResult));
        }

        return TextUtils.join(" · ", parts);
    }

    /**
     * Write the selected driver into the name/lib pref keys that the native side reads.
     * Also fixes stale prefs when switching between already-installed drivers.
     */
    private void syncGlobalDriverPrefs()
    {
        String driverName = getCurrentValue();
        String driverLib = "";

        if (!TextUtils.isEmpty(driverName)) {
            File driverDir = new File(getDriverDir(getContext()), driverName);
            File metaFile = new File(driverDir, "meta.json");
            if (metaFile.isFile()) {
                try (InputStream inputStream = new FileInputStream(metaFile)) {
                    String json = new String(readAll(inputStream), StandardCharsets.UTF_8);
                    driverLib = new JSONObject(json).optString("libraryName", "");
                } catch (Exception ignored) {
                }
            }
        }

        SharedPreferences prefs = getPreferenceManager().getSharedPreferences();
        prefs.edit()
                .putString("gpuDriverName", driverName)
                .putString("gpuDriverLib", driverLib)
                .apply();
    }

    private void deleteSelectedDriver()
    {
        String driverName = getCurrentValue();
        if (TextUtils.isEmpty(driverName)) {
            Notifier.showToast(getContext(), R.string.gpuDriver_noDriverToDelete);
            return;
        }

        File driverDir = new File(getDriverDir(getContext()), driverName);
        new androidx.appcompat.app.AlertDialog.Builder(getContext())
                .setTitle(R.string.gpuDriver_title)
                .setMessage(getContext().getString(R.string.gpuDriver_deleteConfirm, driverName))
                .setPositiveButton(R.string.listItem_delete, (dialog, which) -> {
                    FileUtil.deleteFolder(driverDir);
                    setValue("");
                    syncGlobalDriverPrefs();
                    notifyChanged();
                    Notifier.showToast(getContext(), R.string.gpuDriver_deleteSuccess);
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
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
     * Import a driver zip, returning {sanitizedDriverName, libraryName, minApi} on success or null on failure
     */
    public static String[] importDriver(Context context, Uri uri)
    {
        String driverName = null;
        String libraryName = null;
        String minApi = "0";

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
                    if (TextUtils.isEmpty(driverName)) {
                        driverName = meta.optString("name", "");
                    }
                    libraryName = meta.optString("libraryName", "");
                    minApi = meta.optString("minApi", "0");
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

        return new String[] { sanitizedName, libraryName, minApi };
    }

    /**
     * Best-effort Adreno model lookup for displaying in the picker
     */
    public static String getGpuModel()
    {
        String[] sysfsPaths = {
                "/sys/class/kgsl/kgsl-3d0/gpu_model",
                "/sys/kernel/gpu/gpu_model",
        };
        for (String path : sysfsPaths) {
            String model = readSysfs(path);
            if (!TextUtils.isEmpty(model)) {
                return model;
            }
        }
        return "";
    }

    private static String readSysfs(String path)
    {
        try (FileInputStream inputStream = new FileInputStream(path)) {
            byte[] buffer = new byte[256];
            int read = inputStream.read(buffer);
            return read > 0 ? new String(buffer, 0, read, StandardCharsets.UTF_8).trim() : "";
        } catch (Exception e) {
            return "";
        }
    }

    @Override
    public CharSequence getSummary() {
        CharSequence entry = getEntry();
        CharSequence summary = super.getSummary();
        if (summary == null) {
            return entry;
        }
        String summaryStr = summary.toString();
        if (summaryStr.equals("%1$s") || summaryStr.equals("%s") || summaryStr.isEmpty()) {
            return entry != null ? entry : "";
        }
        if (summaryStr.contains("%1$s") || summaryStr.contains("%s")) {
            return entry != null ? String.format(summaryStr, entry) : "";
        }
        return summary;
    }

    private static byte[] readAll(InputStream inputStream) throws IOException
    {
        java.io.ByteArrayOutputStream outputStream = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int read;
        while ((read = inputStream.read(buffer)) > 0) {
            outputStream.write(buffer, 0, read);
        }
        return outputStream.toByteArray();
    }
}