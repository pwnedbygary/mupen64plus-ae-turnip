package paulscode.android.mupen64plusae.game;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class RaIndicatorStateTest {

    @Test
    public void challengesAreListedInTheOrderTheyStarted() {
        RaIndicatorState state = new RaIndicatorState();
        // Sorting by ID or title, either way, or HashMap order would list these differently.
        state.showChallenge(30, "Flawless");
        state.showChallenge(10, "Speedrun");
        state.showChallenge(20, "No damage");
        assertEquals(Arrays.asList("Flawless", "Speedrun", "No damage"), state.challengeTitles());
    }

    @Test
    public void aChallengeShownAgainIsListedOnce() {
        RaIndicatorState state = new RaIndicatorState();
        state.showChallenge(10, "No damage");
        state.showChallenge(20, "Flawless");
        state.showChallenge(10, "No damage");
        assertEquals(Arrays.asList("No damage", "Flawless"), state.challengeTitles());
    }

    @Test
    public void hidingAChallengeRemovesOnlyThatOne() {
        RaIndicatorState state = new RaIndicatorState();
        state.showChallenge(10, "No damage");
        state.showChallenge(20, "Flawless");
        state.hideChallenge(10);
        assertEquals(Collections.singletonList("Flawless"), state.challengeTitles());
        state.hideChallenge(30);
        assertEquals(Collections.singletonList("Flawless"), state.challengeTitles());
    }

    @Test
    public void progressShowsTheLatestReport() {
        RaIndicatorState state = new RaIndicatorState();
        state.showProgress("Collector", "3/10");
        state.showProgress("Hoarder", "40%");
        assertTrue(state.hasProgress());
        assertEquals("Hoarder", state.progressTitle());
        assertEquals("40%", state.progressText());
    }

    @Test
    public void progressHidesWithoutAnAchievement() {
        RaIndicatorState state = new RaIndicatorState();
        state.showProgress("Collector", "3/10");
        state.hideProgress();
        assertFalse(state.hasProgress());
    }

    @Test
    public void emptyProgressTextShowsNothing() {
        RaIndicatorState state = new RaIndicatorState();
        state.showProgress("Collector", "");
        assertFalse(state.hasProgress());
    }
}
