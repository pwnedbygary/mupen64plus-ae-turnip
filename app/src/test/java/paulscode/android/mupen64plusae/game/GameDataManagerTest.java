package paulscode.android.mupen64plusae.game;

import org.junit.Test;

import java.io.File;

import static org.junit.Assert.assertEquals;

public class GameDataManagerTest {

    // The core keeps a state's achievement progress in <state folder>/.ra-progress/<state file
    // name>.progress (ra_glue.c), where the auto-save cleanup has to find it.
    @Test
    public void progressFileIsInTheStatesFolder() {
        File state = new File("/games/Mario Tennis/AutoSaves/2026-10-21-09-00-00.v2.sav");

        assertEquals(new File("/games/Mario Tennis/AutoSaves/.ra-progress/2026-10-21-09-00-00.v2.sav.progress"),
                GameDataManager.getRaProgressFile(state));
    }
}
