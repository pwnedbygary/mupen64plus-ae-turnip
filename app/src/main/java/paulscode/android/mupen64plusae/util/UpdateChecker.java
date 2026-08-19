/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */
package paulscode.android.mupen64plusae.util;

import android.content.Context;

import paulscode.android.mupen64plusae.BuildConfig;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class UpdateChecker
{
    private static final String UPDATE_REPO_OWNER = "pwnedbygary";
    private static final String UPDATE_REPO_NAME = "mupen64plus-ae-turnip";

    public interface OnUpdateResult
    {
        /**
         * Called on the UI thread when the check completes.
         *
         * @param latestTag the latest release tag, or null on failure
         * @param updateAvailable true when a newer version exists
         */
        void onResult(String latestTag, boolean updateAvailable);
    }

    /**
     * Check the GitHub releases of this fork against the installed version.
     * Release tags must be named "v<versionCode>" (e.g. "v332").
     */
    public static void checkForUpdates(Context context, OnUpdateResult listener)
    {
        ExecutorService executor = Executors.newSingleThreadExecutor();
        executor.execute(() -> {
            String latestTag = null;
            boolean updateAvailable = false;

            try {
                latestTag = GpuDriverDownloader.fetchLatestTag(UPDATE_REPO_OWNER, UPDATE_REPO_NAME);
                int latestCode = parseVersionCode(latestTag);
                if (latestCode > 0) {
                    updateAvailable = latestCode > BuildConfig.VERSION_CODE;
                }
            } catch (Exception e) {
                // Leave latestTag null
            }

            String finalLatestTag = latestTag;
            boolean finalUpdateAvailable = updateAvailable;
            if (context instanceof android.app.Activity) {
                ((android.app.Activity) context).runOnUiThread(
                        () -> listener.onResult(finalLatestTag, finalUpdateAvailable));
            } else {
                listener.onResult(finalLatestTag, finalUpdateAvailable);
            }
            executor.shutdown();
        });
    }

    private static int parseVersionCode(String tag)
    {
        if (tag == null) {
            return 0;
        }
        StringBuilder digits = new StringBuilder();
        for (char c : tag.toCharArray()) {
            if (Character.isDigit(c)) {
                digits.append(c);
            }
        }
        try {
            return Integer.parseInt(digits.toString());
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}