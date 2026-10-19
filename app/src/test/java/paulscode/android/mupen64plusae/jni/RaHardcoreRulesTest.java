package paulscode.android.mupen64plusae.jni;

import org.junit.Test;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class RaHardcoreRulesTest {

    @Test
    public void normalSpeedIsAllowed() {
        assertTrue(RaHardcoreRules.isSpeedAllowed(100));
    }

    @Test
    public void fasterSpeedsAreRefused() {
        assertFalse(RaHardcoreRules.isSpeedAllowed(101));
        assertFalse(RaHardcoreRules.isSpeedAllowed(110));
        assertFalse(RaHardcoreRules.isSpeedAllowed(250));
        assertFalse(RaHardcoreRules.isSpeedAllowed(300));
    }

    @Test
    public void slowerSpeedsAreRefused() {
        assertFalse(RaHardcoreRules.isSpeedAllowed(99));
        assertFalse(RaHardcoreRules.isSpeedAllowed(90));
        assertFalse(RaHardcoreRules.isSpeedAllowed(10));
    }

    @Test
    public void framelimiterMayOnlyBeSwitchedOn() {
        assertTrue(RaHardcoreRules.isFramelimiterToggleAllowed(false));
        assertFalse(RaHardcoreRules.isFramelimiterToggleAllowed(true));
    }
}
