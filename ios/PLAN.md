# Dillo iOS Renderer Spike — Implementation Plan

**Branch:** `ios-render-spike`
**Goal of this spike:** Prove that Dillo's rendering engine (`dw/`) runs on iOS with a
native (non-FLTK) drawing backend. Success = a hardcoded chunk of styled text renders
in an iOS app with **correct word wrapping**, drawn through Core Graphics / Core Text,
with zero FLTK linked in.

If wrapping is correct, ~80% of the interesting uncertainty (font metrics + the paint
seam) is de-risked. Everything after that is plumbing, not research.

> This is a **spike**, not the product. We are explicitly cutting scope to the single
> question above. Images, forms, scrolling, networking, and browser chrome are all
> OUT until the spike renders text.

---

## Why this is tractable (the architecture)

Dillo already separates the engine from the toolkit behind two abstract interfaces:

- **`dw/view.hh`** (`dw::core::View`) — the drawing surface. 27 pure-virtual methods;
  the paint primitives are `drawText`, `drawSimpleWrappedText`, `drawLine`,
  `drawTypedLine`, `drawRectangle`, `drawArc`, `drawPolygon`, `drawImage`, plus
  clipping (`getClippingView`/`mergeClippingView`) and queue/scroll bookkeeping.
- **`dw/platform.hh`** (`dw::core::Platform`) — font metrics, text measurement, color
  factory, idle/event hooks, resource (form-widget) factory, clipboard, tooltips.

The `fltk*.cc` files are just **one** implementation of those. The HTML parser
(`src/html.cc`), CSS engine (`src/css*.cc`), and layout (`dw/textblock.cc`, box/float/
table model) never reference FLTK — they only talk to `View` and `Platform`.

**The reference harness is `test/dw/dw_example.cc` (106 lines).** It builds a `Layout`,
attaches a viewport `View`, creates a `Textblock`, and adds words. Only **four**
objects in it are FLTK-specific:

| dw_example.cc uses                     | iOS spike replaces with                         |
| -------------------------------------- | ----------------------------------------------- |
| `dw::fltk::FltkPlatform`               | `dw::apple::ApplemPlatform` (Core Text metrics) |
| `dw::fltk::FltkViewport` (a `View`)    | `dw::apple::CoreGraphicsView`                   |
| `Fl_Window` + `Fl::run()`              | `UIViewController` + a `UIView`/`CALayer`       |
| `fontAttrs.name = "Bitstream Charter"` | a font that exists on iOS (e.g. `"Georgia"`)    |

`Layout`, `Textblock`, `Style`, `Font`, `Color` are all engine — they stay identical.

---

## Phase 0 — Build the engine as a static library for iOS (no FLTK)

**Outcome:** `libdw-engine.a` for the iOS simulator (arm64), containing `dw/` (minus
`fltk*`), `lout/`, and `dlib/`, compiled with the iOS SDK. No FLTK, no `src/`.

1. Determine the minimal engine translation-unit set. Start from `dw/Makefile.am` and
   **exclude every `fltk*.cc`**. Include all other `dw/*.cc`, plus `lout/*.cc` and the
   `dlib/*.c` that `dw`/`lout` reference. Resolve the compile set empirically: compile,
   read the missing-symbol / missing-header errors, add the referenced TU, repeat.
2. Create `ios/` build glue. Two acceptable routes — **pick the Xcode-project route for
   the spike** (fastest to a running app):
   - **(chosen) Xcode project** `ios/DilloSpike.xcodeproj` with the engine `.cc/.c`
     files added to a static-library target compiled `-std=c++11 -fno-rtti
-fno-exceptions` (C files `-std=c99`), header search paths for the repo root.
   - (later) SwiftPM / CMake toolchain file — defer; not needed to answer the question.
3. Expect to stub or `#ifdef` out the few desktop-only bits `dw` pulls in
   (e.g. anything reaching into `fltkmisc`). Grep for `#include "dw/fltk` inside
   non-fltk `dw` files first — there should be none; if there are, that's the real
   coupling to document.

**Checkpoint:** engine compiles to a static lib against the iOS SDK with no FLTK symbol
referenced. `nm` the archive; assert zero `Fl_`/`fl_` symbols.

---

## Phase 1 — `AplPlatform`: font metrics via Core Text

**File:** `ios/engine/AppleCoreTextPlatform.cc` + `.hh` (namespace `dw::apple`).

Implement `dw::core::Platform`. This is the metrics half of the spike — the part most
likely to reveal wrapping bugs. Model the structure on `dw/fltkplatform.cc`.

Must implement for real (text-critical):

- `textWidth(Font*, text, len)` — measure a UTF-8 run with Core Text
  (`CTLineGetTypographicBounds` or advances). Must match how `Textblock` expects widths.
- `nextGlyph` / `prevGlyph` — UTF-8 codepoint stepping (can port FLTK's `fl_utf8*`
  logic directly; it's plain UTF-8, no toolkit dependency).
- `textToUpper` / `textToLower` — UTF-8 case fold.
- `fontExists(name)` — `CTFontManager` / `UIFont(name:)` probe.
- `createColor(int)` — return a `dw::apple::AppleColor : dw::core::style::Color`
  subclass wrapping the 0xRRGGBB. (Mirror `FltkColor`.)
- `dpiX`/`dpiY` — return the screen scale × 72 (or 96; verify against what CSS px
  expects — this directly affects font sizing and thus wrapping).

Also need a **`Font` subclass** (`dw::apple::AppleFont : dw::core::style::Font`) created
via the platform's font path, storing a `CTFontRef` and the metrics `Textblock` reads:
`ascent`, `descent`, `spaceWidth`, `xHeight`, `zeroWidth`. Look at `FltkFont` in
`dw/fltkplatform.cc` for the exact fields to populate and how `Font::create` dispatches.

Safe to **stub** for the spike (return no-op / null / assert-unused):

- `getResourceFactory()` — return a factory whose methods are unreached (no forms in the
  test page). Only needs to not crash at construction.
- `createTooltip`/`cancelTooltip`, `copySelection`.
- `addIdle`/`removeIdle` — a spike page is static; return a dummy id, no-op remove.
  (Revisit if `Textblock::flush` schedules an idle it depends on — check `layout.cc`.)
- `setLayout`/`attachView`/`detachView` — store the pointers like FltkPlatform does.

---

## Phase 2 — `CoreGraphicsView`: the paint surface

**File:** `ios/engine/CoreGraphicsView.cc` + `.hh` (implements `dw::core::View`).

Draw into a `CGContextRef` (from `UIGraphicsImageRenderer` for a first static frame, or
the `CALayer`'s `drawInContext:`). Implement, in priority order:

Text-critical (do first):

- `drawText(Font*, Color*, ..., x, y, text, len)` — Core Text run at baseline `y`.
  Honor color and the font's `CTFontRef`. This + `textWidth` agreeing is the whole ball
  game for wrapping.
- `drawSimpleWrappedText` — see FLTK impl; may just loop `drawText`.
- `setBgColor` / clearing.

Box-critical (do second — borders/backgrounds so you can _see_ wrapping boxes):

- `drawRectangle` (filled + outline), `drawLine`, `drawTypedLine`, `drawPolygon`,
  `drawArc`. All trivial `CGPath` / `CGContextFill*`.

Clipping + bookkeeping:

- `startDrawing`/`finishDrawing`/`queueDraw`/`queueDrawTotal`/`cancelQueueDraw` —
  for a static single-paint spike these can be minimal (accumulate a dirty rect, or
  just repaint all). Model on `fltkviewbase.cc` but slash the async machinery.
- `getClippingView`/`mergeClippingView` — implement via `CGContextSaveGState` +
  `CGContextClipToRect`. Needed for correct nested-box paint.
- `setCanvasSize`/`setCursor`/`usesViewport`/scrollbar-thickness getters — return
  fixed values; **`usesViewport()` → false** for the spike (draw the whole canvas, no
  scrolling) to cut the viewport machinery entirely.

Safe to **stub**:

- `drawImage` / everything touching `Imgbuf` — the test page is text-only. Provide a
  no-op `drawImage` and a trivial `Imgbuf` factory only if construction demands it.
- `scrollTo`/`scroll`/`setViewportSize` — no-op (no scrolling in spike).

---

## Phase 3 — The iOS app shell + the spike harness

**Files:** `ios/DilloSpike/` — `AppDelegate.swift`, `SpikeViewController.swift`,
`DilloCanvasView` (UIView), and an Objective-C++ bridge `SpikeBridge.mm` + `.h`.

1. **`SpikeBridge.mm`** — port `dw_example.cc`'s `main()` body into a
   `-[SpikeBridge renderInto:(CGContextRef)ctx size:(CGSize)]` (or into a UIView's
   `drawRect:`). Swap the four FLTK nouns per the table above. Keep the exact
   Textblock-building loop (the "This is the N. paragraph…" words) so output is
   directly comparable to the desktop example.
2. **`DilloCanvasView`** — a `UIView` whose `drawRect:` calls the bridge with
   `UIGraphicsGetCurrentContext()`. Fixed narrow width (e.g. 200pt like the example) so
   wrapping is forced and obvious.
3. **`SpikeViewController`** — host the canvas view. That's it. No chrome.

**SUCCESS CRITERION:** Run in the iOS Simulator. The ten numbered paragraphs render,
each wrapping across multiple lines within the 200pt column, text legible, no overlap,
no clipping of descenders. Compare side-by-side with `test/dw/dw_example` on desktop —
line breaks should fall at plausibly similar places (not necessarily identical; fonts
differ).

Capture a simulator screenshot to `ios/spike-result.png` and commit it.

---

## Explicitly OUT of scope for this spike (do not start these)

- Networking / IO / TLS (`src/IO/`), URL loading, real web pages.
- HTML parsing (`src/html.cc`) and CSS (`src/css*.cc`) — the spike drives `Textblock`
  directly, exactly like `dw_example.cc`. (Wiring the real parser is the _next_ milestone
  once paint is proven — it's engine code and should "just work," but it's not this spike.)
- Images / `Imgbuf` (Core Graphics image decode).
- Forms / the `ui::ResourceFactory` widgets.
- Scrolling / viewport / `FltkViewport` behavior.
- Browser chrome (address bar, tabs, back/forward — all of `src/ui.cc`, `src/uicmd.cc`).
- The DPI daemon / multiprocess model (`dpid/`, `dpi/`).
- App Store / code-signing beyond a local simulator/dev run.

---

## Risk log (what will actually bite)

1. **Metric mismatch → wrong wrapping.** `textWidth` (Platform) and `drawText` (View)
   MUST use the same font object and the same measurement basis, or text wraps wrong or
   overprints. This is the #1 thing the spike is testing. If lines wrap wrong, suspect
   dpi/scale in `dpiX`/`dpiY` and the `Font` metric fields first.
2. **UTF-8 glyph stepping.** Port FLTK's `fl_utf8*` codepoint logic verbatim rather than
   inventing it; subtle off-by-one here corrupts measurement.
3. **`-fno-exceptions -fno-rtti` across the C++/ObjC++ boundary.** The engine is built
   no-exceptions/no-rtti; keep the ObjC++ bridge from throwing across into it. Compile
   the bridge with matching flags where it includes `dw` headers.
4. **`lout` container/identity assumptions.** `lout/` uses its own RTTI-free type
   identity. Should be portable C++11, but watch for any `__GNUC__`/endian/`size_t`
   assumptions on arm64.
5. **Hidden FLTK reach-in.** If any non-`fltk*` `dw` file `#include`s an FLTK header,
   that's real coupling to surface and cut. (Expected: none. Verify in Phase 0.)

---

## Suggested commit sequence (small, reviewable)

1. `ios/`: Xcode project skeleton + engine static-lib target that compiles the FLTK-free
   `dw`/`lout`/`dlib` set. (Phase 0)
2. `AppleCoreTextPlatform` + `AppleFont` + `AppleColor`, with unreached bits stubbed.
   (Phase 1)
3. `CoreGraphicsView` text + box primitives; image/scroll stubbed. (Phase 2)
4. `SpikeBridge` (ported `dw_example`) + UIView + view controller; renders in simulator.
   (Phase 3)
5. `ios/spike-result.png` + a short `ios/RESULT.md` recording what rendered and any
   metric surprises.

**Definition of done for the whole spike:** commit 4 renders wrapped text in the
simulator, commit 5 documents it. Then we decide whether to wire the real HTML/CSS
parser (next milestone) — at which point this stops being a science experiment and
starts being a browser.
