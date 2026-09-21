package paulscode.android.mupen64plusae.jni;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.Nullable;

import com.sun.jna.Native;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Java-side driver for the native RetroAchievements glue (see
 * mupen64plus-core/upstream/src/api/ra_glue.h).
 *
 * Responsibilities:
 *  - own the ra_glue client lifecycle (create/shutdown) and register the JNA
 *    bridge trampolines the native side calls back into;
 *  - perform the HTTP traffic for rcheevos on a background executor, then hand
 *    responses back with ra_glue_dispatch_server_response();
 *  - deliver native events and async results to {@link Listener}s on the main
 *    thread;
 *  - expose login / load-game / progress / hardcore operations to the UI.
 *
 * The native client is internally mutex-protected, so its API may be called
 * from the emu thread and from Java threads. All ra_glue_* entry points are
 * exported by the core .so (see api_export.ver).
 *
 * This class is a process-wide singleton: rcheevos must outlive individual
 * activities, since achievements are evaluated from the emulation thread.
 */
public class RetroAchievementsManager {
    private static final String TAG = "RetroAchievements";

    /** rc_api_server_response_t status for a transient client-side failure (rcheevos retries). */
    private static final int RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR = -2;

    /**
     * RetroAchievements negotiates client capabilities (including hardcore unlocks) from the
     * User-Agent, which must carry a numeric product version.
     */
    private static final String USER_AGENT = "Mupen64PlusAE/3.0 (Android)";
    private static final String DEFAULT_CONTENT_TYPE = "application/x-www-form-urlencoded";

    /** Large enough for any realistic achievement list / progress blob. */
    private static final int TEXT_BUFFER_SIZE = 128 * 1024;
    private static final int PROGRESS_BUFFER_SIZE = 64 * 1024;
    private static final int PROGRESS_BUFFER_MAX = 1024 * 1024;

    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;

    /** rc_client async result code passed to {@link Listener#onRaAsyncResult}: RC_OK == 0; non-zero is an error. */
    public static final int RESULT_OK = 0;

    public interface Listener {
        /**
         * A native rcheevos event. {@code type} is the parsed event type
         * (achievement, server_error, reset, game_completed,
         * leaderboard_started, leaderboard_failed, leaderboard_submitted),
         * {@code json} is the raw event payload.
         */
        void onRaEvent(String type, String json);

        /** Completion of a login/logout/load_game operation. */
        void onRaAsyncResult(int result, @Nullable String errorMessage);
    }

    private static volatile RetroAchievementsManager sInstance;

    public static RetroAchievementsManager getInstance() {
        RetroAchievementsManager instance = sInstance;
        if (instance == null) {
            synchronized (RetroAchievementsManager.class) {
                instance = sInstance;
                if (instance == null) {
                    instance = new RetroAchievementsManager();
                    sInstance = instance;
                }
            }
        }
        return instance;
    }

    private final CoreLibrary mCore = Native.load("mupen64plus-core", CoreLibrary.class);
    private final ExecutorService mHttpExecutor = Executors.newFixedThreadPool(2);
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final List<Listener> mListeners = new ArrayList<>();
    private final Object mListenerLock = new Object();

    // Held strongly for the process lifetime: the native client stores these
    // function pointers and would call freed trampolines if they were collected.
    private CoreLibrary.RaServerCallCallback mServerCallCallback;
    private CoreLibrary.RaEventCallback mEventCallback;
    private CoreLibrary.RaAsyncResultCallback mAsyncResultCallback;

    private boolean mInitialized = false;

    private RetroAchievementsManager() {
    }

    /**
     * Create the native client and install the JNA bridge. Idempotent.
     *
     * @return true if the client is ready.
     */
    public synchronized boolean initialize() {
        if (mInitialized) {
            return true;
        }

        mServerCallCallback = this::onServerCall;
        mEventCallback = this::onEvent;
        mAsyncResultCallback = this::onAsyncResult;

        mCore.ra_glue_set_java_bridge(mServerCallCallback, mEventCallback, mAsyncResultCallback);

        if (mCore.ra_glue_create() == 0) {
            Log.e(TAG, "ra_glue_create failed: " + mCore.ra_glue_get_last_error());
            return false;
        }

        mInitialized = true;
        return true;
    }

    /** Tear down the native client. Idempotent. */
    public synchronized void shutdown() {
        if (!mInitialized) {
            return;
        }
        mCore.ra_glue_shutdown();
        mInitialized = false;
    }

    public synchronized boolean isInitialized() {
        return mInitialized;
    }

    // ---- listeners -------------------------------------------------------

    public void addListener(Listener listener) {
        synchronized (mListenerLock) {
            if (!mListeners.contains(listener)) {
                mListeners.add(listener);
            }
        }
    }

    public void removeListener(Listener listener) {
        synchronized (mListenerLock) {
            mListeners.remove(listener);
        }
    }

    // ---- session ---------------------------------------------------------

    /** @return true if the async login request was started. */
    public boolean login(String username, String password) {
        if (!initialize()) {
            return false;
        }
        return mCore.ra_glue_login_password(username, password) != 0;
    }

    /** @return true if the async token login request was started. */
    public boolean loginWithToken(String username, String token) {
        if (!initialize()) {
            return false;
        }
        return mCore.ra_glue_login_token(username, token) != 0;
    }

    public boolean logout() {
        return mInitialized && mCore.ra_glue_logout() != 0;
    }

    /** @return true if the async load-game request was started. */
    public boolean loadGame(String md5) {
        if (!initialize()) {
            return false;
        }
        return mCore.ra_glue_load_game(md5) != 0;
    }

    public boolean isLoggedIn() {
        return mInitialized && mCore.ra_glue_is_logged_in() != 0;
    }

    public boolean isGameLoaded() {
        return mInitialized && mCore.ra_glue_is_game_loaded() != 0;
    }

    /**
     * rc_client load-game state: 0 none, 1 await login, 2 identifying game,
     * 3 fetching game data, 4 starting session, 5 done, 6 aborted.
     */
    public int getLoadGameState() {
        return mInitialized ? mCore.ra_glue_get_load_game_state() : 0;
    }

    @Nullable
    public String getUserName() {
        return readText(mCore::ra_glue_get_user_name);
    }

    /** Session token captured from a successful login, for token-only re-login. */
    @Nullable
    public String getUserToken() {
        return readText(mCore::ra_glue_get_user_token);
    }

    @Nullable
    public String getGameName() {
        return readText(mCore::ra_glue_get_game_name);
    }

    @Nullable
    public String getSummaryJson() {
        return readText(mCore::ra_glue_get_summary_json);
    }

    @Nullable
    public String getAchievementsJson() {
        return readText(mCore::ra_glue_get_achievements_json);
    }

    @Nullable
    public String getLastError() {
        return mCore.ra_glue_get_last_error();
    }

    // ---- hardcore --------------------------------------------------------

    public void setHardcore(boolean enabled) {
        if (mInitialized) {
            mCore.ra_glue_set_hardcore(enabled ? 1 : 0);
        }
    }

    public boolean isHardcore() {
        return mInitialized && mCore.ra_glue_get_hardcore() != 0;
    }

    // ---- progress persistence -------------------------------------------

    /** @return serialized progress bytes, or null if unavailable/not loaded. */
    @Nullable
    public byte[] serializeProgress() {
        if (!mInitialized) {
            return null;
        }

        int capacity = PROGRESS_BUFFER_SIZE;
        while (capacity <= PROGRESS_BUFFER_MAX) {
            byte[] buffer = new byte[capacity];
            int written = mCore.ra_glue_serialize_progress(buffer, buffer.length);
            if (written > 0) {
                byte[] result = new byte[written];
                System.arraycopy(buffer, 0, result, 0, written);
                return result;
            }
            // -1: either no game loaded or the buffer was too small; grow and retry.
            capacity *= 4;
        }
        return null;
    }

    /** @return true if the progress blob was accepted by the client (glue returns 1 on success). */
    public boolean deserializeProgress(byte[] progress) {
        if (!mInitialized || progress == null || progress.length == 0) {
            return false;
        }
        return mCore.ra_glue_deserialize_progress(progress, progress.length) != 0;
    }

    /** Call periodically while emulation is paused so achievements still tick. */
    public void idle() {
        if (mInitialized) {
            mCore.ra_glue_idle();
        }
    }

    // ---- native -> Java bridge ------------------------------------------

    private void onServerCall(int requestId, String url, String postData, String contentType) {
        mHttpExecutor.execute(() -> {
            byte[] body;
            int status;

            try {
                HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
                connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
                connection.setReadTimeout(READ_TIMEOUT_MS);
                connection.setRequestProperty("User-Agent", USER_AGENT);

                if (postData != null && !postData.isEmpty()) {
                    connection.setRequestMethod("POST");
                    connection.setDoOutput(true);
                    connection.setRequestProperty("Content-Type",
                            (contentType != null && !contentType.isEmpty()) ? contentType : DEFAULT_CONTENT_TYPE);

                    byte[] payload = postData.getBytes(StandardCharsets.UTF_8);
                    connection.setFixedLengthStreamingMode(payload.length);
                    try (OutputStream output = connection.getOutputStream()) {
                        output.write(payload);
                    }
                } else {
                    connection.setRequestMethod("GET");
                }

                status = connection.getResponseCode();
                InputStream input = (status >= HttpURLConnection.HTTP_BAD_REQUEST)
                        ? connection.getErrorStream() : connection.getInputStream();
                body = readAll(input);
                connection.disconnect();
            } catch (IOException e) {
                // Hand the error back to rcheevos as a retryable client error with a message body.
                Log.w(TAG, "RA request failed: " + e.getMessage());
                body = ("RA network error: " + e.getMessage()).getBytes(StandardCharsets.UTF_8);
                status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            }

            mCore.ra_glue_dispatch_server_response(requestId, body, body.length, status);
        });
    }

    private void onEvent(String json) {
        String type = "unknown";
        try {
            JSONObject object = new JSONObject(json);
            type = object.optString("type", "unknown");
        } catch (JSONException e) {
            Log.w(TAG, "Malformed RA event: " + json);
        }

        final String eventType = type;
        final String eventJson = json;
        mMainHandler.post(() -> {
            List<Listener> snapshot;
            synchronized (mListenerLock) {
                snapshot = new ArrayList<>(mListeners);
            }
            for (Listener listener : snapshot) {
                listener.onRaEvent(eventType, eventJson);
            }
        });
    }

    private void onAsyncResult(int result, String errorMessage) {
        final String message = errorMessage;
        mMainHandler.post(() -> {
            List<Listener> snapshot;
            synchronized (mListenerLock) {
                snapshot = new ArrayList<>(mListeners);
            }
            for (Listener listener : snapshot) {
                listener.onRaAsyncResult(result, message);
            }
        });
    }

    // ---- helpers ---------------------------------------------------------

    private interface TextGetter {
        int get(byte[] buffer, int bufferSize);
    }

    @Nullable
    private String readText(TextGetter getter) {
        byte[] buffer = new byte[TEXT_BUFFER_SIZE];
        if (getter.get(buffer, buffer.length) == 0) {
            return null;
        }
        int length = 0;
        while (length < buffer.length && buffer[length] != 0) {
            ++length;
        }
        return new String(buffer, 0, length, StandardCharsets.UTF_8);
    }

    private static byte[] readAll(@Nullable InputStream input) throws IOException {
        if (input == null) {
            return new byte[0];
        }
        try (InputStream in = input; ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] chunk = new byte[8192];
            int read;
            while ((read = in.read(chunk)) != -1) {
                out.write(chunk, 0, read);
            }
            return out.toByteArray();
        }
    }
}
