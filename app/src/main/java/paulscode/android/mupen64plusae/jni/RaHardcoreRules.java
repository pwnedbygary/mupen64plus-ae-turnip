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
     * Only a switch back to the {@code configured} setting is allowed; a session without netplay
     * starts with that setting. With the audio volume at 0 the dummy audio plugin does not pace
     * the game, so turning the limiter off would run it uncapped.
     *
     * @param currentlyEnabled whether the frame limiter is on now, before the switch
     * @param configured whether the frame limiter is on in the settings
     * @return true if hardcore mode allows switching the frame limiter.
     */
    static boolean isFramelimiterToggleAllowed(boolean currentlyEnabled, boolean configured) {
        return currentlyEnabled != configured;
    }
}
