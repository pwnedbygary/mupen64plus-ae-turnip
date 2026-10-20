package paulscode.android.mupen64plusae.game;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What the RetroAchievements indicators show: the achievements whose challenge is active, in the
 * order they started, and the progress rcheevos reported last.
 */
final class RaIndicatorState {
    private final Map<Long, String> mChallenges = new LinkedHashMap<>();
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
