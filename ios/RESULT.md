# Dillo iOS — Results

**Status: it's a browser, with pictures.** Milestone 1: the `dw/` engine
renders styled, word-wrapped text on iOS through Core Text / Core Graphics
with zero FLTK linked in. Milestone 2: the **real HTML parser and CSS
engine** run on iOS against a hardcoded document. Milestone 3: **Dillo
browses the live web** — address bar, HTTPS, external stylesheets, tappable
links, history. Milestone 4: **images** — async loading, ImageIO decode,
the engine's own scaling — and enough form-widget robustness to survive the
real web.

## Milestone 4 — images (and surviving forms)

![wikipedia](browser-wikipedia.png)

Verified on live sites: the Dillo homepage renders its PNG screenshot
thumbnails; Wikipedia's Armadillo article renders **styled** (serif display
title, bordered taxobox, the geological time-range chips — all real CSS
through the cascade) with the nine-banded armadillo JPEG decoded and drawn
in place. SVG images degrade to alt text (ImageIO has no SVG decoder).

What was built:

- **`AppleImgbuf`** (`ios/engine/AppleImgbuf.{hh,cc}`): a port of
  `dw/fltkimgbuf.cc` — root + scaled buffers, gamma-aware averaging
  scaler, reference counting — with `draw()` wrapping the raw RGB rows in
  a CGImage (local y-flip, nearest-neighbor since rows are pre-scaled).
  `ApplePlatform::createImgbuf` and `CoreGraphicsView::drawImage` now do
  real work; `src/imgbuf.cc` joins the compile set.
- **Async image pipeline**: `a_Capi_open_url` recognizes image webs and
  hands them (with a client key for bw's progress bookkeeping) to the
  shell via a new `ImageFetchHook`. The bridge fetches with URLSession;
  on completion `dw::ios::deliverImageData` decodes via ImageIO,
  composites alpha over the image's `bg_color`, and drives the DilloImage
  state machine exactly as the desktop dicache does (`a_Imgbuf_new` →
  `set_parms` → per-row `a_Imgbuf_update` + `a_Image_write` → `close`).
  Pages render first; images pop in as they arrive — real browser
  behavior. Pending fetches are cancelled on navigation (web validity
  guarded the same way capi's ValidWebs list works).
- **`StubResources`** (`ios/engine/StubResources.{hh,cc}`): display-only
  form widgets. The milestone-2 assumption that the resource factory was
  "unreached" died on contact with Wikipedia's search `<input>` (NULL
  deref in `Embed`); every resource type now returns a real, fixed-size,
  draw-nothing object. The complex-button resource attaches a contextless
  `CoreGraphicsView` to its inner layout — `Layout::queueResize`
  dereferences the view, so "no view" is not an option.
- **Entity decoding in the CSS prefetcher**: Wikipedia's stylesheet URLs
  contain `&amp;`; the parser entity-decodes hrefs before resolving, so
  the prefetch cache keys have to match that form or every lookup misses
  (which is why Wikipedia first rendered unstyled).

Known image gaps: srcset/lazy-loaded images whose real URL never appears
in `src` don't load; no SVG; no progressive/partial rendering (each image
appears complete when its fetch finishes); no image cache across pages.

## Milestone 3 — a real browser

![home](browser-home.png) ![user manual](browser-user-manual.png)

Verified in the simulator against live sites: `dillo-browser.github.io`
loads over HTTPS with its real stylesheet applied (the yellow notice box,
link styling); tapping "user manual" hit-tests through the engine
(`Layout::buttonPress/Release` → `HtmlLinkReceiver::click` →
`a_UIcmd_open_url` → shell hook) and navigates to `user_help.html` with
title and history updated; the back button returns; typing `example.com`
in the address bar normalizes, loads, and renders. Images are off
(`prefs.load_images = FALSE`) pending an Imgbuf implementation, so `[IMG]`
alt placeholders appear — authentically Dillo.

### Architecture (what replaced what)

| Desktop                                   | iOS                                                                                                                                                                                                                                                                                                        |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/IO/` sockets + TLS + `select()` loop | `URLSession` (`PageLoader.swift`)                                                                                                                                                                                                                                                                          |
| `cache.c`/`capi.c` streaming cache        | prefetch + mini-cache (`IosCapi.cc`): the shell fetches the page **and its stylesheets** (link scan + recursive `@import`, resolved via dillo's own `url.c` so cache keys match the parser) _before_ parsing, then the parser's cached-CSS fast path in `a_Html_load_stylesheet` serves them synchronously |
| `uicmd.cc` (FLTK UI commands)             | hook registry (`IosShell.hh`): `a_UIcmd_open_url` / `set_page_title` dispatch to Swift blocks                                                                                                                                                                                                              |
| `nav.c` history (cache-coupled)           | app-side URL list in `BrowserViewController`                                                                                                                                                                                                                                                               |
| FLTK viewport scrolling                   | `UIScrollView` physics + visible-rect expose: the render view stays screen-sized and `drawIntoContext:visibleRect:` translates the CTM by the scroll offset, so tall pages cost no backing store                                                                                                           |
| FLTK mouse events                         | `UITapGestureRecognizer` → `Layout::buttonPress/buttonRelease` with `BUTTON1_MASK` — the engine's real link hit-testing and click signal                                                                                                                                                                   |

Charset handling: HTTP header → `<meta>` sniff → UTF-8 → Latin-1, decoded
in Swift; the parser is always fed UTF-8 (content type says so).

One layout per app lifetime, one `DilloHtml` per page —
`Layout::setWidget` frees the old page (and its retained parse buffer is
freed only after the swap, per the milestone-2 lesson).

### Known gaps (next milestones)

- **Images**: needs an `Imgbuf` implementation (decode via CGImage) plus
  image fetches in the prefetch pass or an async repaint path.
- Pages wider than the viewport wrap at their min-width and pan
  horizontally (correct engine behavior; a zoom gesture would help).
- No cookies, form submission, find-in-page, tabs, or fragment scrolling.
- Stylesheet fetches the regex scan misses (e.g. JS-injected) are simply
  absent — same as desktop Dillo with CSS off for those sheets.

---

# Milestone 2 — real HTML/CSS parser (hardcoded page)

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
xcodebuild -project DilloSpike.xcodeproj -scheme Dillo \
  -destination 'platform=iOS Simulator,name=iPhone 16 Pro,OS=18.4' build
```

Or open the generated project in Xcode and run the `Dillo` scheme.
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
