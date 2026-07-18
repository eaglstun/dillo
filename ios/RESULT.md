# Dillo iOS Renderer Spike — Result

**Status: SUCCESS, twice over.** Milestone 1: the `dw/` engine renders
styled, word-wrapped text on iOS through Core Text / Core Graphics with zero
FLTK linked in. Milestone 2: the **real HTML parser and CSS engine**
(`src/html.cc` → `src/styleengine.cc` → `src/css*.cc`) run on iOS and render
a full HTML document with a stylesheet.

## Milestone 2 — real HTML/CSS parser

![html result](spike-html-result.png)

The bridge no longer hand-builds a Textblock; it heap-copies a hardcoded
HTML document and feeds it to `DilloHtml::write()`. Everything visible went
through the genuine parser stack and cascade (user-agent stylesheet +
`<style>` block + inline classes): styled `h1`, class selectors, b/i/u/s,
`<mark>` backgrounds, **small-caps**, link styling, character entities and
UTF-8, nested ul/ol with bullets and numbering, an indented blockquote with
background, `<hr>`, a bordered table with `th` styling and per-cell
alignment/color, and monospace `<pre>`.

What it took (all in `ios/`, no engine or parser source changed):

- **Compile set** (`project.yml`): `html.cc css.cc cssparser.cc
styleengine.cc table.cc form.cc image.cc bw.c misc.c colors.c url.c
prefs.c klist.c bitvec.c history.c` — all compiled unmodified.
- **`engine/utf8_ios.cc`** replaces `src/utf8.cc` (which wraps FLTK's
  `fl_utf8*`): a faithful port of `fl_utf8decode` including its CP1252
  fallback for illegal bytes, which the tag-soup parser depends on.
- **`engine/BrowserStubs.cc`** satisfies the 26 link-time symbols from the
  browser shell: `a_Capi_*` (report "not cached / can't fetch"),
  `a_UIcmd_*` (no-op), `a_Web_new/free` (data half of web.cc),
  `a_Dialog_choice`, `a_Hsts_require_https`.
- **`config.h`**: `#define inbuf_t char` (the configure-time iconv probe);
  the app links `libiconv.tbd` for form.cc.
- **Bridge init** mirrors `dillo.cc`'s order (`a_Prefs_init`, `a_Web_init`,
  `a_Bw_init`, `StyleEngine::init`) and `web.cc`'s RootUrl dispatch block
  (canvas bg color, initial "body" style, `layout->setWidget`). Font prefs
  are swapped to iOS families (Georgia / Helvetica Neue / Courier New).

**The one crash along the way is the most useful lesson for the next
milestone:** `Html_write_raw` tokenizes its input buffer **in place** and
`DilloHtml` retains the pointer (`Start_Buf`) after parsing. Feeding it a
string literal SIGBUSes; the buffer must be writable heap memory that
outlives the parser — the desktop cache layer guarantees exactly this, so
whatever replaces it on iOS must too.

---

# Milestone 1 — engine + Core Text (original spike)

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
