/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */
package paulscode.android.mupen64plusae;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import paulscode.android.mupen64plusae.dialog.ProgressDialog;
import paulscode.android.mupen64plusae.persistent.AppData;
import paulscode.android.mupen64plusae.persistent.GlobalPrefs;
import paulscode.android.mupen64plusae.preference.DriverPreference;
import paulscode.android.mupen64plusae.util.GpuDriverDownloader;
import paulscode.android.mupen64plusae.util.Notifier;

public class GpuDriverDownloadActivity extends AppCompatActivity
{
    private static class DriverSource
    {
        final String name;
        final String description;
        final String owner;
        final String repo;

        DriverSource(String name, String description, String owner, String repo)
        {
            this.name = name;
            this.description = description;
            this.owner = owner;
            this.repo = repo;
        }
    }

    private static final DriverSource[] SOURCES = {
            new DriverSource("K11MCH1", "Classic AdrenoTools releases (Turnip v26.x, Qualcomm)", "K11MCH1", "AdrenoToolsDrivers"),
            new DriverSource("StevenMXZ", "Scheduled Turnip builds (v26.2.x, Gen8)", "StevenMXZ", "Adreno-Tools-Drivers"),
            new DriverSource("Banners-Turnip", "Daily Mesa main builds (v26.3.0)", "The412Banner", "Banners-Turnip"),
            new DriverSource("Mr. Purple", "purple-turnip builds", "MrPurple666", "purple-turnip"),
            new DriverSource("Whitebelyash", "freedreno CI builds", "whitebelyash", "AdrenoToolsDrivers"),
            new DriverSource("nihui", "mesa-turnip-android-driver builds", "nihui", "mesa-turnip-android-driver"),
    };

    private static final String STATE_CURRENT_SOURCE = "STATE_CURRENT_SOURCE";

    private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();

    private ListView mListView = null;
    private TextView mTitle = null;

    private DriverSource mCurrentSource = null;
    private List<GpuDriverDownloader.DriverAsset> mAssets = new ArrayList<>();

    private ProgressDialog mProgressDialog = null;
    private boolean mDownloadCanceled = false;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_gpu_driver_download);

        mListView = findViewById(R.id.driverList);
        mTitle = findViewById(R.id.title);

        mListView.setOnItemClickListener((parent, view, position, id) -> {
            if (mCurrentSource == null) {
                loadReleases(SOURCES[position]);
            } else {
                downloadAsset(mAssets.get(position));
            }
        });

        if (savedInstanceState != null) {
            String sourceIndex = savedInstanceState.getString(STATE_CURRENT_SOURCE);
            if (sourceIndex != null) {
                mCurrentSource = SOURCES[Integer.parseInt(sourceIndex)];
            }
        }

        if (mCurrentSource != null) {
            loadReleases(mCurrentSource);
        } else {
            showSources();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState)
    {
        super.onSaveInstanceState(outState);
        if (mCurrentSource != null) {
            for (int i = 0; i < SOURCES.length; i++) {
                if (SOURCES[i] == mCurrentSource) {
                    outState.putString(STATE_CURRENT_SOURCE, String.valueOf(i));
                    break;
                }
            }
        }
    }

    @Override
    protected void onDestroy()
    {
        mExecutor.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onBackPressed()
    {
        if (mCurrentSource != null) {
            mCurrentSource = null;
            showSources();
        } else {
            super.onBackPressed();
        }
    }

    private void showSources()
    {
        mTitle.setText(R.string.gpuDriver_downloadSources);
        mListView.setAdapter(new SourceAdapter(this, SOURCES));
    }

    private void loadReleases(DriverSource source)
    {
        mCurrentSource = source;
        mTitle.setText(source.name);

        mProgressDialog = new ProgressDialog(this, getString(R.string.gpuDriver_downloadTitle),
                source.name, getString(R.string.gpuDriver_downloadFetching), false);
        mProgressDialog.show();

        mExecutor.execute(() -> {
            try {
                List<GpuDriverDownloader.DriverAsset> assets =
                        GpuDriverDownloader.fetchAssets(source.owner, source.repo);
                runOnUiThread(() -> {
                    mProgressDialog.dismiss();
                    mProgressDialog = null;
                    showAssets(assets);
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    mProgressDialog.dismiss();
                    mProgressDialog = null;
                    mCurrentSource = null;
                    Notifier.showToast(this, R.string.gpuDriver_downloadError);
                    showSources();
                });
            }
        });
    }

    private void showAssets(List<GpuDriverDownloader.DriverAsset> assets)
    {
        mAssets = assets;
        if (assets.isEmpty()) {
            mTitle.setText(R.string.gpuDriver_noReleases);
        } else {
            mTitle.setText(getString(R.string.gpuDriver_downloadSelect, mCurrentSource.name));
        }
        mListView.setAdapter(new AssetAdapter(this, assets));
    }

    private void downloadAsset(GpuDriverDownloader.DriverAsset asset)
    {
        File tempFile = new File(getCacheDir(), "driver_download.zip");

        mDownloadCanceled = false;
        mProgressDialog = new ProgressDialog(this, getString(R.string.gpuDriver_downloadTitle),
                asset.name, getString(R.string.gpuDriver_downloading), true);
        mProgressDialog.setMaxProgress(asset.size);
        mProgressDialog.setOnCancelListener(() -> mDownloadCanceled = true);
        mProgressDialog.show();

        mExecutor.execute(() -> {
            String[] driverInfo = null;
            try {
                GpuDriverDownloader.download(asset.url, tempFile,
                        (downloaded, total) -> mProgressDialog.setProgress(downloaded));
                if (!mDownloadCanceled) {
                    driverInfo = DriverPreference.importDriver(this, Uri.fromFile(tempFile));
                }
            } catch (IOException e) {
                // Leave driverInfo null
            } finally {
                tempFile.delete();
            }

            String[] finalDriverInfo = driverInfo;
            runOnUiThread(() -> {
                mProgressDialog.dismiss();
                mProgressDialog = null;

                if (mDownloadCanceled) {
                    return;
                }

                if (finalDriverInfo != null) {
                    GlobalPrefs globalPrefs = new GlobalPrefs(this, new AppData(this));
                    globalPrefs.putGpuDriver(finalDriverInfo[0], finalDriverInfo[1]);
                    Notifier.showToast(this, R.string.gpuDriver_importSuccess);
                    finish();
                } else {
                    Notifier.showToast(this, R.string.gpuDriver_downloadError);
                }
            });
        });
    }

    private static class SourceAdapter extends ArrayAdapter<DriverSource>
    {
        SourceAdapter(Context context, DriverSource[] sources)
        {
            super(context, R.layout.list_item_two_text_icon, sources);
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent)
        {
            if (convertView == null) {
                convertView = LayoutInflater.from(getContext()).inflate(R.layout.list_item_two_text_icon, parent, false);
            }

            DriverSource source = getItem(position);
            TextView text1 = convertView.findViewById(R.id.text1);
            TextView text2 = convertView.findViewById(R.id.text2);
            text1.setText(source.name);
            text2.setText(source.description);
            return convertView;
        }
    }

    private static class AssetAdapter extends ArrayAdapter<GpuDriverDownloader.DriverAsset>
    {
        AssetAdapter(Context context, List<GpuDriverDownloader.DriverAsset> assets)
        {
            super(context, R.layout.list_item_two_text_icon, assets);
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent)
        {
            if (convertView == null) {
                convertView = LayoutInflater.from(getContext()).inflate(R.layout.list_item_two_text_icon, parent, false);
            }

            GpuDriverDownloader.DriverAsset asset = getItem(position);
            TextView text1 = convertView.findViewById(R.id.text1);
            TextView text2 = convertView.findViewById(R.id.text2);
            text1.setText(asset.name);
            text2.setText(asset.publishedAt + "  ·  " + formatSize(asset.size));
            return convertView;
        }

        private static String formatSize(long size)
        {
            if (size <= 0) {
                return "";
            }
            if (size < 1024 * 1024) {
                return String.valueOf(size / 1024) + " KB";
            }
            return String.valueOf(size / (1024 * 1024)) + " MB";
        }
    }
}