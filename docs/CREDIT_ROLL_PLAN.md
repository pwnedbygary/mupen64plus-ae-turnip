# Credit roll

## 1. Goal

Replace the "Credits" entry under Gallery → About, which opens the paulscode forum in a browser,
with an in-app credit roll for the Turnip fork. The roll is an action-bar-free, Star Wars style
perspective crawl that fills the app window (the system status and navigation bars stay visible):
the blocks rise from the bottom edge at full size, are read there, then recede toward a single
vanishing point and fade into it.

## 2. User contract

- Gallery → drawer → About → Credits opens the roll.
- Any touch, key, gamepad button, or Back press leaves the roll immediately.
- A tap on a contributor's GitHub handle opens that profile and leaves the roll.
- The roll holds on the empty vanishing point for a moment and then returns to the gallery on its
  own, so the screen is never a dead end.
- The roll resumes where it was after a configuration change (rotation).

## 3. Design

### 3.1 Projection

`CreditRollView` is a custom `Canvas` view. The track is laid out once in flat pixels, one block
per contributor, and the whole roll then moves away from the viewer. A block at depth `d` is drawn
with a 1/z divide:

```
scale = readDepth / (readDepth + d)
y     = horizonY + (height - horizonY + offsetInBlock) * scale
```

- `horizonY` is 25% down from the top; the dot sits there.
- `readDepth` is 0.6x the space between the vanishing point and the bottom edge.
- A block is a **billboard**: its avatar and its lines of text share the block's depth and scale and
  keep their natural spacing inside it (`Element.offsetY`), so a card stays readable while it
  travels. Lines are drawn individually because a view tree can only be transformed as a
  rectangle.
- Block base depths **decrease** along the track (`-i * slot`). A block at negative depth is below
  the bottom edge and is skipped until the crawl brings it past zero, so the blocks rise into view
  in order.
- `slot = FADE_END * readDepth` is the depth from the viewer plane at which a block's fade has
  ended, and it is also the depth gap between neighbouring blocks. That hands the screen over
  exactly at the vanishing point: the next block starts rising past the bottom edge when the
  outgoing one has already faded to 20% opacity at 43% of its size, so the two are briefly visible
  at the same time, but never as two readable cards. The fade from full opacity to zero spans
  `FADE_END - FADE_START = 1.25` read depths; a block first becomes visible at `-ENTRY_FADE`.
- Glyphs are not skewed. `Canvas` cannot warp text, so text and avatars scale and converge rather
  than turn into trapezoids. Per-glyph warp would need an OpenGL renderer.

### 3.2 Timing

- `ROLL_SECONDS = 30`, `CRAWL_FRACTION = 0.9`: the crawl runs for 27s, then the empty dot holds
  for 3s before the activity returns to the gallery.
- `FADE_START = 0.35`, `FADE_END = 1.6`, `ENTRY_FADE = 0.25` (all in read depths). A block fades in
  as it rises past the bottom edge, is fully opaque while close enough to read, and fades out as it
  recedes.
- The activity ticks the view every 16ms using real elapsed time, so a slow frame does not slow the
  roll. Progress is saved and restored as a 0..1 fraction.

## 4. Files

| Path | Role |
| --- | --- |
| `app/src/main/AndroidManifest.xml` | Declares `CreditRollActivity` (not exported, singleTop). |
| `.../GalleryActivity.java` | `menuItem_credits` starts the activity instead of opening the forum. |
| `.../CreditRollActivity.java` | Content (six contributors), input handling, saved state, auto-return. |
| `.../CreditRollView.java` | The perspective renderer described above. |
| `app/src/main/res/layout/credit_roll_activity.xml` | Hosts the custom view. |
| `app/src/main/res/values/strings.xml` | `creditRoll_title`, `creditRoll_thankYou`, `creditRoll_viewOnGitHub`, and the six `creditRoll_blurb_*` contributor descriptions. |
| `app/src/main/res/values/strings-no-translate.xml` | The old `uri_credits` forum link is removed; the roll replaces it. |
| `app/src/main/res/drawable/credit_avatar_*.png` | Six bundled avatars (option A below). |
| `docs/CREDIT_ROLL_PLAN.md` | This document. |

`res/layout/item_credit_contributor.xml` was written for an earlier scroll-based version and
removed; nothing references it.

## 5. Avatars

Bundled PNGs (option A, chosen) in `res/drawable`, one per contributor. The handles were checked
against the upstream project pages: `pwnedbygary`, `fzurita`, `littleguy77`, `ecsv`, `paulscode`,
`casualjames`.

## 6. Verification

Toolchain per `README.md`: Android SDK 34, Build Tools 34.0.0, NDK 26.1.10909125, JDK 17, Gradle
wrapper 8.4.

- Build: `./gradlew :app:assembleDebug` (green).
- Device: Retroid Pocket 6, serial `49016109`, debug package
  `org.mupen64plusae.turnip.pwnedbygary.debug` installed with `adb install -r` alongside the
  release build.
  - The roll renders and animates; frames were sampled across the full 30s.
  - The roll returns to the gallery on its own after the dot hold.
  - Touch, D-pad key, and Back each leave the roll immediately.
  - A tap on a GitHub handle opens that profile: the probe located the handle in a frame, corrected
    for the roll's motion, tapped it, and Brave Browser came forward with
    `dat=https://github.com/pwnedbygary` (the first card). The roll closed at the same time.
  - No `FATAL EXCEPTION` / `AndroidRuntime` crash lines for the debug package during any run.
- Still not verified on device: configuration-change restore, TalkBack/rotary announcement, and
  readability at a large font scale.

## 6a. Review round 1

Two independent read-only reviews ran against the frozen snapshot; both returned NEEDS CHANGES.
Resolved in the current tree:

- The startup callback was an unretained lambda that could run after `exit()` or after destruction
  on a configuration change. It is now a retained `mStartRunnable` guarded by the exiting flag, and
  `stopRoll()` cancels every pending callback from both `exit()` and `onDestroy()`.
- The documented dot hold was 3s but the code added a further 2s; the extra delay is gone, so the
  crawl (27s) plus the dot hold (3s) is the whole 30s.
- "Full-screen" was inaccurate: the app window is filled but the system bars stay visible. The
  wording now says so in the code and here.
- Handle taps could never work: `onDraw` cleared the handle list every frame and never rebuilt it.
  The bounds of each handle are now reset per frame and refilled as handles are drawn, so only
  visible handles are tappable (verified on device, above).
- The blurb's translucent colour was overwritten by `Paint.setAlpha`, and the first attempt to
  combine the two compounded the alpha every frame until the text vanished. Each paint's base alpha
  is now captured once when its element is built.
- Contributor blurbs are included in the view's content description; the dead `closing` flag and
  the orphaned `uri_credits` string were removed; the fade-distance and direction-of-travel
  comments were corrected.

## 6b. Review round 2

A fresh standards/spec review of the round-1 snapshot returned NEEDS CHANGES. Resolved in the
current tree:

- The index was not a coherent snapshot: the new files were still intent-to-add placeholders, so
  committing from the index would have carried only this document. All fourteen paths are now
  staged explicitly, and the review artifact is the complete staged diff.
- The closing card's panel did not fade with its block, because the panel paint took its colour but
  never the roll alpha; it now folds in the colour's own alpha, like the text paints.
- `onGenericMotionEvent` ignored only `ACTION_HOVER_MOVE`, so a pointer entering the window sent an
  `ACTION_HOVER_ENTER` that closed the roll. All three hover actions are now ignored, matching the
  documented "hover is not deliberate input" rule.
- The fade-distance wording overstated the hand-off. A block is visible from `-ENTRY_FADE` through
  `FADE_END`, so the visible interval is 1.85 read depths, not 1.6; two neighbours are briefly
  visible together. The comments now state the actual hand-off: the next block starts rising when
  the outgoing one has faded to 20% opacity at 43% of its size. The `slot` formula is unchanged.
- The contributor blurbs were hard-coded English strings in Java, against the convention at the top
  of `strings.xml`. They are now six string resources (`creditRoll_blurb_*`); names and GitHub
  handles stay in code because they are proper nouns.

## 7. Review gate

- Base revision: `08e39b6ac`.
- Freeze the snapshot (status, full diff, untracked list, SHA-256 of every path).
- Two fresh read-only reviews: one against the documented conventions and this document, one
  re-running the build and re-checking the input and exit paths. The planned `gentle_review` helper
  is not available in this harness, so fresh subagent reviews stand in for it.
- Commit the reviewed snapshot as one work unit. Push and the pull request are left to the user;
  the branch has no upstream.

## 8. Risks

- The projection is a billboard approximation, not a plane projection, so a card's contents do not
  foreshorten internally. This is deliberate: it keeps the text readable.
- A long roll can feel slow on a large screen. The timing constants are at the top of
  `CreditRollView` and can be tuned without touching the layout code.
- The handle bounds are recorded during drawing, so a tap between frames uses the last drawn
  frame's bounds. The roll moves slowly, so this is not noticeable, and any tap outside a handle
  still exits.
- Accessibility is limited: the view has one content description naming the title, every
  contributor, handle, and blurb, and the closing card, but the individual handles are not exposed
  as individually focusable accessibility actions, so a screen reader announces the credits but
  cannot activate a profile from the roll. Opening the profile from the gallery's Help links or a
  browser remains available.
