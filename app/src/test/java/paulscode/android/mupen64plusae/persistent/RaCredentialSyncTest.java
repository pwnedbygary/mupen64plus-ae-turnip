package paulscode.android.mupen64plusae.persistent;

import android.content.SharedPreferences;

import org.junit.Test;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class RaCredentialSyncTest {

    // The password row's key in res/xml/preferences_ra.xml, and the key the token is saved under
    // in existing settings files.
    private static final String PASSWORD = "retroAchievementsPassword";
    private static final String TOKEN = "retroAchievementsToken";

    @Test
    public void deletedPasswordAndNewTokenReachTheCopy() {
        FakePreferences copy = new FakePreferences()
                .with(PASSWORD, "secret").with(TOKEN, "old").with("otherSetting", "kept");

        assertTrue(RaCredentialSync.apply(copy, "new", true));

        assertEquals("new", copy.getString(TOKEN, null));
        assertFalse(copy.contains(PASSWORD));
        assertEquals("kept", copy.getString("otherSetting", null));
        assertEquals(1, copy.saves);
    }

    @Test
    public void passwordTheOtherProcessKeptStays() {
        FakePreferences copy = new FakePreferences().with(PASSWORD, "secret");

        assertTrue(RaCredentialSync.apply(copy, "new", false));

        assertEquals("new", copy.getString(TOKEN, null));
        assertEquals("secret", copy.getString(PASSWORD, null));
    }

    @Test
    public void passwordBroughtBackBesideTheSameTokenIsDeleted() {
        FakePreferences copy = new FakePreferences().with(PASSWORD, "secret").with(TOKEN, "same");

        assertTrue(RaCredentialSync.apply(copy, "same", true));

        assertEquals("same", copy.getString(TOKEN, null));
        assertFalse(copy.contains(PASSWORD));
    }

    @Test
    public void copyThatAlreadyMatchesIsNotSaved() {
        FakePreferences copy = new FakePreferences().with(TOKEN, "same");

        assertFalse(RaCredentialSync.apply(copy, "same", true));
        assertFalse(RaCredentialSync.apply(copy, "same", false));

        assertEquals(0, copy.saves);
    }

    @Test
    public void passwordIsNeverDeletedWithoutAToken() {
        FakePreferences copy = new FakePreferences().with(PASSWORD, "secret");

        assertFalse(RaCredentialSync.apply(copy, null, true));
        assertFalse(RaCredentialSync.apply(copy, "", true));

        assertEquals("secret", copy.getString(PASSWORD, null));
        assertNull(copy.getString(TOKEN, null));
        assertEquals(0, copy.saves);
    }

    /** Settings held in memory; counts how often an edit is saved. */
    private static final class FakePreferences implements SharedPreferences {
        private final Map<String, Object> values = new HashMap<>();
        int saves;

        FakePreferences with(String key, String value) {
            values.put(key, value);
            return this;
        }

        @Override
        public Map<String, ?> getAll() {
            return new HashMap<>(values);
        }

        @Override
        public String getString(String key, String defValue) {
            return values.containsKey(key) ? (String) values.get(key) : defValue;
        }

        @Override
        @SuppressWarnings("unchecked")
        public Set<String> getStringSet(String key, Set<String> defValues) {
            return values.containsKey(key) ? (Set<String>) values.get(key) : defValues;
        }

        @Override
        public int getInt(String key, int defValue) {
            return values.containsKey(key) ? (Integer) values.get(key) : defValue;
        }

        @Override
        public long getLong(String key, long defValue) {
            return values.containsKey(key) ? (Long) values.get(key) : defValue;
        }

        @Override
        public float getFloat(String key, float defValue) {
            return values.containsKey(key) ? (Float) values.get(key) : defValue;
        }

        @Override
        public boolean getBoolean(String key, boolean defValue) {
            return values.containsKey(key) ? (Boolean) values.get(key) : defValue;
        }

        @Override
        public boolean contains(String key) {
            return values.containsKey(key);
        }

        @Override
        public Editor edit() {
            return new FakeEditor();
        }

        @Override
        public void registerOnSharedPreferenceChangeListener(
                OnSharedPreferenceChangeListener listener) {
        }

        @Override
        public void unregisterOnSharedPreferenceChangeListener(
                OnSharedPreferenceChangeListener listener) {
        }

        private final class FakeEditor implements Editor {
            private final Map<String, Object> puts = new HashMap<>();
            private final Set<String> removals = new HashSet<>();
            private boolean clear;

            @Override
            public Editor putString(String key, String value) {
                puts.put(key, value);
                return this;
            }

            @Override
            public Editor putStringSet(String key, Set<String> values) {
                puts.put(key, values);
                return this;
            }

            @Override
            public Editor putInt(String key, int value) {
                puts.put(key, value);
                return this;
            }

            @Override
            public Editor putLong(String key, long value) {
                puts.put(key, value);
                return this;
            }

            @Override
            public Editor putFloat(String key, float value) {
                puts.put(key, value);
                return this;
            }

            @Override
            public Editor putBoolean(String key, boolean value) {
                puts.put(key, value);
                return this;
            }

            @Override
            public Editor remove(String key) {
                removals.add(key);
                return this;
            }

            @Override
            public Editor clear() {
                clear = true;
                return this;
            }

            @Override
            public boolean commit() {
                if (clear) {
                    values.clear();
                }
                for (String key : removals) {
                    values.remove(key);
                }
                values.putAll(puts);
                saves++;
                return true;
            }

            @Override
            public void apply() {
                commit();
            }
        }
    }
}
