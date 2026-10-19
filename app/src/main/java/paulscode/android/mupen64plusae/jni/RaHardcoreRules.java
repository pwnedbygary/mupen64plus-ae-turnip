package paulscode.android.mupen64plusae.jni;

/** What RetroAchievements hardcore mode allows during a session. */
final class RaHardcoreRules {

    private RaHardcoreRules() {
    }

    /**
     * Normal speed must stay allowed: requesting it is how a session that is off normal speed
     * returns to it.
     *
     * @return true if hardcore mode allows running at {@code speed} percent of normal speed.
     */
    static boolean isSpeedAllowed(int speed) {
        return speed == CoreFragment.BASELINE_SPEED;
    }

    /**
     * Switching the limiter on is always allowed; switching it off is not. A hardcore session
     * starts with it on: with audio sync only the audio output paces the game, and the dummy
     * audio plugin used at volume 0 does not pace it at all.
     *
     * @param currentlyEnabled whether the frame limiter is on now, before the switch
     * @return true if hardcore mode allows switching the frame limiter.
     */
    static boolean isFramelimiterToggleAllowed(boolean currentlyEnabled) {
        return !currentlyEnabled;
    }
}
