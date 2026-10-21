package paulscode.android.mupen64plusae.jni;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

public class RetroAchievementsManagerTest {

    private static final int MB = 1024 * 1024;

    private static final String SERVER_URL = "https://retroachievements.org/dorequest.php";

    /** RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR in rcheevos's rc_api_request.h. */
    private static final int RETRYABLE_CLIENT_ERROR = -2;

    /**
     * RetroAchievements' compliance page (section C) asks for "EmulatorName/v1.0.0 (OSName 10.0)":
     * the emulator's own name, a numeric version with an optional "v", then, optionally, the
     * operating system.
     */
    private static final String USER_AGENT_FORMAT =
            "Mupen64Turnip/v?\\d+(\\.\\d+)*( \\(Android [^()]+\\))?";

    /** Like the glue's JSON getters: writes the text and a NUL only if both fit, else returns 0. */
    private static final class FakeGetter implements RetroAchievementsManager.TextGetter {
        final List<Integer> bufferSizes = new ArrayList<>();
        private final byte[] text;

        FakeGetter(byte[] text) {
            this.text = text;
        }

        @Override
        public int get(byte[] buffer, int bufferSize) {
            bufferSizes.add(bufferSize);
            if (text.length + 1 > bufferSize) {
                return 0;
            }
            System.arraycopy(text, 0, buffer, 0, text.length);
            buffer[text.length] = 0;
            return 1;
        }
    }

    /** Answers with a fixed status and body, and keeps what the request wrote. */
    private static final class FakeConnection extends HttpURLConnection {
        final ByteArrayOutputStream sent = new ByteArrayOutputStream();
        private final int status;
        private final byte[] answer;

        FakeConnection(int status, String answer) throws IOException {
            super(new URL(SERVER_URL));
            this.status = status;
            this.answer = answer.getBytes(StandardCharsets.UTF_8);
        }

        @Override
        public void connect() {
        }

        @Override
        public void disconnect() {
        }

        @Override
        public boolean usingProxy() {
            return false;
        }

        @Override
        public OutputStream getOutputStream() {
            return sent;
        }

        @Override
        public int getResponseCode() {
            return status;
        }

        @Override
        public InputStream getInputStream() {
            return new ByteArrayInputStream(answer);
        }
    }

    private static String text(byte[] bytes) {
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static byte[] textOfLength(int length) {
        byte[] text = new byte[length];
        Arrays.fill(text, (byte) 'a');
        return text;
    }

    @Test
    public void shortTextIsReadInOneCall() {
        FakeGetter getter = new FakeGetter("{\"achievements\":[]}".getBytes(StandardCharsets.UTF_8));
        assertEquals("{\"achievements\":[]}", RetroAchievementsManager.readText(getter, 2 * MB));
        assertEquals(1, getter.bufferSizes.size());
    }

    @Test
    public void textLargerThanTheFirstBufferIsRead() {
        FakeGetter getter = new FakeGetter(textOfLength(MB));
        String text = RetroAchievementsManager.readText(getter, 2 * MB);
        assertNotNull(text);
        assertEquals(MB, text.length());
    }

    @Test
    public void textLargerThanTheLimitIsNotRead() {
        FakeGetter getter = new FakeGetter(textOfLength(2 * MB));
        assertNull(RetroAchievementsManager.readText(getter, 2 * MB));
        assertFalse(getter.bufferSizes.isEmpty());
        for (int size : getter.bufferSizes) {
            assertTrue(size <= 2 * MB);
        }
    }

    @Test
    public void textIsDecodedAsUtf8() {
        FakeGetter getter = new FakeGetter("Pok\u00e9mon Snap".getBytes(StandardCharsets.UTF_8));
        assertEquals("Pok\u00e9mon Snap", RetroAchievementsManager.readText(getter, 2 * MB));
    }

    @Test
    public void serverAnswerIsPassedOn() throws IOException {
        FakeConnection connection = new FakeConnection(200, "{\"Success\":true}");
        RetroAchievementsManager.ServerResponse response =
                RetroAchievementsManager.request(url -> connection, SERVER_URL, "r=ping&u=tester", null);
        assertEquals(200, response.status);
        assertEquals("{\"Success\":true}", text(response.body));
        assertEquals("r=ping&u=tester", text(connection.sent.toByteArray()));
        assertNull(response.failure);
    }

    @Test
    public void networkFailureIsAnsweredForRetry() {
        IOException offline = new IOException("offline");
        RetroAchievementsManager.ServerResponse response = RetroAchievementsManager.request(
                url -> { throw offline; }, SERVER_URL, "r=ping", null);
        assertEquals(RETRYABLE_CLIENT_ERROR, response.status);
        assertEquals("RA network error: offline", text(response.body));
        assertSame(offline, response.failure);
    }

    @Test
    public void unexpectedExceptionIsAnsweredForRetry() {
        IllegalStateException broken = new IllegalStateException("broken");
        RetroAchievementsManager.ServerResponse response = RetroAchievementsManager.request(
                url -> { throw broken; }, SERVER_URL, "r=ping", null);
        assertEquals(RETRYABLE_CLIENT_ERROR, response.status);
        assertTrue(text(response.body).contains("broken"));
        assertSame(broken, response.failure);
    }

    @Test
    public void userAgentNamesTheAppVersionAndAndroidRelease() {
        assertEquals("Mupen64Turnip/3.0.338 (Android 13)",
                RetroAchievementsManager.userAgent("3.0.338 06aedfe1", 338, "13"));
    }

    @Test
    public void userAgentWithoutTheCommit() {
        // The build leaves the commit out of the version name when git fails.
        assertEquals("Mupen64Turnip/3.0.339 (Android 14)",
                RetroAchievementsManager.userAgent("3.0.339 ", 339, "14"));
    }

    @Test
    public void userAgentWithoutAnAndroidRelease() {
        assertEquals("Mupen64Turnip/3.0.338",
                RetroAchievementsManager.userAgent("3.0.338 06aedfe1", 338, null));
        assertEquals("Mupen64Turnip/3.0.338",
                RetroAchievementsManager.userAgent("3.0.338 06aedfe1", 338, ""));
    }

    @Test
    public void userAgentFallsBackToTheVersionCode() {
        assertEquals("Mupen64Turnip/338 (Android 13)",
                RetroAchievementsManager.userAgent("debug", 338, "13"));
    }

    @Test
    public void requestsSendTheUserAgent() throws IOException {
        FakeConnection connection = new FakeConnection(200, "{\"Success\":true}");
        RetroAchievementsManager.request(url -> connection, SERVER_URL, "r=ping&u=tester", null);
        String userAgent = connection.getRequestProperty("User-Agent");
        assertNotNull(userAgent);
        assertTrue(userAgent, userAgent.matches(USER_AGENT_FORMAT));
    }
}
