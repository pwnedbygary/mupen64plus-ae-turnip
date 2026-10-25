package paulscode.android.mupen64plusae.netplay.room;

import org.junit.Test;

import android.net.wifi.FakeWifiManager;
import android.net.wifi.WifiManager;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

public class MulticastLocksTest {

    @Test
    public void noLockWithoutWifi() {
        assertNull(MulticastLocks.acquire(null));
    }

    @Test
    public void takesOneReferenceCountedLock() {
        FakeWifiManager wifi = new FakeWifiManager();
        WifiManager.MulticastLock lock = MulticastLocks.acquire(wifi);

        assertEquals(1, wifi.locks.size());
        FakeWifiManager.Lock taken = wifi.locks.get(0);
        assertSame(taken, lock);
        assertEquals("multicastLock", taken.tag);
        assertTrue(taken.referenceCounted);
        assertEquals(1, taken.acquires);
        assertTrue(taken.isHeld());
    }

    @Test
    public void releasesOnlyOnceWhenCalledTwice() {
        FakeWifiManager wifi = new FakeWifiManager();
        WifiManager.MulticastLock lock = MulticastLocks.acquire(wifi);

        MulticastLocks.release(lock);
        MulticastLocks.release(lock);

        FakeWifiManager.Lock taken = wifi.locks.get(0);
        assertEquals(1, taken.releases);
        assertFalse(taken.isHeld());
    }

    @Test
    public void releasingNoLockDoesNothing() {
        MulticastLocks.release(null);
    }
}
