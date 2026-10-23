package paulscode.android.mupen64plusae.game;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What the RetroAchievements indicators show: the achievements whose challenge is active and the
 * values of the leaderboard trackers, each in the order rcheevos showed them, and the progress
 * rcheevos reported last.
 */
final class RaIndicatorState {
    private final Map<Long, String> mChallenges = new LinkedHashMap<>();
    private final Map<Long, String> mTrackers = new LinkedHashMap<>();
    private String mProgressTitle = "";
    private String mProgressText = "";

    void showChallenge(long id, String title) {
        mChallenges.put(id, title);
    }

    void hideChallenge(long id) {
        mChallenges.remove(id);
    }

    List<String> challengeTitles() {
        return new ArrayList<>(mChallenges.values());
    }

    /** rcheevos shares a tracker between leaderboards with the same value, so it names none. */
    void showTracker(long id, String value) {
        mTrackers.put(id, value);
    }

    void hideTracker(long id) {
        mTrackers.remove(id);
    }

    List<String> trackerValues() {
        return new ArrayList<>(mTrackers.values());
    }

    void showProgress(String title, String text) {
        mProgressTitle = title;
        mProgressText = text;
    }

    /** Hides whatever progress is shown: rcheevos's timeout hide names no achievement. */
    void hideProgress() {
        mProgressTitle = "";
        mProgressText = "";
    }

    /** rcheevos leaves the progress text empty when it has none to show, for example below 1%. */
    boolean hasProgress() {
        return !mProgressText.isEmpty();
    }

    String progressTitle() {
        return mProgressTitle;
    }

    String progressText() {
        return mProgressText;
    }
}
