/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * Copyright (C) 2013 Paul Lamb
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Mupen64PlusAE is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: littleguy77
 */
package paulscode.android.mupen64plusae;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.MotionEvent;

import androidx.activity.OnBackPressedCallback;
import androidx.appcompat.app.AppCompatActivity;

import paulscode.android.mupen64plusae.util.LocaleContextWrapper;

/**
 * Action-bar-free credit roll shown when the user selects "Credits" in the gallery drawer. The
 * activity fills the app window; the system status and navigation bars stay visible.
 * <p>
 * The roll is drawn in forced perspective: the title and the contributor cards rise from below the
 * bottom edge at full size, are read there, then recede toward a vanishing point in the background
 * and fade into it. Once the roll has run out, the screen holds on the empty dot briefly and
 * returns to the gallery by itself.
 * <p>
 * Any input (key, touch, gamepad, or hardware back) finishes the activity immediately and returns
 * to the gallery. Tapping a contributor's GitHub handle opens that profile first, then leaves the
 * roll.
 */
public class CreditRollActivity extends AppCompatActivity
{
    /**
     * One credited contributor: avatar drawable, display name, GitHub handle and profile URL, plus
     * a string resource for the short description of the contribution. Names and handles are proper
     * nouns and stay in code; the descriptions are visible text and live in {@code strings.xml}.
     */
    private static final class Contributor
    {
        final int avatarResId;
        final String name;
        final String github;
        final String url;
        final int blurbResId;

        Contributor( int avatarResId, String name, String github, String url, int blurbResId )
        {
            this.avatarResId = avatarResId;
            this.name = name;
            this.github = github;
            this.url = url;
            this.blurbResId = blurbResId;
        }
    }

    private static final Contributor[] CONTRIBUTORS = new Contributor[] {
        new Contributor( R.drawable.credit_avatar_pwnedbygary, "Gary Bagley", "pwnedbygary",
            "https://github.com/pwnedbygary", R.string.creditRoll_blurb_pwnedbygary ),
        new Contributor( R.drawable.credit_avatar_fzurita, "Francisco Zurita", "fzurita",
            "https://github.com/fzurita", R.string.creditRoll_blurb_fzurita ),
        new Contributor( R.drawable.credit_avatar_littleguy77, "Matt Lichter", "littleguy77",
            "https://github.com/littleguy77", R.string.creditRoll_blurb_littleguy77 ),
        new Contributor( R.drawable.credit_avatar_ecsv, "Sven Eckelmann", "ecsv",
            "https://github.com/ecsv", R.string.creditRoll_blurb_ecsv ),
        new Contributor( R.drawable.credit_avatar_paulscode, "Paul Lamb", "paulscode",
            "https://github.com/paulscode", R.string.creditRoll_blurb_paulscode ),
        new Contributor( R.drawable.credit_avatar_casualjames, "Gilles Siberlin", "casualjames",
            "https://github.com/casualjames", R.string.creditRoll_blurb_casualjames ),
    };

    /** Delay between roll steps, matching one ~60 Hz frame. */
    private static final long ROLL_FRAME_MS = 16L;

    private static final String STATE_ROLL_PROGRESS = "STATE_ROLL_PROGRESS";

    private final Handler mHandler = new Handler( Looper.getMainLooper() );

    private CreditRollView mStage;
    private long mLastFrameMs;
    private float mSavedProgress;
    private boolean mExiting = false;

    /**
     * Advances the roll by the time since the previous frame, so the roll keeps the same pace no
     * matter how long a frame takes, and returns to the gallery once the roll has run out and the
     * vanishing point has been held on its own.
     */
    private final Runnable mRollRunnable = new Runnable()
    {
        @Override
        public void run()
        {
            if( mExiting )
            {
                return;
            }

            final long now = SystemClock.uptimeMillis();
            mStage.advance( ( now - mLastFrameMs ) / 1000f );
            mLastFrameMs = now;

            if( mStage.isFinished() )
            {
                mHandler.removeCallbacks( this );
                exit();
                return;
            }

            mHandler.postDelayed( this, ROLL_FRAME_MS );
        }
    };

    /**
     * Restores the saved position and starts the roll. The stage posts this once it has been laid
     * out, because the track cannot be laid out before the view knows its width. It is kept as a
     * field so it can be cancelled if the roll is closed or the activity destroyed before the
     * stage gets that far.
     */
    private final Runnable mStartRunnable = new Runnable()
    {
        @Override
        public void run()
        {
            if( mExiting )
            {
                return;
            }

            mStage.setProgress( mSavedProgress );
            mLastFrameMs = SystemClock.uptimeMillis();
            mHandler.postDelayed( mRollRunnable, ROLL_FRAME_MS );
        }
    };

    @Override
    protected void attachBaseContext( Context newBase )
    {
        if( TextUtils.isEmpty( LocaleContextWrapper.getLocalCode() ) )
        {
            super.attachBaseContext( newBase );
        }
        else
        {
            super.attachBaseContext(
                LocaleContextWrapper.wrap( newBase, LocaleContextWrapper.getLocalCode() ) );
        }
    }

    @Override
    protected void onCreate( Bundle savedInstanceState )
    {
        super.onCreate( savedInstanceState );

        // onBackPressed() is deprecated; route the back gesture through the dispatcher.
        getOnBackPressedDispatcher().addCallback( this, new OnBackPressedCallback( true )
        {
            @Override
            public void handleOnBackPressed()
            {
                exit();
            }
        } );

        setContentView( R.layout.credit_roll_activity );

        mStage = findViewById( R.id.creditRollStage );

        // Any touch ends the roll, except a touch on a GitHub handle, which opens that profile.
        mStage.setOnTouchListener( ( view, event ) ->
        {
            if( event.getActionMasked() == MotionEvent.ACTION_DOWN )
            {
                final String url = mStage.urlAt( event.getX(), event.getY() );
                if( url != null )
                {
                    ActivityHelper.launchUri( this, url );
                }
                exit();
            }
            return true;
        } );

        mStage.setTitle( getString( R.string.creditRoll_title ) );
        mStage.setClosing( getString( R.string.creditRoll_thankYou ) );
        for( Contributor contributor : CONTRIBUTORS )
        {
            mStage.addContributor( contributor.avatarResId, contributor.name, contributor.github,
                getString( contributor.blurbResId ), contributor.url );
        }

        mSavedProgress = savedInstanceState == null ? 0f
            : savedInstanceState.getFloat( STATE_ROLL_PROGRESS, 0f );
        // Wait for the first layout pass: the track cannot be laid out before the view knows its
        // width.
        mStage.post( mStartRunnable );
    }

    @Override
    public void onSaveInstanceState( Bundle savedInstanceState )
    {
        if( mStage != null )
        {
            savedInstanceState.putFloat( STATE_ROLL_PROGRESS, mStage.getProgress() );
        }

        super.onSaveInstanceState( savedInstanceState );
    }

    /**
     * Any key press finishes the roll, so a D-pad, gamepad button, or keyboard input returns to the
     * gallery. Key-up events are only consumed, not acted on, so the release of the press used to
     * open this screen cannot close it immediately.
     */
    @Override
    public boolean dispatchKeyEvent( KeyEvent event )
    {
        if( event.getAction() == KeyEvent.ACTION_DOWN )
        {
            exit();
        }
        return true;
    }

    /**
     * Touch fallback for events not consumed by the stage.
     */
    @Override
    public boolean onTouchEvent( MotionEvent event )
    {
        if( event.getActionMasked() == MotionEvent.ACTION_DOWN )
        {
            exit();
        }
        return true;
    }

    /**
     * Any joystick, mouse, or touchpad motion finishes the roll. Hover motion (a cursor entering,
     * moving over, or leaving the window, or a resting stick emitting hover events) is not
     * deliberate input and is ignored.
     */
    @Override
    public boolean onGenericMotionEvent( MotionEvent event )
    {
        final int action = event.getActionMasked();
        if( action == MotionEvent.ACTION_HOVER_ENTER
            || action == MotionEvent.ACTION_HOVER_MOVE
            || action == MotionEvent.ACTION_HOVER_EXIT )
        {
            return true;
        }

        exit();
        return true;
    }

    /**
     * Finishes the activity at most once and stops the roll.
     */
    private void exit()
    {
        if( !mExiting )
        {
            mExiting = true;
            stopRoll();
            finish();
        }
    }

    /**
     * Cancels every pending roll callback, whether the roll is starting, running, or already done.
     */
    private void stopRoll()
    {
        mHandler.removeCallbacksAndMessages( null );
        if( mStage != null )
        {
            mStage.removeCallbacks( mStartRunnable );
        }
    }

    @Override
    protected void onDestroy()
    {
        stopRoll();
        super.onDestroy();
    }
}
