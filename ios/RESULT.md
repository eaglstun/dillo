# Dillo iOS Renderer Spike — Result

**Status: SUCCESS.** The `dw/` engine renders styled, word-wrapped text on iOS
through Core Text / Core Graphics with zero FLTK linked in.

![spike result](spike-result.png)

Screenshot: iPhone 16 Pro simulator (iOS 18.4). The dw_example test page — ten
numbered paragraphs, Georgia 14, 5 px margins — laid out by `dw::Textblock`
into a 200 pt column. Every paragraph wraps across three lines ("… paragraph.
Here / comes some more text to / demonstrate word wrapping."), breaks are
identical across all ten paragraphs, no overlap, no clipped descenders.
Consistent breaks at the same words are only possible when
`Platform::textWidth` (measurement) and `View::drawText` (paint) agree — which
was the question the spike existed to answer.

## Building / running

```sh
cd ios
xcodegen generate          # produces DilloSpike.xcodeproj from project.yml
xcodebuild -project DilloSpike.xcodeproj -scheme DilloSpike \
  -destination 'platform=iOS Simulator,name=iPhone 16 Pro,OS=18.4' build
```

Or open the generated project in Xcode and run the `DilloSpike` scheme.
The `.xcodeproj` is generated — `project.yml` is the source of truth.

## What was built

| Piece             | File(s)                                    | Notes                                                                                                                                       |
| ----------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Engine static lib | `DilloEngine` target                       | all of `dw/` minus `fltk*`, `lout/`, `dlib/` — **compiled unmodified**                                                                      |
| iOS `config.h`    | `ios/engine/config.h`                      | replaces the autoconf one; engine subset only reads PACKAGE/VERSION/HAVE_STDINT_H                                                           |
| Platform          | `ios/engine/AppleCoreTextPlatform.{hh,cc}` | `ApplePlatform`, `AppleFont` (CTFontRef + metrics), `AppleColor`; mirrors `fltkplatform.cc` incl. small-caps and letter-spacing measurement |
| View              | `ios/engine/CoreGraphicsView.{hh,cc}`      | all paint primitives into a `CGContextRef`; clipping via GState save/clip/restore; `usesViewport()` = false                                 |
| App shell         | `ios/DilloSpike/`                          | `SpikeBridge.mm` is `dw_example.cc`'s main body with the four FLTK nouns swapped; Swift `UIView.drawRect` hands its context to the bridge   |

## Findings (in plan risk-log order)

1. **Metric agreement worked on the first render.** Measurement and painting
   share one code path (`AppleFont::measureWidth` on the same `CTFontRef`),
   so they cannot drift. `CTLineGetTypographicBounds` truncated to int matches
   how the engine consumed FLTK's `fl_width`.
2. **UTF-8 stepping** was ported as plain continuation-byte skipping
   (`fl_utf8fwd`/`fl_utf8back` semantics); shared between platform and view.
3. **`-fno-exceptions -fno-rtti` across the ObjC++ boundary** was a non-issue:
   the whole app (including `SpikeBridge.mm`) builds with those flags.
4. **`lout` portability**: zero changes needed for arm64/iOS SDK.
5. **Hidden FLTK reach-ins: none.** Verified two ways: grep (only doc comments
   in `imgbuf.hh`/`ui.hh` mention FLTK) and `nm` on the compiled archive
   (zero `Fl_`/`fl_` symbols).

## Things the next milestone should know

- **No event loop:** FLTK's idle queue is replaced by
  `ApplePlatform::runIdles()`, which the bridge drains synchronously after
  building the widget tree and again before each paint. `Layout::resizeIdle`
  runs there. A real browser shell will want these on the main run loop
  (`dispatch_async`) instead.
- **Wrap width** is fed via the public `Layout::viewportSizeChanged()`, which
  sets `viewportWidth` (what `Widget::getAvailWidth` reads) even though the
  view returns `usesViewport() == false`. Scrolling in the spike is a plain
  `UIScrollView` over the fully-drawn canvas.
- **dpi is pinned at 96** (`dpiX`/`dpiY`). Nothing in the spike path consults
  it (font size in `FontAttrs` is already px), but the CSS engine will —
  revisit when wiring `src/styleengine.cc`.
- **Stubbed:** images (`createImgbuf`/`drawImage` return NULL/no-op),
  tooltips, clipboard, form-widget `ResourceFactory` (returns NULL resources).
  The hyphenator compiles but finds no pattern files (`DILLO_LIBDIR` points at
  `/var/empty/`) and degrades gracefully.
- **Desktop comparison:** the desktop `dw_example` builds and runs from the
  same tree (see `test/dw/`). A side-by-side pixel capture wasn't automated
  (macOS screen-recording permission), but the iOS line breaks fall exactly
  where the column width dictates and are uniform across paragraphs, which is
  the behavior the desktop example exhibits with its (different) font.

## Verdict

~80% of the port uncertainty (font metrics + the paint seam) is retired.
Next milestone per PLAN.md: wire the real HTML/CSS parser (`src/html.cc`,
`src/css*.cc`) — engine code that talks only to the interfaces this spike
just proved out.
