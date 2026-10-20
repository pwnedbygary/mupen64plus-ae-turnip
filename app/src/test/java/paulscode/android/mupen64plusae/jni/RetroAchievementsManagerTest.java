package paulscode.android.mupen64plusae.jni;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class RetroAchievementsManagerTest {

    private static final int MB = 1024 * 1024;

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
}
