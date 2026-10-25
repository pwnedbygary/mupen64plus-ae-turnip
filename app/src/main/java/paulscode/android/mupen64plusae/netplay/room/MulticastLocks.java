package paulscode.android.mupen64plusae.netplay.room;

import android.net.wifi.WifiManager;

import androidx.annotation.Nullable;

/**
 * The Wi-Fi multicast lock that network service discovery needs to see a netplay room.
 */
final class MulticastLocks
{
    private MulticastLocks()
    {
    }

    /**
     * Takes a multicast lock.
     *
     * @param wifi the Wi-Fi service, which is null on Android 9 and later when the device has no
     *             Wi-Fi, such as a TV on Ethernet only
     * @return the lock, or null without the Wi-Fi service
     */
    @Nullable
    static WifiManager.MulticastLock acquire(@Nullable WifiManager wifi)
    {
        if (wifi == null) {
            return null;
        }

        WifiManager.MulticastLock lock = wifi.createMulticastLock("multicastLock");
        lock.setReferenceCounted(true);
        lock.acquire();
        return lock;
    }

    /**
     * Releases a lock from {@link #acquire}. Later calls and a null lock do nothing, as a
     * reference-counted lock throws when it is released more often than it was taken. The room
     * client can release it from two threads, so the check and the release happen together.
     */
    static void release(@Nullable WifiManager.MulticastLock lock)
    {
        if (lock == null) {
            return;
        }

        synchronized (lock) {
            if (lock.isHeld()) {
                lock.release();
            }
        }
    }
}
