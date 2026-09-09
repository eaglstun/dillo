# Dillo web browser — Apple platforms fork

Dillo is a multi-platform graphical web browser, known for its speed and
small footprint, that is developed with a focus on personal security and
privacy. It is built with the [FLTK 1.3 GUI toolkit](http://fltk.org).

**This fork adds two things upstream doesn't have:** a macOS build that
behaves like a Mac application instead of an X11 program in a costume, and
**an iOS port** — Dillo's real rendering engine and HTML/CSS parser running
on a phone with no FLTK linked in at all.

<img src="ios/browser-wikipedia.png" alt="Dillo rendering Wikipedia on iOS" width="300">

Everything is additive. The Linux/BSD build is untouched: every macOS
behaviour below is behind `#ifdef __APPLE__`, the iOS port lives entirely in
`ios/`, and the engine and parser have needed **zero source modifications**
to run there.

## What's in the fork

### iOS port (`ios/`)

A real browser, not a rendering demo — it browses the live web in the
Simulator and on device.

- **`dw/` + the parser stack on Core Text / Core Graphics.** No FLTK, no
  `src/IO/`, no dpi. `AppleCoreTextPlatform` does metrics and fonts,
  `CoreGraphicsView` paints, `AppleImgbuf` backs images.
- **Fetching via `URLSession`,** with a mini-cache that feeds prefetched
  stylesheets (link scan + recursive `@import`) into the parser's
  synchronous cached-CSS path. Images fetch async and replay the dicache
  feed sequence on arrival.
- **Interactive forms** — native UIKit controls driven by the engine's own
  layout, GET and POST submitted through dillo's `form.cc`, plus an async
  two-phase file picker and `data:` URLs.
- **Navigation polish**: find-in-page, long-press-drag selection with copy,
  anchor/fragment navigation, left-edge swipe back, native long-press menus
  on links and images, styled error pages.
- **Pinch zoom** at 50–300% — reflow zoom, not bitmap magnification: the
  bridge re-parses the retained source and rebuilds the widget tree so font
  sizes and CSS lengths are genuinely recomputed. The gesture previews
  continuously with a layer transform and reflows once, when the fingers
  lift — so text is bitmap-soft mid-pinch and crisp the moment you let go.
  That single reparse still clears transient form values.
- **Shortcuts / Siri** via an _Open URL in Dillo_ App Intent, and the
  `dillo:` URL scheme.

See **[ios/PLAN.md](ios/PLAN.md)** for the roadmap and architecture, and
**[ios/RESULT.md](ios/RESULT.md)** for what each milestone actually proved,
with screenshots.

### macOS native integration

- **System menu bar** (`Fl_Sys_Menu_Bar`) with File and Help menus; the
  in-window File and `?` buttons are hidden.
- **Native context menus** — page, link, image, and form right-clicks are
  presented as AppKit `NSMenu`s, translated from the existing
  `Fl_Menu_Item` hierarchy so the callbacks stay shared with every other
  platform.
- **Native window tabs** — each tab is a real FLTK window joined to an
  AppKit `NSWindowTabGroup`, instead of the in-window `CustTabs`/`Fl_Wizard`
  the other platforms use.
- **Native save and find dialogs** (`NSSavePanel`, a Cocoa search sheet).
- **Bundled DejaVu fonts,** registered process-locally with Core Text —
  macOS ships no DejaVu, which is what `prefs.c` asks for, so text used to
  silently fall back to Helvetica. Nothing is installed for other apps.
- **`Dillo.app` bundle** with an app icon and an **Apple Help Book**, whose
  search index is built with `hiutil`; the plain binary falls back to the
  manual in a tab.
- **Help menu** entries that deep-link into the user manual by anchor.

### Browser features

- **Persistent history.** Successfully loaded http/https pages are saved to
  `~/.dillo/history.txt` and browsable at `about:history` or via
  **File → History** (⌘Y). Local paths and internal pages are never
  recorded. Tunable with `save_history` and `history_max_entries` in
  `dillorc`; a `history` key action is available in `keysrc`.
- **CSS fixes**: `text-align: start` / `end`, `background-color:
transparent`, and `text-decoration: none` now parse and apply instead of
  being dropped. Covered by `test/html/render/css-easy-wins.html`.

### Scripting and automation

The upstream control socket grows nine verbs, turning `dilloc` from
"navigate and read" into "introspect and act": `links`, `forms`, `inputs`,
`resolve`, `set`, `submit`, `reset`, `follow`, and `noninteractive`. Both
app bundles also register the `dillo:` URL scheme for Apple Shortcuts.

There is **still no JavaScript engine** — only data present in the server's
HTML is reachable, and addressing is by index and name, never by selector or
visible text. Read **[AUTOMATION.md](AUTOMATION.md)** before scripting
anything; it documents the traps that actually bite. The staged plan behind
the form/link verbs is in **[AUTOMATION-PLAN.md](AUTOMATION-PLAN.md)**.

## Building

### macOS

```sh
brew install autoconf automake fltk@1.3 openssl@3 jpeg webp brotli

./autogen.sh
mkdir build && cd build
PATH="$(brew --prefix fltk@1.3)/bin:$PATH" \
  ../configure \
    LDFLAGS="-L$(brew --prefix openssl)/lib -L$(brew --prefix jpeg)/lib -L$(brew --prefix webp)/lib -L$(brew --prefix brotli)/lib" \
    CPPFLAGS="-I$(brew --prefix openssl)/include -I$(brew --prefix jpeg)/include -I$(brew --prefix webp)/include -I$(brew --prefix brotli)/include"
make -j$(sysctl -n hw.ncpu)
```

The binary lands at `build/src/dillo` (and `build/src/dilloc`). Note that
Homebrew's own `dillo` on `PATH` will shadow it — run yours by path.

### iOS

```sh
cd ios
xcodegen generate     # project.yml is the source of truth
xcodebuild -project DilloSpike.xcodeproj -scheme Dillo \
  -destination 'platform=iOS Simulator,name=iPhone 16 Pro,OS=18.4' build
```

### Other platforms

Unchanged from upstream — follow the
[installation guide](doc/install.md).

**[AGENTS.md](AGENTS.md)** carries the working notes for this fork: the
constraints behind each macOS integration, the project layout, and how to
verify UI changes on macOS without fooling yourself.

## Upstream

This fork tracks [dillo-browser/dillo](https://github.com/dillo-browser/dillo).
Screenshot of the [Dillo Website][dillo] rendered in Dillo:

[dillo]: https://dillo-browser.org/

![Screenshot of Dillo website rendered in Dillo](doc/dillo.png)

You can clone the upstream version of Dillo directly from their git server:

- <https://git.dillo-browser.org/dillo>

Mirrors, kept updated in case that git server fails:

- <https://codeberg.org/dillo/dillo>
- <https://git.sr.ht/~dillo/dillo>

The upstream commits are signed with the same OpenPGP key used for the
releases.

See also other related forks: [dillo-plus][dillo-plus],
[dilloNG][dilloNG], [D+ browser][dplus-browser] and [Mobilized
Dillo][mobilized].

[dillo-plus]: https://github.com/crossbowerbt/dillo-plus
[dilloNG]: https://github.com/w00fpack/dilloNG
[dplus-browser]: https://sourceforge.net/projects/dplus-browser/
[mobilized]: https://www.toomanyatoms.com/software/mobilized_dillo.html

> **Warning**:
> As of December 2023, the host `dillo.org` is [no longer under control][gone]
> of Dillo developers. A copy of the old website is archived in
> [our website][old] and the [Wayback Machine (May 2022)][may].

[gone]: https://dillo-browser.org/dillo.org.html
[old]: https://dillo-browser.org/old/
[may]: http://web.archive.org/web/20220508022123/https://www.dillo.org/
