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
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * Draws a credit roll in forced perspective, in the style of the Star Wars opening crawl: blocks
 * rise from below the bottom edge at full size, are read there, then recede toward a single
 * vanishing point and fade into it.
 * <p>
 * The track is laid out once in flat pixels, one block per contributor, and the whole roll then
 * moves away from the viewer. Every block at depth {@code d} is drawn with a 1/z divide:
 * <pre>
 *     scale = readDepth / (readDepth + d)
 *     y     = horizonY + (height - horizonY + offsetInBlock) * scale
 * </pre>
 * The nearest block therefore sits at full size on the bottom edge, and the blocks behind it
 * compress toward the vanishing point. The depth gap between blocks is one
 * {@code FADE_END * readDepth}, the depth from the viewer plane at which a block's fade has ended,
 * and that is also the block-to-block gap, so one block hands over to the next exactly at the
 * vanishing point: the next block starts rising when the outgoing one has already faded to 20%
 * opacity at 43% of its size, and the two are never both readable. The fade itself, from full
 * opacity to zero, spans {@code FADE_END - FADE_START} read depths.
 * <p>
 * A block is a billboard: its avatar and its lines of text share the block's depth and scale and
 * keep their natural spacing inside it, so a card stays readable as it passes. Each line and avatar
 * is still positioned and drawn individually rather than as pre-laid out views, because a view tree
 * can only be transformed as a rectangle. Glyphs themselves are not skewed, since Canvas cannot
 * warp text.
 */
public class CreditRollView extends View
{
    /**
     * Read depth as a multiple of the space between the vanishing point and the bottom edge: a
     * block twice as deep as another is drawn half as tall.
     */
    private static final float READ_DEPTH_FACTOR = 0.6f;

    /**
     * Depth, in read depths, at which a block has faded out completely at the vanishing point. The
     * depth gap between blocks is set to the same distance, so one block fades out at the dot
     * exactly as the next one rises into view at the bottom edge.
     */
    private static final float FADE_END = 1.6f;

    /** Depth, in read depths, at which a block is fully opaque. */
    private static final float FADE_START = 0.35f;

    /** Depth, in read depths, below the viewer plane where a block starts fading in. */
    private static final float ENTRY_FADE = 0.25f;

    /** Fraction of the total roll spent scrolling the blocks up to the vanishing point. */
    private static final float CRAWL_FRACTION = 0.9f;

    /** Total time of the roll, in seconds, including the hold on the empty vanishing point. */
    private static final float ROLL_SECONDS = 30f;

    /** Vanishing point position, as a fraction of the view height measured from the top. */
    private static final float HORIZON_FRACTION = 0.25f;

    /** Radius of the bright core of the vanishing-point dot, in dp. */
    private static final float DOT_CORE_DP = 2f;

    /** Radius of the vanishing-point dot's glow, in dp. */
    private static final float DOT_GLOW_DP = 18f;

    /** Horizontal margin of the track, in dp. */
    private static final float SIDE_MARGIN_DP = 24f;

    /** Avatar edge length, in dp. */
    private static final float AVATAR_DP = 96f;

    /** Gap above a contributor's name, in dp. */
    private static final float NAME_GAP_DP = 16f;

    /** Gap above a contributor's GitHub handle, in dp. */
    private static final float HANDLE_GAP_DP = 8f;

    /** Gap above a contributor's blurb, in dp. */
    private static final float BLURB_GAP_DP = 12f;

    /** Space below a contributor card, in dp. */
    private static final float CARD_GAP_DP = 56f;

    /** Padding inside the closing panel, in dp. */
    private static final float CLOSING_PANEL_PADDING_DP = 28f;

    /** Extra touch padding around a GitHub handle, in dp. */
    private static final float HANDLE_TOUCH_PADDING_DP = 10f;

    private static final float TITLE_SIZE_SP = 34f;
    private static final float NAME_SIZE_SP = 22f;
    private static final float HANDLE_SIZE_SP = 16f;
    private static final float BLURB_SIZE_SP = 15f;
    private static final float CLOSING_SIZE_SP = 28f;

    private static final int HANDLE_COLOR = 0xFF8AB4F8;
    private static final int BLURB_COLOR = 0xB3FFFFFF;
    private static final int CLOSING_PANEL_COLOR = 0x33FFFFFF;

    /**
     * One drawable element of the track: a single laid-out line of text, or an avatar bitmap. Every
     * line is its own element so it can be placed and scaled on its own, while the elements of one
     * block share a depth and stay a fixed distance apart inside it.
     */
    private static final class Element
    {
        final Layout layout;
        final TextPaint paint;
        final int line;
        final Bitmap avatar;
        final float depth;
        final float offsetY;
        final String url;
        final int panelColor;
        final int baseAlpha;
        final float halfWidth;
        final float halfHeight;

        /** Screen bounds on the last drawn frame, used for GitHub handle hit-testing. */
        final RectF bounds = new RectF();

        Element( Layout layout, TextPaint paint, int line, float depth, float offsetY, String url,
            int panelColor )
        {
            this.layout = layout;
            this.paint = paint;
            this.line = line;
            this.avatar = null;
            this.depth = depth;
            this.offsetY = offsetY;
            this.url = url;
            this.panelColor = panelColor;
            this.baseAlpha = Color.alpha( paint.getColor() );
            this.halfWidth = layout.getLineWidth( line ) / 2f;
            this.halfHeight =
                ( layout.getLineAscent( line ) + layout.getLineDescent( line ) ) / 2f;
        }

        Element( Bitmap avatar, float depth, float offsetY )
        {
            this.layout = null;
            this.paint = null;
            this.line = -1;
            this.avatar = avatar;
            this.depth = depth;
            this.offsetY = offsetY;
            this.url = null;
            this.panelColor = 0;
            this.baseAlpha = 0;
            this.halfWidth = 0f;
            this.halfHeight = 0f;
        }
    }

    /** Raw content of one contributor, kept until the view knows its width and can lay it out. */
    private static final class Contributor
    {
        final int avatarResId;
        final CharSequence name;
        final CharSequence handle;
        final CharSequence blurb;
        final String url;

        Contributor( int avatarResId, CharSequence name, CharSequence handle, CharSequence blurb,
            String url )
        {
            this.avatarResId = avatarResId;
            this.name = name;
            this.handle = handle;
            this.blurb = blurb;
            this.url = url;
        }
    }

    private final List<Contributor> mContributors = new ArrayList<>();
    private final List<Element> mElements = new ArrayList<>();
    private final List<Element> mHandles = new ArrayList<>();

    private final TextPaint mTitlePaint = textPaint( Color.WHITE, TITLE_SIZE_SP, true, 0.2f );
    private final TextPaint mNamePaint = textPaint( Color.WHITE, NAME_SIZE_SP, true, 0f );
    private final TextPaint mHandlePaint = textPaint( HANDLE_COLOR, HANDLE_SIZE_SP, false, 0f );
    private final TextPaint mBlurbPaint = textPaint( BLURB_COLOR, BLURB_SIZE_SP, false, 0f );
    private final TextPaint mClosingPaint = textPaint( Color.WHITE, CLOSING_SIZE_SP, true, 0f );

    private final Paint mAvatarPaint =
        new Paint( Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG );
    private final Paint mPanelPaint = new Paint( Paint.ANTI_ALIAS_FLAG );
    private final Paint mGlowPaint = new Paint( Paint.ANTI_ALIAS_FLAG );
    private final Paint mCorePaint = new Paint( Paint.ANTI_ALIAS_FLAG );

    private CharSequence mTitle;
    private CharSequence mClosing;

    /** How far the blocks have travelled toward the vanishing point, in flat pixels. */
    private float mCrawl;

    /** Flat depth at which a block is drawn at full size. */
    private float mReadDepth = 1f;

    /** Crawl distance at which the closing panel reaches its hold depth. */
    private float mCrawlEnd = 1f;

    /** Seconds of roll, used to derive the crawl speed from the track length. */
    private float mElapsed;

    private float mHorizonY;
    private float mContentWidth;
    private float mAvatarSize;
    private boolean mLaidOut;

    public CreditRollView( Context context )
    {
        this( context, null );
    }

    public CreditRollView( Context context, AttributeSet attrs )
    {
        super( context, attrs );
        setBackgroundColor( Color.BLACK );

        mPanelPaint.setStyle( Paint.Style.FILL );
        mCorePaint.setColor( Color.WHITE );
    }

    /** Sets the opening title, which scrolls away first. */
    public void setTitle( CharSequence title )
    {
        mTitle = title;
        layoutTrack();
    }

    /** Sets the closing panel, the last element and the one that recedes into the dot. */
    public void setClosing( CharSequence closing )
    {
        mClosing = closing;
        layoutTrack();
    }

    /**
     * Appends one contributor card to the roll.
     *
     * @param avatarResId drawable to show, or 0 for no avatar
     * @param name display name
     * @param handle GitHub handle, which stays tappable while it is on screen
     * @param blurb short description of the contribution
     * @param url profile the handle opens
     */
    public void addContributor( int avatarResId, CharSequence name, CharSequence handle,
        CharSequence blurb, String url )
    {
        mContributors.add( new Contributor( avatarResId, name, handle, blurb, url ) );
        layoutTrack();
    }

    /**
     * Advances the roll by a time step, moving every block toward the vanishing point until the
     * roll has run out and the empty dot is held.
     */
    public void advance( float seconds )
    {
        if( !mLaidOut )
        {
            return;
        }

        mElapsed = Math.min( ROLL_SECONDS, mElapsed + seconds );
        invalidate();
    }

    /**
     * Returns whether the closing panel has finished receding into the vanishing point.
     */
    public boolean isFinished()
    {
        return mLaidOut && mElapsed >= ROLL_SECONDS;
    }

    /**
     * Returns how far the roll has run, from 0 to 1, for saving across a configuration change.
     */
    public float getProgress()
    {
        return ROLL_SECONDS > 0f ? Math.min( 1f, mElapsed / ROLL_SECONDS ) : 0f;
    }

    /**
     * Restores a roll position saved by {@link #getProgress()}.
     */
    public void setProgress( float progress )
    {
        mElapsed = Math.max( 0f, Math.min( 1f, progress ) ) * ROLL_SECONDS;
        invalidate();
    }

    /**
     * Returns the profile URL of the GitHub handle under the given view coordinates, or null when
     * the point is not on a handle.
     */
    public String urlAt( float x, float y )
    {
        for( Element handle : mHandles )
        {
            if( handle.bounds.contains( x, y ) )
            {
                return handle.url;
            }
        }
        return null;
    }

    @Override
    protected void onSizeChanged( int width, int height, int oldWidth, int oldHeight )
    {
        super.onSizeChanged( width, height, oldWidth, oldHeight );

        mHorizonY = height * HORIZON_FRACTION;
        mContentWidth = Math.max( 1f, width - 2f * dp( SIDE_MARGIN_DP ) );
        mAvatarSize = dp( AVATAR_DP );

        mGlowPaint.setShader( new RadialGradient( width / 2f, mHorizonY, dp( DOT_GLOW_DP ),
            new int[] { 0x59FFFFFF, 0x00FFFFFF }, new float[] { 0f, 1f },
            Shader.TileMode.CLAMP ) );

        layoutTrack();
    }

    @Override
    protected void onDraw( Canvas canvas )
    {
        super.onDraw( canvas );

        if( !mLaidOut )
        {
            return;
        }

        final float centerX = getWidth() / 2f;
        final float crawlFraction = Math.min( 1f, mElapsed / ( ROLL_SECONDS * CRAWL_FRACTION ) );
        final float progress = Math.min( 1f, mElapsed / ROLL_SECONDS );

        mCrawl = crawlFraction * mCrawlEnd;

        // Handle bounds are filled in as the handles are drawn, so clearing them here means only
        // the handles that are on screen this frame can be tapped.
        for( Element handle : mHandles )
        {
            handle.bounds.setEmpty();
        }

        for( Element element : mElements )
        {
            final float depth = element.depth + mCrawl;
            if( depth <= 0f )
            {
                continue;
            }

            final float scale = mReadDepth / ( mReadDepth + depth );
            if( scale < 0.02f )
            {
                continue;
            }

            final float alpha = fadeAlpha( depth );
            if( alpha <= 0f )
            {
                continue;
            }

            final float y = mHorizonY + ( getHeight() - mHorizonY + element.offsetY ) * scale;
            if( element.avatar != null )
            {
                drawAvatar( canvas, element, centerX, y, scale, alpha );
            }
            else
            {
                drawLine( canvas, element, centerX, y, scale, alpha );
            }
        }

        drawDot( canvas, centerX, progress );
    }

    /**
     * Lays the flat track out: every block is wrapped once at the content width, then the blocks
     * are stacked with their gaps so that each element has a flat depth.
     */
    private void layoutTrack()
    {
        mElements.clear();
        mHandles.clear();
        mLaidOut = false;

        if( mContentWidth <= 0f || mTitle == null || mClosing == null )
        {
            return;
        }

        final StringBuilder description = new StringBuilder( mTitle );

        mReadDepth = Math.max( 1f, ( getHeight() - mHorizonY ) * READ_DEPTH_FACTOR );

        // Every later block starts one slot nearer than the viewer, which is below the bottom edge,
        // and climbs into view as the roll advances, so the blocks hand over in order: the next one
        // begins rising when the outgoing one is already a faint speck at the dot.
        final float slot = FADE_END * mReadDepth;

        // Opening title, the first block to rise into view.
        final Layout title = buildLayout( mTitle, mTitlePaint );
        addLayout( title, mTitlePaint, 0f, 0f, null, 0 );

        float depth = 0f;
        for( Contributor contributor : mContributors )
        {
            final boolean hasAvatar = contributor.avatarResId != 0;
            final Layout name = buildLayout( contributor.name, mNamePaint );
            final Layout handle = buildLayout( contributor.handle, mHandlePaint );
            final Layout blurb = contributor.blurb != null && contributor.blurb.length() > 0
                ? buildLayout( contributor.blurb, mBlurbPaint ) : null;

            float cardHeight = name.getHeight() + dp( HANDLE_GAP_DP ) + handle.getHeight()
                + dp( CARD_GAP_DP );
            if( hasAvatar )
            {
                cardHeight += mAvatarSize + dp( NAME_GAP_DP );
            }
            if( blurb != null )
            {
                cardHeight += dp( BLURB_GAP_DP ) + blurb.getHeight();
            }

            // A card is one billboard: every line and the avatar share the card's depth and scale,
            // and keep their natural spacing inside it.
            depth -= slot;
            float offset = -cardHeight / 2f;
            if( hasAvatar )
            {
                final Bitmap avatar = BitmapFactory.decodeResource( getResources(),
                    contributor.avatarResId );
                if( avatar != null )
                {
                    mElements.add( new Element( avatar, depth, offset + mAvatarSize / 2f ) );
                }
                offset += mAvatarSize + dp( NAME_GAP_DP );
            }

            addLayout( name, mNamePaint, depth, offset + name.getHeight() / 2f, null, 0 );
            offset += name.getHeight() + dp( HANDLE_GAP_DP );

            addLayout( handle, mHandlePaint, depth, offset + handle.getHeight() / 2f,
                contributor.url, 0 );
            mHandles.add( mElements.get( mElements.size() - 1 ) );
            offset += handle.getHeight();

            if( blurb != null )
            {
                addLayout( blurb, mBlurbPaint, depth,
                    offset + dp( BLURB_GAP_DP ) + blurb.getHeight() / 2f, null, 0 );
            }

            description.append( ", " ).append( contributor.name ).append( ", " )
                .append( getResources().getString( R.string.creditRoll_viewOnGitHub ) )
                .append( ": " ).append( contributor.handle );
            if( contributor.blurb != null && contributor.blurb.length() > 0 )
            {
                description.append( ". " ).append( contributor.blurb );
            }
        }

        final Layout closing = buildLayout( mClosing, mClosingPaint );
        depth -= slot;
        addLayout( closing, mClosingPaint, depth, 0f, null, CLOSING_PANEL_COLOR );

        description.append( ", " ).append( mClosing );

        mCrawlEnd = Math.max( 1f, -depth + FADE_END * mReadDepth );
        mLaidOut = true;

        setContentDescription( description.toString() );
    }

    /**
     * Appends every line of a laid-out block as its own element, all sharing the block's depth and
     * placed at their natural offset inside it.
     */
    private void addLayout( Layout layout, TextPaint paint, float depth, float offset, String url,
        int panelColor )
    {
        for( int line = 0; line < layout.getLineCount(); line++ )
        {
            final float lineOffset = offset
                + ( layout.getLineTop( line ) + layout.getLineBottom( line ) ) / 2f
                - layout.getHeight() / 2f;
            mElements.add( new Element( layout, paint, line, depth, lineOffset, url, panelColor ) );
        }
    }

    private Layout buildLayout( CharSequence text, TextPaint paint )
    {
        return StaticLayout.Builder.obtain( text, 0, text.length(), paint, (int) mContentWidth )
            .setAlignment( Layout.Alignment.ALIGN_CENTER )
            .setIncludePad( false )
            .setLineSpacing( 0f, 1.2f )
            .build();
    }

    private void drawLine( Canvas canvas, Element element, float centerX, float y, float scale,
        float alpha )
    {
        final int start = element.layout.getLineStart( element.line );
        final int end = element.layout.getLineEnd( element.line );
        final float halfWidth = element.halfWidth * scale;
        final float halfHeight = element.halfHeight * scale;

        if( element.panelColor != 0 )
        {
            final float padding = dp( CLOSING_PANEL_PADDING_DP ) * scale;
            mPanelPaint.setColor( element.panelColor );
            // setAlpha replaces the color's own alpha, so the panel has to fold in the alpha it
            // was authored with, or the closing panel stays solid while its text fades away.
            mPanelPaint.setAlpha( (int) ( alpha * Color.alpha( element.panelColor ) ) );
            canvas.drawRoundRect( centerX - halfWidth - padding, y - halfHeight - padding,
                centerX + halfWidth + padding, y + halfHeight + padding, padding, padding,
                mPanelPaint );
        }

        if( element.url != null )
        {
            final float touchPadding = dp( HANDLE_TOUCH_PADDING_DP );
            element.bounds.set( centerX - halfWidth - touchPadding, y - halfHeight - touchPadding,
                centerX + halfWidth + touchPadding, y + halfHeight + touchPadding );
        }
        else
        {
            element.bounds.set( centerX - halfWidth, y - halfHeight, centerX + halfWidth,
                y + halfHeight );
        }

        // setAlpha replaces the color's own alpha, so the roll alpha is combined with the alpha the
        // paint was authored with; otherwise the softer blurb color would be drawn fully opaque.
        element.paint.setAlpha( (int) ( alpha * element.baseAlpha ) );

        canvas.save();
        canvas.translate( centerX, y );
        canvas.scale( scale, scale );
        // Center the line on the origin: the baseline sits half way between the line's ascent and
        // its descent.
        canvas.drawText( element.layout.getText(), start, end, -element.halfWidth,
            -element.halfHeight, element.paint );
        canvas.restore();
    }

    private void drawAvatar( Canvas canvas, Element element, float centerX, float y, float scale,
        float alpha )
    {
        final float size = mAvatarSize * scale;
        final Bitmap avatar = element.avatar;
        final float fit =
            Math.min( size / avatar.getWidth(), size / avatar.getHeight() );

        mAvatarPaint.setAlpha( (int) ( alpha * 255f ) );
        canvas.drawBitmap( avatar, null, new RectF( centerX - avatar.getWidth() * fit / 2f,
            y - avatar.getHeight() * fit / 2f, centerX + avatar.getWidth() * fit / 2f,
            y + avatar.getHeight() * fit / 2f ), mAvatarPaint );
    }

    /**
     * Returns the opacity of a block at the given depth: it fades in as the block rises past the
     * bottom edge, is fully opaque while it is close enough to read, and fades out again as it
     * recedes toward the vanishing point.
     */
    private float fadeAlpha( float depth )
    {
        final float full = FADE_START * mReadDepth;
        if( depth <= full )
        {
            return Math.min( 1f, Math.max( 0f, ( depth + ENTRY_FADE * mReadDepth )
                / ( full + ENTRY_FADE * mReadDepth ) ) );
        }
        return Math.max( 0f, 1f - ( depth - full ) / ( FADE_END * mReadDepth - full ) );
    }

    /**
     * Draws the vanishing-point dot, brightening as the last blocks recede into it.
     */
    private void drawDot( Canvas canvas, float centerX, float progress )
    {
        final int alpha = (int) ( ( 0.35f + 0.65f * progress ) * 255f );
        mGlowPaint.setAlpha( alpha );
        mCorePaint.setAlpha( alpha );
        canvas.drawCircle( centerX, mHorizonY, dp( DOT_GLOW_DP ), mGlowPaint );
        canvas.drawCircle( centerX, mHorizonY, dp( DOT_CORE_DP ), mCorePaint );
    }

    private TextPaint textPaint( int color, float sizeSp, boolean bold, float letterSpacing )
    {
        final TextPaint paint = new TextPaint( Paint.ANTI_ALIAS_FLAG );
        paint.setColor( color );
        paint.setTextSize( sp( sizeSp ) );
        if( bold )
        {
            paint.setTypeface( Typeface.DEFAULT_BOLD );
        }
        paint.setLetterSpacing( letterSpacing );
        return paint;
    }

    private float dp( float value )
    {
        return TypedValue.applyDimension( TypedValue.COMPLEX_UNIT_DIP, value,
            getResources().getDisplayMetrics() );
    }

    private float sp( float value )
    {
        return TypedValue.applyDimension( TypedValue.COMPLEX_UNIT_SP, value,
            getResources().getDisplayMetrics() );
    }
}
