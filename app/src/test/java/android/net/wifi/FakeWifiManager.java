package android.net.wifi;

import java.util.ArrayList;
import java.util.List;

/**
 * A Wi-Fi service for host tests. It is in this package because the SDK's WifiManager and
 * MulticastLock have constructors that only this package can call, and the unit tests'
 * android.jar leaves their methods unimplemented.
 */
public class FakeWifiManager extends WifiManager {

    /** The locks made, in order. */
    public final List<Lock> locks = new ArrayList<>();

    @Override
    public MulticastLock createMulticastLock(String tag) {
        Lock lock = new Lock(tag);
        locks.add(lock);
        return lock;
    }

    /**
     * A multicast lock that counts like the platform's: reference-counted by default, held from
     * its first acquire until as many releases, and throwing when released once more.
     */
    public class Lock extends MulticastLock {
        public final String tag;
        public boolean referenceCounted = true;
        public int acquires;
        public int releases;
        private int mCount;
        private boolean mHeld;

        Lock(String tag) {
            this.tag = tag;
        }

        @Override
        public void acquire() {
            acquires++;
            if (!referenceCounted || ++mCount == 1) {
                mHeld = true;
            }
        }

        @Override
        public void release() {
            releases++;
            if (!referenceCounted || --mCount == 0) {
                mHeld = false;
            }
            if (mCount < 0) {
                throw new RuntimeException("MulticastLock under-locked " + tag);
            }
        }

        @Override
        public void setReferenceCounted(boolean refCounted) {
            referenceCounted = refCounted;
        }

        @Override
        public boolean isHeld() {
            return mHeld;
        }

        @Override
        public String toString() {
            return "FakeWifiManager.Lock " + tag;
        }
    }
}
