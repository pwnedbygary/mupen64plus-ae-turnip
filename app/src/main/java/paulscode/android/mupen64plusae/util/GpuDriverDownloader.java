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

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class GpuDriverDownloader
{
    public interface OnProgress
    {
        void onProgress(long bytesDownloaded, long totalBytes);
    }

    public static class DriverAsset
    {
        public final String name;
        public final String url;
        public final long size;
        public final String publishedAt;

        DriverAsset(String name, String url, long size, String publishedAt)
        {
            this.name = name;
            this.url = url;
            this.size = size;
            this.publishedAt = publishedAt;
        }
    }

    private static final String USER_AGENT = "Mupen64PlusAE-Turnip";
    private static final int TIMEOUT_MS = 15000;

    /**
     * Fetch zip assets from a GitHub repo's releases, filtering out
     * Adreno 8xx-only builds that cannot run on 7xx devices.
     */
    public static List<DriverAsset> fetchAssets(String owner, String repo) throws IOException
    {
        URL url = new URL("https://api.github.com/repos/" + owner + "/" + repo + "/releases?per_page=10");
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestProperty("User-Agent", USER_AGENT);
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);

        int responseCode = connection.getResponseCode();
        if (responseCode != 200) {
            throw new IOException("GitHub API error: " + responseCode);
        }

        String json;
        try (InputStream inputStream = connection.getInputStream()) {
            json = new String(readAll(inputStream), StandardCharsets.UTF_8);
        }

        try {
            return parseAssets(json);
        } catch (Exception e) {
            throw new IOException("Failed to parse GitHub API response", e);
        }
    }

    private static List<DriverAsset> parseAssets(String json) throws Exception
    {
        List<DriverAsset> assets = new ArrayList<>();
        JSONArray releases = new JSONArray(json);

        for (int i = 0; i < releases.length(); i++) {
            JSONObject release = releases.getJSONObject(i);
            String publishedAt = release.optString("published_at", "");
            if (publishedAt.length() > 10) {
                publishedAt = publishedAt.substring(0, 10);
            }

            JSONArray releaseAssets = release.optJSONArray("assets");
            if (releaseAssets == null) {
                continue;
            }

            for (int j = 0; j < releaseAssets.length(); j++) {
                JSONObject asset = releaseAssets.getJSONObject(j);
                String assetName = asset.optString("name", "");
                if (!assetName.endsWith(".zip")) {
                    continue;
                }

                String lower = assetName.toLowerCase();
                if (lower.contains("a8xx") || lower.contains("gen8") || lower.contains("8xx")) {
                    continue;
                }

                assets.add(new DriverAsset(
                        assetName,
                        asset.getString("browser_download_url"),
                        asset.optLong("size", 0),
                        publishedAt));
            }
        }

        return assets;
    }

    /**
     * Download a file, reporting progress via the given listener
     */
    public static void download(String url, File target, OnProgress progress) throws IOException
    {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setRequestProperty("User-Agent", USER_AGENT);
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);
        connection.setInstanceFollowRedirects(true);

        int responseCode = connection.getResponseCode();
        if (responseCode != 200) {
            throw new IOException("Download failed: HTTP " + responseCode);
        }

        long total = connection.getContentLengthLong();
        try (InputStream inputStream = connection.getInputStream();
             FileOutputStream outputStream = new FileOutputStream(target)) {

            byte[] buffer = new byte[64 * 1024];
            long downloaded = 0;
            int read;
            while ((read = inputStream.read(buffer)) > 0) {
                outputStream.write(buffer, 0, read);
                downloaded += read;
                if (progress != null) {
                    progress.onProgress(downloaded, total);
                }
            }
        }
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