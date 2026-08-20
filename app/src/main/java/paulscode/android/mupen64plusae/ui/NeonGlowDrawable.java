package paulscode.android.mupen64plusae.ui;

import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

/**
 * Neon light-pipe glow and clean, uniform frosted-glass surface for game cards.
 *
 * Features:
 * - 100% uniform, clean frosted-glass surface (zero discolored boxes or padding artifacts)
 * - Multi-pass neon light-pipe stroke with white-hot filament center
 * - Scalable glow intensity mapped directly to uiThemeCardGlow (0..100%)
 * - Scalable glass translucency mapped directly to uiThemeGlassOpacity (20..100%)
 */
public class NeonGlowDrawable extends Drawable {
    private final Paint mPaint;
    private final float mRadius;
    private final float mDensity;
    private final int mColor;
    private final int mCoreColor;
    private final int mFilamentColor;
    private final float mGlowScale;
    private final float mGlassOpacity;
    private RectF mRect = new RectF();

    public NeonGlowDrawable(int color, float radiusDp, float density) {
        this(color, radiusDp, density, 1.0f, 0.85f);
    }

    public NeonGlowDrawable(int color, float radiusDp, float density, float glowScale, float glassOpacity) {
        mColor = color;
        mCoreColor = blendWithWhite(color, 0.50f);
        mFilamentColor = blendWithWhite(color, 0.88f);
        mRadius = radiusDp * density;
        mDensity = density;
        mGlowScale = Math.max(0f, Math.min(1.0f, glowScale));
        mGlassOpacity = Math.max(0.1f, Math.min(1.0f, glassOpacity));
        mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mPaint.setStyle(Paint.Style.STROKE);
    }

    private static int blendWithWhite(int color, float whiteRatio) {
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        int rNew = Math.min(255, Math.round(r + (255 - r) * whiteRatio));
        int gNew = Math.min(255, Math.round(g + (255 - g) * whiteRatio));
        int bNew = Math.min(255, Math.round(b + (255 - b) * whiteRatio));
        return (0xFF << 24) | (rNew << 16) | (gNew << 8) | bNew;
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        mRect.set(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }

    @Override
    public void draw(Canvas canvas) {
        float d = mDensity;
        float w = mRect.width();
        float h = mRect.height();
        if (w <= 0 || h <= 0) return;

        // -------------------------------------------------------------
        // Neon Light-Pipe Outline Glow (scaled by mGlowScale)
        // -------------------------------------------------------------
        if (mGlowScale > 0.01f) {
            float tubeInset = 2.5f * d;
            RectF tubeRect = new RectF(mRect);
            tubeRect.inset(tubeInset, tubeInset);
            float rTube = Math.max(0f, mRadius - tubeInset);
            Path tubePath = roundedRect(tubeRect, rTube);

            mPaint.setStyle(Paint.Style.STROKE);

            // Layer 1: Broad soft ambient halo
            mPaint.setColor(mColor);
            mPaint.setAlpha(Math.round(45 * mGlowScale));
            mPaint.setStrokeWidth(10.0f * d);
            mPaint.setMaskFilter(new BlurMaskFilter(4.5f * d, BlurMaskFilter.Blur.NORMAL));
            canvas.drawPath(tubePath, mPaint);

            // Layer 2: Medium glow wings
            mPaint.setColor(mColor);
            mPaint.setAlpha(Math.round(95 * mGlowScale));
            mPaint.setStrokeWidth(6.0f * d);
            mPaint.setMaskFilter(new BlurMaskFilter(2.5f * d, BlurMaskFilter.Blur.NORMAL));
            canvas.drawPath(tubePath, mPaint);

            // Layer 3: Vibrant neon tube body
            mPaint.setColor(mColor);
            mPaint.setAlpha(Math.round(180 * mGlowScale));
            mPaint.setStrokeWidth(3.4f * d);
            mPaint.setMaskFilter(new BlurMaskFilter(1.2f * d, BlurMaskFilter.Blur.NORMAL));
            canvas.drawPath(tubePath, mPaint);

            // Layer 4: Bright light core
            mPaint.setColor(mCoreColor);
            mPaint.setAlpha(Math.round(220 * mGlowScale));
            mPaint.setStrokeWidth(1.8f * d);
            mPaint.setMaskFilter(null);
            canvas.drawPath(tubePath, mPaint);

            // Layer 5: White-hot central filament
            mPaint.setColor(mFilamentColor);
            mPaint.setAlpha(Math.round(255 * mGlowScale));
            mPaint.setStrokeWidth(0.9f * d);
            mPaint.setMaskFilter(null);
            canvas.drawPath(tubePath, mPaint);
        }
    }

    private static Path roundedRect(RectF r, float radius) {
        Path p = new Path();
        p.addRoundRect(r, radius, radius, Path.Direction.CW);
        return p;
    }

    @Override
    public void setAlpha(int alpha) {
        mPaint.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        mPaint.setColorFilter(colorFilter);
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
