# Dillo Web Browser

Dillo is a lightweight, multi-platform graphical web browser built with FLTK 1.3. Written in C (core) and C++ (rendering/UI).

## Building on macOS

### Prerequisites

```sh
brew install autoconf automake fltk@1.3 openssl@3 jpeg webp brotli
```

### Build from source

```sh
./autogen.sh
mkdir build && cd build
PATH="$(brew --prefix fltk@1.3)/bin:$PATH" \
  ../configure \
    LDFLAGS="-L$(brew --prefix openssl)/lib -L$(brew --prefix jpeg)/lib -L$(brew --prefix webp)/lib -L$(brew --prefix brotli)/lib" \
    CPPFLAGS="-I$(brew --prefix openssl)/include -I$(brew --prefix jpeg)/include -I$(brew --prefix webp)/include -I$(brew --prefix brotli)/include"
make -j$(sysctl -n hw.ncpu)
```

Binary is produced at `build/src/dillo`. Install system-wide with `sudo make install` (default prefix: `/usr/local`).

### Reconfigure after changes to configure.ac

Run `./autogen.sh` again to regenerate the configure script, then re-run configure and make.

## Project structure

- `src/` - Main browser source (C `.c` and C++ `.cc` files)
  - `src/dillo.cc` - Entry point
  - `src/html.cc` - HTML parser
  - `src/css.cc`, `src/cssparser.cc` - CSS engine
  - `src/ui.cc`, `src/uicmd.cc` - FLTK UI layer
  - `src/IO/` - Network I/O (HTTP, HTTPS, DNS, TLS)
- `dw/` - Dillo Widget library (C++ rendering engine, layout)
- `lout/` - Lightweight object utility toolkit (containers, identity, misc)
- `dlib/` - Dillo library (string, list, URL utilities in C)
- `dpi/` - Dillo Plugin Interface programs (cookies, bookmarks, file, ftp, downloads)
- `dpid/` - DPI daemon
- `dpip/` - DPI protocol library
- `doc/` - Documentation and install guide
- `devdoc/` - Developer documentation
- `test/` - Unit tests and rendering tests
- `icons/` - Icon resources

## Build system

Uses autoconf/automake. Key files:

- `configure.ac` - Main configure template
- `Makefile.am` - Top-level and per-directory Makefile templates
- `autogen.sh` - Regenerates configure from configure.ac

## Key configure options

- `--disable-tls` - Disable HTTPS support
- `--disable-openssl` / `--disable-mbedtls` - Control TLS library selection
- `--disable-jpeg`, `--disable-png`, `--disable-webp`, `--disable-gif`, `--disable-svg` - Disable image formats
- `--disable-threaded-dns` - Use single-threaded DNS (useful if crashes occur)
- `--disable-cookies` - Disable cookie support
- `--enable-control-socket` - Enable remote control via UNIX socket (experimental)

## Code style

- C code uses C99 (`-std=c99`)
- C++ code uses C++11, no RTTI, no exceptions (`-fno-rtti -fno-exceptions`)
- Compiler warnings enabled: `-Wall -W -Wno-unused-parameter -pedantic`

## Scripting / automation

See **[AUTOMATION.md](AUTOMATION.md)** before planning anything that drives Dillo
programmatically — it carries the full command set, worked examples, and the limits that
actually bite. **[AUTOMATION-PLAN.md](AUTOMATION-PLAN.md)** is the staged plan behind the
form/link verbs; Stages 1-4 and 6 are done, Stage 5 (async replies) is open. Short version:

- `dilloc` (built alongside `dillo` when `+CTL` is in `--version`) drives a running
  browser over `~/.dillo/ctl/<pid>`. Navigate and read: `open`, `wait`, `dump`, `hdump`,
  `load`, `rawload`, `title`, `url`, `reload`, `quit`. Introspect and act: `links`,
  `forms`, `inputs N`, `resolve`, `set N NAME VALUE`, `submit N [NAME]`, `reset N`,
  `follow N`, `noninteractive`.
- **Still no JavaScript engine** — only data present in the server's HTML is reachable,
  and addressing is by index and name, never by visible text or selector.
- Two traps worth knowing before you write a script: `wait` burns its full timeout and
  exits 1 on a heavy page that loaded fine (settle on `dump`'s byte count instead), and a
  second running Dillo makes bare `dilloc` refuse outright — spawn your own and pin
  `DILLO_PID`. Both are detailed in AUTOMATION.md.
- `ios/` is a separate iOS port (branch `ios-render-spike`): the dw engine +
  parser behind an ObjC bridge, UIKit form controls, `URLSession` fetching. It is
  a human-interaction surface — there is no control channel.

## iOS port layout (`ios/`)

- `ios/Dillo/` — app shell: `DilloBridge.{h,mm}` (public API), `PageLoader.swift`
  (fetch + charset + stylesheet prefetch), `NativeFormResources.mm` (UIKit form
  widgets), `BrowserViewController.swift` (chrome, find bar, gestures, history)
- `ios/engine/` — platform layer: `AppleCoreTextPlatform`, `CoreGraphicsView`,
  `AppleImgbuf`, `IosCapi`, stubs for the desktop-only pieces
- `ios/PLAN.md` / `ios/RESULT.md` — living roadmap and milestone log
