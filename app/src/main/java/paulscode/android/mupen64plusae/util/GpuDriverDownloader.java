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

import android.text.TextUtils;

import java.io.File;
import java.io.FileInputStream;
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
        public final String tag;

        DriverAsset(String name, String url, long size, String publishedAt, String tag)
        {
            this.name = name;
            this.url = url;
            this.size = size;
            this.publishedAt = publishedAt;
            this.tag = tag;
        }
    }

    public static class DriverSource
    {
        public final String owner;
        public final String repo;

        public DriverSource(String owner, String repo)
        {
            this.owner = owner;
            this.repo = repo;
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
            String tag = release.optString("tag_name", "");
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
                        publishedAt,
                        tag));
            }
        }

        return assets;
    }

    /**
     * Fetch the tag name of the latest release of a repo, or null on failure
     */
    public static String fetchLatestTag(String owner, String repo) throws IOException
    {
        URL url = new URL("https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest");
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
            return new JSONObject(json).optString("tag_name", "");
        } catch (Exception e) {
            return "";
        }
    }

    /**
     * Compare two release tags numerically (e.g. "v26.3.0-20260818-r9" vs "v26.0.0-rc08").
     * Returns &lt; 0, 0, or &gt; 0 if a is older, equal, or newer than b.
     */
    public static int compareTags(String a, String b)
    {
        if (TextUtils.isEmpty(a)) {
            return TextUtils.isEmpty(b) ? 0 : -1;
        }
        if (TextUtils.isEmpty(b)) {
            return 1;
        }

        StringBuilder aDigits = new StringBuilder();
        StringBuilder bDigits = new StringBuilder();
        for (char c : a.toCharArray()) {
            if (Character.isDigit(c) || c == '.') {
                aDigits.append(c);
            }
        }
        for (char c : b.toCharArray()) {
            if (Character.isDigit(c) || c == '.') {
                bDigits.append(c);
            }
        }

        String[] aParts = aDigits.toString().split("\\.");
        String[] bParts = bDigits.toString().split("\\.");
        int length = Math.max(aParts.length, bParts.length);
        for (int i = 0; i < length; i++) {
            int aPart = i < aParts.length ? parsePart(aParts[i]) : 0;
            int bPart = i < bParts.length ? parsePart(bParts[i]) : 0;
            if (aPart != bPart) {
                return Integer.compare(aPart, bPart);
            }
        }
        return 0;
    }

    private static int parsePart(String part)
    {
        try {
            return Integer.parseInt(part);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /**
     * Write the source info of a downloaded driver into its driver dir so
     * future update checks can locate the matching GitHub repo.
     */
    public static void writeSourceInfo(File driverDir, String owner, String repo, String tag)
    {
        try (java.io.FileWriter writer = new java.io.FileWriter(new File(driverDir, "source.json"))) {
            writer.write("{\"owner\":\"" + owner + "\",\"repo\":\"" + repo + "\",\"tag\":\"" + tag + "\"}");
        } catch (IOException ignored) {
        }
    }

    /**
     * Parse the source info of an installed driver, or null if unknown
     */
    public static DriverSource readSourceInfo(File driverDir)
    {
        File sourceFile = new File(driverDir, "source.json");
        if (!sourceFile.isFile()) {
            return null;
        }
        try (FileInputStream inputStream = new FileInputStream(sourceFile)) {
            String json = new String(readAll(inputStream), StandardCharsets.UTF_8);
            JSONObject source = new JSONObject(json);
            return new DriverSource(source.optString("owner", ""), source.optString("repo", ""));
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * Read the tag a driver was downloaded from, or null if unknown
     */
    public static String readSourceTag(File driverDir)
    {
        File sourceFile = new File(driverDir, "source.json");
        if (!sourceFile.isFile()) {
            return null;
        }
        try (FileInputStream inputStream = new FileInputStream(sourceFile)) {
            String json = new String(readAll(inputStream), StandardCharsets.UTF_8);
            return new JSONObject(json).optString("tag", "");
        } catch (Exception e) {
            return null;
        }
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