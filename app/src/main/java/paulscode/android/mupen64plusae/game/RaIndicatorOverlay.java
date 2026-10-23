package paulscode.android.mupen64plusae.game;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.Nullable;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.List;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.jni.RetroAchievementsManager;

/**
 * Shows the RetroAchievements indicators over the game: a line for each achievement whose
 * challenge is active, a line for each leaderboard tracker, which holds the value of one or more
 * running attempts, and a line with the progress rcheevos reported last, which rcheevos hides two
 * seconds after the last change. It handles no touches, so they reach the views below it.
 */
public class RaIndicatorOverlay extends LinearLayout implements RetroAchievementsManager.Listener {
    private static final String TAG = "RaIndicatorOverlay";

    private final RaIndicatorState mState = new RaIndicatorState();
    private final LinearLayout mChallengeLines;
    private final LinearLayout mTrackerLines;
    private final TextView mProgressLine;
    private RetroAchievementsManager mManager;

    public RaIndicatorOverlay(Context context, AttributeSet attrs) {
        super(context, attrs);
        setOrientation(VERTICAL);
        setGravity(Gravity.CENTER_HORIZONTAL);

        mChallengeLines = new LinearLayout(context);
        mChallengeLines.setOrientation(VERTICAL);
        mChallengeLines.setGravity(Gravity.CENTER_HORIZONTAL);
        addView(mChallengeLines);

        mTrackerLines = new LinearLayout(context);
        mTrackerLines.setOrientation(VERTICAL);
        mTrackerLines.setGravity(Gravity.CENTER_HORIZONTAL);
        addView(mTrackerLines);

        mProgressLine = newLine(context);
        mProgressLine.setVisibility(GONE);
        addView(mProgressLine);
    }

    /** Shows the indicators of the session in this process until {@link #stop()}. */
    public void start(RetroAchievementsManager manager) {
        mManager = manager;
        manager.addListener(this);
        setVisibility(VISIBLE);

        // An activity created for a session that is already running starts with its challenges.
        // rcheevos lists no trackers, so the value of a running leaderboard attempt shows again
        // when it next changes.
        if (manager.isGameLoaded()) {
            addActiveChallenges(manager.getAchievementsJson());
        }
        showChallenges();
    }

    public void stop() {
        if (mManager != null) {
            mManager.removeListener(this);
            mManager = null;
        }
    }

    @Override
    public void onRaEvent(String type, String json) {
        try {
            switch (type) {
            case "challenge_show": {
                JSONObject event = new JSONObject(json);
                mState.showChallenge(event.getLong("id"), event.getString("title"));
                showChallenges();
                break;
            }
            case "challenge_hide":
                mState.hideChallenge(new JSONObject(json).getLong("id"));
                showChallenges();
                break;
            case "tracker_show":
            case "tracker_update": {
                JSONObject event = new JSONObject(json);
                mState.showTracker(event.getLong("id"), event.getString("display"));
                showTrackers();
                break;
            }
            case "tracker_hide":
                mState.hideTracker(new JSONObject(json).getLong("id"));
                showTrackers();
                break;
            case "progress_show":
            case "progress_update": {
                JSONObject event = new JSONObject(json);
                mState.showProgress(event.getString("title"), event.getString("progress"));
                showProgress();
                break;
            }
            case "progress_hide":
                mState.hideProgress();
                showProgress();
                break;
            default:
                break;
            }
        } catch (JSONException e) {
            Log.w(TAG, "Malformed RetroAchievements " + type + " event: " + json);
        }
    }

    @Override
    public void onRaAsyncResult(int result, @Nullable String errorMessage) {
    }

    private void addActiveChallenges(@Nullable String achievementsJson) {
        if (achievementsJson == null) {
            return;
        }
        try {
            JSONArray achievements = new JSONObject(achievementsJson).getJSONArray("achievements");
            for (int i = 0; i < achievements.length(); i++) {
                JSONObject achievement = achievements.getJSONObject(i);
                if (achievement.getInt("challenge") != 0) {
                    mState.showChallenge(achievement.getLong("id"), achievement.getString("title"));
                }
            }
        } catch (JSONException e) {
            Log.w(TAG, "Unreadable RetroAchievements achievement list", e);
        }
    }

    private void showChallenges() {
        mChallengeLines.removeAllViews();
        for (String title : mState.challengeTitles()) {
            TextView line = newLine(getContext());
            line.setText(getContext().getString(R.string.ra_indicatorChallenge, title));
            mChallengeLines.addView(line);
        }
    }

    /**
     * Reuses the lines it has: a timer's value changes every frame. A fixed-width font keeps a
     * changing value from shifting the line.
     */
    private void showTrackers() {
        List<String> values = mState.trackerValues();
        while (mTrackerLines.getChildCount() > values.size()) {
            mTrackerLines.removeViewAt(mTrackerLines.getChildCount() - 1);
        }
        while (mTrackerLines.getChildCount() < values.size()) {
            TextView line = newLine(getContext());
            line.setTypeface(Typeface.MONOSPACE);
            mTrackerLines.addView(line);
        }
        for (int i = 0; i < values.size(); i++) {
            ((TextView) mTrackerLines.getChildAt(i)).setText(
                    getContext().getString(R.string.ra_indicatorLeaderboard, values.get(i)));
        }
    }

    private void showProgress() {
        if (mState.hasProgress()) {
            mProgressLine.setText(getContext().getString(R.string.ra_indicatorProgress,
                    mState.progressTitle(), mState.progressText()));
            mProgressLine.setVisibility(VISIBLE);
        } else {
            mProgressLine.setVisibility(GONE);
        }
    }

    private static TextView newLine(Context context) {
        TextView line = new TextView(context);
        line.setTextColor(Color.WHITE);
        line.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        line.setSingleLine(true);
        line.setEllipsize(TextUtils.TruncateAt.END);
        line.setBackgroundResource(R.drawable.ra_indicator_background);
        int horizontal = dpToPx(context, 10);
        int vertical = dpToPx(context, 4);
        line.setPadding(horizontal, vertical, horizontal, vertical);
        LayoutParams params = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        params.topMargin = dpToPx(context, 4);
        line.setLayoutParams(params);
        return line;
    }

    private static int dpToPx(Context context, int dp) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp,
                context.getResources().getDisplayMetrics()));
    }
}
