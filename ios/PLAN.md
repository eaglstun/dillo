# Dillo iOS — Plan

**Branch:** `ios-render-spike`
**Status:** the spike answered its question and kept going — this is now a
**working browser**: address bar, HTTPS, real HTML/CSS parsing, tappable
links, history, scrolling, and images, all in the iOS Simulator against
live websites. See `RESULT.md` for the verified evidence per milestone,
with screenshots.

The original spike plan (prove that `dw/` can wrap text through Core
Text/Core Graphics with zero FLTK) is in this file's git history. Its core
bet paid off completely: **the engine and parser have needed zero source
modifications through four milestones.** Everything iOS-specific lives in
`ios/`.

---

## What exists (milestones 1–5, done)

| #   | Milestone                                                                       | Proof                                             |
| --- | ------------------------------------------------------------------------------- | ------------------------------------------------- |
| 1   | Engine renders wrapped styled text via Core Text, no FLTK                       | `spike-result.png`                                |
| 2   | Real parser stack (`html.cc` → `styleengine.cc` → `css*.cc`) renders a document | `spike-html-result.png`                           |
| 3   | Browses the live web: URL bar, HTTPS, stylesheets, link taps, back, scrolling   | `browser-home.png`, `browser-user-manual.png`     |
| 4   | Images (async fetch + ImageIO decode + engine scaling); forms don't crash       | `browser-wikipedia.png`                           |
| 5   | Interactive forms: native UIKit controls, GET search + POST submission          | `browser-ddg-search.png`, `browser-form-post.png` |

### Architecture (the desktop → iOS seam)

- **Fetching:** `URLSession` (`Dillo/PageLoader.swift`) replaces
  `src/IO/` + `cache.c`/`capi.c`. Stylesheets are prefetched (link scan +
  recursive `@import`, hrefs entity-decoded and resolved with dillo's own
  `url.c`) and seeded into a mini-cache (`engine/IosCapi.cc`) so the
  parser's synchronous cached-CSS path serves them. Images go the other
  way: `a_Capi_open_url` dispatches image webs to the shell, which fetches
  async and replays the dicache feed sequence on arrival.
- **Rendering:** `engine/AppleCoreTextPlatform.cc` (metrics/fonts),
  `engine/CoreGraphicsView.cc` (paint), `engine/AppleImgbuf.cc` (image
  buffers) implement the `dw::core` interfaces. One `Layout` per browser
  window, one `DilloHtml` per page, exactly like the desktop.
- **Shell:** `Dillo/DilloBridge.mm` owns the engine objects and the page
  lifecycle; `Dillo/BrowserViewController.swift` owns chrome, scroll
  physics (visible-rect expose — the render view never grows), taps
  (forwarded into `Layout::buttonPress/Release`), and app-side history.
  Parser→shell callbacks (open URL, title, fetch image) go through the
  hook registry in `engine/IosShell.hh`.
- **Not compiled:** FLTK anything, `src/IO/`, dpid/dpi, `uicmd.cc`,
  `menu.cc`, `dialog.cc`, `nav.c`, `cache.c`, `capi.c`, `dicache.c`, the
  image decoders (`jpeg.c`, `png.c`, …— ImageIO does this), `utf8.cc`
  (FLTK-backed; replaced by `engine/utf8_ios.cc`).

### Build

```sh
cd ios
xcodegen generate     # project.yml is the source of truth
xcodebuild -project DilloSpike.xcodeproj -scheme Dillo \
  -destination 'platform=iOS Simulator,name=iPhone 16 Pro,OS=18.4' build
```

---

## Next milestones (each independently shippable)

Roughly ordered by user-visible value per unit of work.

### ~~M5 — Interactive forms~~ (done)

Shipped as planned, with two design deltas from the sketch above worth
noting: controls live as subviews of the browser's UIScrollView (whose
content coordinates ARE canvas coordinates), so no scroll-offset math or
reflow hooks were needed — `sizeAllocate` alone keeps them aligned; and
the factory swap happens via `ApplePlatform::setResourceFactory`
(`Dillo/NativeFormResources.mm` is the UIKit factory, `StubResources`
remains the fallback). GET and POST verified live; see `RESULT.md`.
Remaining form gaps live in M6.5 below.

### M6 — Image completeness

- **srcset/lazy images**: resolve `srcset`/`data-src` in the shell (the
  parser only reads `src`); either rewrite attrs pre-parse or teach the
  fetch hook to try alternates. Fixes the blank lead images on Wikipedia.
- **Cross-page image cache** keyed by URL (mini-cache already exists —
  just stop clearing decoded images on navigation, add an LRU cap).
- **SVG**: smallest path is rasterizing via `UIImage`/CoreSVG-adjacent
  APIs where available, else ship without it (desktop uses nanosvg —
  could also be compiled, it's a header-only C lib in `src/svg.c`'s
  orbit).
- CGImage caching inside `AppleImgbuf::draw` (currently rebuilt per
  paint; cache and invalidate on `copyRow`).

### ~~M6.5 — Form completeness~~ (done)

All four shipped and verified live (see `RESULT.md`): button titles via
DeepIterator text extraction, async two-phase file upload through
UIDocumentPicker (nested-runloop modals are impossible on iOS — the
pick re-fires the button click), inline checkable multi-select lists,
and placeholder-based autofill hints. `data:` URLs load for form
testing.

### M7 — Zoom + wide pages (pinch implemented; fit-width open)

Pinch gesture now updates `bw->zoom` in 10% steps and reparses the retained
HTML source (the StyleEngine resolves zoom while building styles). The shell
approximately preserves the canvas point under the gesture while reflowing.
Double-tap to toggle fit-width for pages wider than the viewport (which
currently wrap at min-width and pan horizontally).

### ~~M8 — Navigation polish~~ (mostly done)

Shipped: anchor scrolling (usesViewport flipped true — the engine's
scroll machinery now runs, with UIScrollView doing the moving),
find-in-page (engine search + highlight + scroll-into-view, thin find
bar), selection + copy (long-press → engine selection → UIPasteboard),
left-edge swipe-back, styled error pages. See `RESULT.md`.
Still open: per-page load progress, history persistence.

### M9 — Platform correctness

- **Cookies**: `URLSession` already keeps a session cookie jar for
  fetches (so login-ish flows partially work by accident). Decide:
  keep NSHTTPCookieStorage as the jar (persistent by default) or compile
  dillo's `cookies.c` + dpi model (heavy; probably never).
- Charset edge cases (the header→meta-sniff→UTF-8→Latin-1 ladder is
  good; BOM handling and HTML5 prescan order could be tightened).
- Dirty-rect repaints: `queueDraw` currently invalidates the whole
  visible rect; accumulate engine dirty rects → `setNeedsDisplay(_:)`.
- Memory: page teardown is verified, but no profiling has been done on
  image-heavy pages; add a simulator Instruments pass.

### M10 — Device + distribution reality checks

- Run on a physical iPhone (only simulator-verified so far). Expect
  nothing platform-specific — same arm64 — but touch latency and font
  rendering deserve eyes.
- **Licensing**: Dillo is GPLv3; App Store distribution of GPLv3 apps is
  historically fraught (VLC precedent). TestFlight/personal sideloading
  is fine; store distribution needs an actual decision (upstream
  relicensing is not a thing that happens). Flagging early, before
  anyone dreams of shipping.

---

## Lessons that must survive (the hard-won invariants)

1. **The parser mutates and retains its input buffer** (`Start_Buf`).
   Writable heap memory, freed only after the _next_ page swap. The
   bridge handles this; never hand it anything else.
2. **Cache keys must match the parser's entity-decoded, url.c-resolved
   form.** Any shell-side URL that will later be compared against a
   parser-computed URL must go through `resolveURL` + entity decoding.
3. **No stub is unreachable on the real web.** Wikipedia found the NULL
   resource factory in one page load. Every interface the engine can
   call must return something structurally valid.
4. **Inner Layouts need views.** `Layout::queueResize` dereferences its
   view unconditionally; a contextless `CoreGraphicsView` is the
   canonical "flat view".
5. **The engine is single-threaded.** All engine calls happen on the
   main thread; async completions hop to main before touching anything.
   Keep it that way until there's a reason not to (there isn't).
6. **Don't edit engine/parser sources.** Four milestones, zero engine
   diffs. If a change seems to require touching `dw/` or `src/`, the
   design is wrong or it's an upstream patch to propose on its own
   merits.

## Working agreements

- `ios/project.yml` is the build's source of truth; the `.xcodeproj` is
  generated and gitignored.
- Every milestone ends with: verified against live sites in the
  simulator, screenshot committed, `RESULT.md` updated, small reviewable
  commits.
