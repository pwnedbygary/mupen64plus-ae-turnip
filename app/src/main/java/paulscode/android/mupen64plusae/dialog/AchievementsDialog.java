package paulscode.android.mupen64plusae.dialog;

import android.app.Activity;
import android.content.Context;
import android.text.TextUtils;
import android.util.Log;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;
import androidx.core.widget.ImageViewCompat;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.jni.RetroAchievementsManager;
import paulscode.android.mupen64plusae.ui.UiTheme;

/** Read-only list of the loaded game's RetroAchievements, opened from the in-game menu. */
public final class AchievementsDialog {

    private static final String TAG = "AchievementsDialog";

    private AchievementsDialog() {}

    private static final class Row {
        final CharSequence title;
        final CharSequence detail;
        final int iconVisibility;

        Row(CharSequence title, CharSequence detail, int iconVisibility) {
            this.title = title;
            this.detail = detail;
            this.iconVisibility = iconVisibility;
        }
    }

    public static void show(Activity activity) {
        String title = null;
        List<Row> rows = null;

        RetroAchievementsManager manager = RetroAchievementsManager.peekInstance();
        if (manager != null && manager.isInitialized() && manager.isGameLoaded()) {
            String summaryJson = manager.getSummaryJson();
            String achievementsJson = manager.getAchievementsJson();
            if (summaryJson != null && achievementsJson != null) {
                try {
                    JSONObject summary = new JSONObject(summaryJson);
                    title = summary.getString("game");
                    rows = buildRows(activity, summary,
                            new JSONObject(achievementsJson).getJSONArray("achievements"));
                } catch (JSONException e) {
                    Log.w(TAG, "Could not read the achievement list", e);
                }
            }
        }

        AlertDialog.Builder builder = ThemedAlertDialog.newBuilder(activity)
                .setTitle(TextUtils.isEmpty(title) ? activity.getString(R.string.menuItem_achievements) : title)
                .setPositiveButton(android.R.string.ok, null);
        if (rows == null) {
            builder.setMessage(R.string.achievements_unavailable);
        } else {
            builder.setAdapter(Prompt.createAdapter(activity, rows, AchievementsDialog::populateRow), null);
        }

        AlertDialog dialog = builder.create();
        dialog.setOnShowListener(d -> UiTheme.get(activity).applyToDialog(dialog));
        dialog.show();
    }

    private static List<Row> buildRows(Context context, JSONObject summary, JSONArray achievements)
            throws JSONException {
        List<Row> warningRows = new ArrayList<>();
        List<Row> achievementRows = new ArrayList<>();
        int unlockedAchievements = 0;
        int totalPoints = 0;
        int unlockedPoints = 0;

        for (int i = 0; i < achievements.length(); i++) {
            JSONObject achievement = achievements.getJSONObject(i);
            String title = achievement.getString("title");
            String description = achievement.getString("description");

            if (achievement.getLong("id") >= RetroAchievementsManager.WARNING_ACHIEVEMENT_ID) {
                warningRows.add(new Row(title, description, View.GONE));
                continue;
            }

            boolean unlocked = achievement.getInt("unlocked") != 0;
            int points = achievement.getInt("points");
            totalPoints += points;
            if (unlocked) {
                unlockedAchievements++;
                unlockedPoints += points;
            }
            String status = context.getResources().getQuantityString(
                    unlocked ? R.plurals.achievements_unlocked : R.plurals.achievements_locked, points, points);
            // INVISIBLE, not GONE, keeps a locked row's text aligned with the unlocked rows.
            achievementRows.add(new Row(title,
                    TextUtils.isEmpty(description) ? status : description + "\n" + status,
                    unlocked ? View.VISIBLE : View.INVISIBLE));
        }

        // The summary JSON counts only the game's main achievement set, but the list has every set
        // the game loaded, so the totals are counted from the rows.
        List<Row> rows = new ArrayList<>();
        int summaryText = summary.getInt("hardcore") != 0 ?
                R.string.achievements_summaryHardcore : R.string.achievements_summarySoftcore;
        rows.add(new Row(
                context.getString(summaryText, unlockedAchievements, achievementRows.size()),
                context.getResources().getQuantityString(R.plurals.achievements_summaryPoints, totalPoints,
                        unlockedPoints, totalPoints),
                View.GONE));
        rows.addAll(warningRows);

        if (achievementRows.isEmpty()) {
            rows.add(new Row(context.getString(R.string.achievements_empty), null, View.GONE));
        }
        rows.addAll(achievementRows);
        return rows;
    }

    private static void populateRow(Row row, int position, TextView text1, TextView text2, ImageView icon) {
        text1.setText(row.title);
        text2.setText(row.detail);
        text2.setVisibility(TextUtils.isEmpty(row.detail) ? View.GONE : View.VISIBLE);
        icon.setImageResource(R.drawable.ic_check);
        ImageViewCompat.setImageTintList(icon, text1.getTextColors());
        icon.setVisibility(row.iconVisibility);
    }
}
