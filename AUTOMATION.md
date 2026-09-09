# Driving Dillo from a script

Two separate surfaces, with very different capabilities. Read the limits before
you plan anything around them.

|                  | desktop control socket              | iOS bridge                 |
| ---------------- | ----------------------------------- | -------------------------- |
| where            | `src/IO/control.c` + `src/dilloc.c` | `ios/Dillo/DilloBridge.h`  |
| driven by        | any process, over a UNIX socket     | in-process Swift/ObjC only |
| scriptable today | **yes**                             | **no** (see below)         |
| JavaScript       | none                                | none                       |
| network stack    | dillo's own HTTP + OpenSSL          | `URLSession`               |

---

## 1. The control socket (`dilloc`)

Upstream feature by Rodrigo Arias Mallo, on by default
(`--disable-control-socket` turns it off). Confirm a binary has it:

```console
$ build/src/dillo --version
Dillo v3.2.0-210-gf330e201
Features: ... +CTL          # <- this
```

**Homebrew's `dillo` does not ship `dilloc`.** Build it here:

```sh
cd build
PATH="$(brew --prefix fltk@1.3)/bin:$PATH" make -j$(sysctl -n hw.ncpu)
# -> build/src/dillo  and  build/src/dilloc
```

### Apple Shortcuts and automation URLs

The macOS app bundle and iOS app both register the `dillo:` automation
scheme. Put a URL after the scheme to open it in Dillo:

```sh
open 'dillo:https://example.com/path'
```

In Apple Shortcuts, concatenate `dillo:` with a URL variable and pass the
result to the system **Open URLs** action. This works on macOS and iOS and
does not require Dillo to be running first. The longer, percent-encoded form
`dillo://open?url=https%3A%2F%2Fexample.com` is also accepted.

On iOS, Dillo additionally contributes an **Open URL in Dillo** action
directly to Shortcuts and Siri.

### Connecting

Each running Dillo listens on `~/.dillo/ctl/<pid>`. `dilloc` uses the first
process that answers; set `DILLO_PID` to pick one. Dillo exports `DILLO_PID`
itself into `link_action` / `page_action` commands, so context-menu actions can
call back into the browser that launched them.

### The whole command set

```
ping           check liveness ("pong")
pid            print the PID being controlled
ready          exit 0 if the tab finished loading, 1 otherwise
wait [T]       block until loaded, max T seconds (default 60, 0 = forever)
open URL       load URL in the current tab
url            print the current tab's URL
title          print the current tab's <title>
status [MSG]   set (or clear) the status bar text
dump           print the current page's source to stdout
hdump          print the current page's HTTP response headers
load           replace the tab's content with stdin
rawload        replace the tab's headers AND content with stdin
reload         reload the current tab
cmd NAME       run a keybinding by name
quit           close dillo

links               list links: index TAB absolute href
forms               list forms: index TAB method TAB action TAB enctype
inputs N            list form N's inputs: index TAB type TAB name TAB value
resolve HREF        print HREF resolved against the current page URL
set N NAME [VALUE]  set input NAME of form N (VALUE = rest of the line)
submit N [NAME]     submit form N as if submit button NAME was clicked
reset N             reset form N
follow N            open link N
noninteractive [0|1] print/set non-interactive mode (dialogs auto-cancel)
```

Commands exit non-zero on error. `load`/`rawload` read stdin, everything else
writes to stdout. `set` and `submit` use distinct exit codes (1 no such
form, 2 no such input/button, 3 not settable/refused, 4 value rejected).
`submit` and `follow` start a page load and return at once — run `wait`
after them. A value given to `set` may contain spaces but not newlines (the
command is one line); checkbox/radio values are reported as `on:VALUE` /
`off:VALUE`; file inputs cannot be set. The `noninteractive` mode (also
`noninteractive=YES` in `dillorc`) answers every modal dialog with its
cancel/refuse default so a script can never hang on one.

`cmd NAME` is thinner than it looks — `a_UIcmd_by_name` in `src/uicmd.cc` only
implements `new-tab`, `close-tab`, `left-tab`, `right-tab`, `new-window`. Every
other keybinding name returns -1.

### The fetch loop

```sh
D=build/src/dilloc
$D open "https://example.com/page" && $D wait 30 && $D dump > page.html
$D hdump | head -1     # HTTP/1.1 200 OK
```

### The form loop

Discover, fill, submit — nothing here needs the page's HTML parsed by hand:

```sh
D=build/src/dilloc
$D noninteractive 1                 # never block on a modal dialog

$D forms                            # 0  GET  https://ex.example/search  application/x-www-form-urlencoded
$D inputs 0                         # 0  text  q  <empty>
                                    # 1  submit  go  Search
$D set 0 q "boise idaho"            # spaces fine, newlines impossible
$D submit 0 go                      # submits as if the "go" button was clicked
                                    # then settle (see the wait caveat in Limits)
$D url                              # ...?q=boise+idaho&go=Search
```

Checkbox and radio values read back as `on:VALUE` / `off:VALUE`, so `inputs N` is also how
you confirm a `set` landed. Only the _named_ submit button is included in the query, an
unchecked checkbox is omitted entirely rather than sent as `off`, and only the selected
radio is sent — verified 2026-09-08 against `test/dilloc/form.html`.

For links, `links` prints absolute hrefs (fragments already resolved against the page URL),
and `follow N` takes the same index:

```sh
$D links | grep -i 'contact' | cut -f1 | head -1 | xargs $D follow
```

### Rendering bytes you fetched elsewhere

`load`/`rawload` go the other direction — you hand Dillo bytes and it renders
them. That is what the shipped example config does:

```
page_action="Reader mode:dilloc dump | rdrview -H | dilloc load"
page_action="Mimic Chrome:curl_chrome136 -i --raw $url | dilloc rawload"
```

### Limits (the real ones)

- **No JavaScript engine.** Not "limited JS" — none. Only data present in the
  server's HTML is reachable.
- **Addressing is by index and name only.** `follow N` and `submit N` take
  the indices that `links`/`forms` print; `set` takes an input name. There is
  no click-by-visible-text and no selector — see AUTOMATION-PLAN.md.
- `dump` returns page **source**, not rendered text. There is no text-extraction
  command; you parse the HTML yourself.
- Opening the URL that is **already displayed** is a no-op: nothing reloads, and
  `ready` / `wait` return immediately. (Until 2026-09-08 it wedged the tab in a
  never-finished state — `a_Nav_push` armed the load expectation and
  `Nav_open_url` then started no load to clear it. Fixed in `src/nav.c`.) Use
  `reload` if you actually want the page re-fetched.
- Commands target `a_UIcmd_get_first_active_bw()` — one implicit target, no
  per-tab addressing.
- It is a GUI app. Fine on macOS; headless Linux needs Xvfb or similar.
- No response status code is exposed directly — read the first line of `hdump`.
- **`wait` works on a light page and not on a heavy one.** It gates on
  `a_UIcmd_has_finished()`, which stays false while any subresource is outstanding — and on
  a big commercial page something never completes. Same binary, same session, both URLs
  freshly opened (2026-09-08, `v3.2.0-210-gf330e201-dirty`):

  | page                           | `wait 20`                                                                                                                                 |
  | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
  | `https://dillo-browser.org/`   | returns in **1.8 s**                                                                                                                      |
  | a Zillow `homedetails` listing | burns all **20 s**, exits 1, `Current url not cached` — while `hdump` said `200 OK`, `title` was right, and `dump` returned 903,966 bytes |

  So a script that gates on `wait`'s exit code discards good pages. Treat `wait` as a hint
  and settle on the page's own byte count instead — poll `dump` until two consecutive reads
  are the same length. **This matters most for the two verbs whose help text tells you to
  use `wait`:** `submit` and `follow` both say "run `wait` afterwards", which is exactly the
  advice that fails on the pages worth automating. (This is separate from the already-fixed
  `nav.c` bug below, which had the same symptom for a different reason.)

- **No instance isolation.** Sockets are `~/.dillo/ctl/<pid>`, and bare `dilloc` refuses
  outright — `multiple ctl sockets found, set DILLO_PID` — the moment a second Dillo
  exists, for any reason (a browser someone opened, another agent running the `test/dilloc`
  suite). Any script that matters should spawn its own instance and pin `DILLO_PID` to it
  rather than hoping it is the only one.

### What has actually been exercised

Behaviour confirmed by running it on 2026-09-08 against
`v3.2.0-210-gf330e201-dirty`, not read off the source:

|                                    |                                                                                                                |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `links` `forms` `inputs` `resolve` | correct on `test/dilloc/form.html`; `links` also on a live Zillow page — 77 rows, absolute, no empty-href rows |
| `set`                              | round-trips text, password, checkbox, radio, select, textarea, hidden                                          |
| `set` failure codes                | `2` no such input, `3` not settable, `4` value rejected — as documented                                        |
| `submit N NAME`                    | correct form encoding, including which button and which checkbox/radio are sent                                |
| `follow N`                         | live network navigation                                                                                        |
| `noninteractive`                   | reports and sets                                                                                               |
| `forms` on a JS-driven page        | correctly empty — Zillow's search box is not a `<form>` in the server HTML                                     |

Not exercised: the new `form.cc` paths under memory pressure, `reset N`, file inputs,
multipart POST to a real endpoint, or any of it on Linux/CI.

---

## 2. Bot-detection behaviour (verified 2026-09-08)

Dillo's TLS + header fingerprint is not on the usual blocklists. Measured from
one macOS laptop, same URL, same minute:

| client                                                 | result     |
| ------------------------------------------------------ | ---------- |
| `curl`                                                 | 403        |
| `curl -A '<Chrome 136 UA>'`                            | 403        |
| Dillo, sending `User-Agent: Dillo/3.2.0-210-gf330e201` | **200 OK** |

Target was a Zillow `homedetails` page behind PerimeterX; 3/3 listing pages and
one search page all returned 200 with no captcha. The block therefore keys on
the **client fingerprint** (TLS handshake / header shape), not the IP and not
the UA string — spoofing the UA on curl changed nothing, and announcing itself
honestly as Dillo changed everything.

Useful because the pages carried their data server-side: JSON-LD
(`RealEstateListing` → price, `numberOfBedrooms`, `floorSize`, address, geo)
plus embedded search JSON (`unformattedPrice`, `beds`, `baths`, `livingArea`).
None of that needed JS. **A JS-rendered site would still come back empty.**

Not yet verified: whether this holds from a datacenter IP, over long runs, or
after the operator notices. Treat it as a measurement, not a guarantee, and
don't hammer anyone's origin with it.

---

## 3. The iOS bridge (`ios/`)

`ios/Dillo/DilloBridge.h` wraps one `dw::core::Layout` — the phone equivalent of
a desktop browser window. Swift fetches, the bridge parses and paints.

Public API, in full:

- lifecycle: `initWithViewportWidth:height:`, `setViewportWidth:height:`,
  `setFormHost:`, `clearCache`, `seedCSS:forURL:`, `loadHTML:url:`
- geometry: `canvasWidth` / `canvasHeight`, `drawIntoContext:visibleRect:`
- input: `tapAtX:y:`, `selectionBegan/Moved/Ended(x:y:)`, `reportScroll(x:y:)`
- navigation: `resolveURL:baseURL:`, `navigateFragment:`,
  `findText:caseSensitive:backwards:`, `resetFind`
- callbacks: `onInvalidate`, `onOpenURL(url, body, contentType)`, `onTitle`,
  `onScrollTo`, `onCopy`

Forms are **real UIKit controls** (`ios/Dillo/NativeFormResources.mm`, 929
lines) installed through a `ResourceFactory` on `ApplePlatform` — the iOS
counterpart to desktop `fltkui.cc`. `PageLoader.load(_:method:body:
bodyContentType:completion:)` does GET and POST through `URLSession`.

### URL automation exists; page automation does not

iOS 16 and later exposes **Open URL in Dillo** through App Intents, and the
cross-platform `dillo:` scheme described in §1 can launch or address the app.
Once a page is open, however, user interaction still arrives through
`UIGestureRecognizer` callbacks and `private func`s on
`BrowserViewController`. There is **no** control channel, no "fill the field
named `q`", and no text extraction — `tapAtX:y:` takes canvas pixel coordinates
and hit-tests, which means you would have to already know where the link is.

The primitives to build automation on are all present (`loadHTML:url:`,
`tapAtX:y:`, `findText:`, `onOpenURL`, POST in `PageLoader`), but nothing wires
them to anything but a finger. Adding a control channel here is a real project,
not a flag.

Also note the iOS side fetches with `URLSession`, **not** dillo's HTTP + OpenSSL
stack — so it does not inherit the fingerprint behaviour measured in §2. That is
a separate, untested question.

---

## 4. What adding click / type / submit to the socket would take

Investigated 2026-09-08. Shorter than you'd guess, because most of the
machinery already exists and is already public.

### Why it's cheap

**The threading problem doesn't exist.** `a_Control_init` registers the socket
with `a_IOwatch_add_fd`, which is `Fl::add_fd` (`src/IO/iowatch.cc:26`). Command
callbacks therefore run **on the FLTK main thread**, so they can touch widgets
and the dw tree directly. No marshalling, no locks, no deferred queue.

**The form internals are already public.** `src/form.cc:99` —
`public: //BUG: for now everything is public`. So `form->inputs`,
`input->type`, `input->name`, `input->init_str`, and `input->getResource()` are
all reachable.

**The setters already exist** in `dw/ui.hh`:
`TextResource::setText/getText` (entries + textareas),
`ToggleButtonResource::setActivated/isActivated` (checkbox, radio),
`SelectionResource` (select/option).

**Submit is already exported.** `html.hh` declares, inside `extern "C"`:

```c
void a_Html_form_submit(void *v_html, void *v_form);   /* validity-checked */
void a_Html_form_reset(void *v_html, void *v_form);
```

and `DilloHtmlForm::submit(DilloHtmlInput *active_input, EventButton *event)`
already accepts an "active submit" input, so naming the button that was
"clicked" is nearly free.

**Reaching the document from a bw is one call.** `a_Bw_get_current_doc(bw)`
returns the `DilloHtml*` as `void*`; `src/menu.cc:852` is the existing idiom.

### The shape of the work

`control.c` is C, so new logic goes in the C++ side and is exposed as
`extern "C" a_Html_*` in `html.hh`, exactly like `a_Html_load_images`.

New in `src/html.cc` / `src/form.cc` (~120 lines):

| function                                                                | does                                           |
| ----------------------------------------------------------------------- | ---------------------------------------------- |
| `a_Html_get_form(void *doc, int n)`                                     | index → `v_form` pointer                       |
| `a_Html_form_describe(void *doc, Dstr *out)`                            | forms: index, method, action, enc              |
| `a_Html_inputs_describe(void *doc, int n, Dstr *out)`                   | inputs: index, type, name, current value       |
| `a_Html_input_set(void *doc, int n, const char *name, const char *val)` | `setText` / `setActivated` / select by value   |
| `a_Html_links_describe(void *doc, Dstr *out)`                           | walk `html->links` (`SimpleVector<DilloUrl*>`) |
| `a_Html_link_url(void *doc, int n)`                                     | index → `DilloUrl*` for `a_UIcmd_open_url`     |

New verbs in `src/IO/control.c` + help text in `control.c` and `dilloc.c`
(~80 lines of `strncmp` dispatch in the existing chain):

```
links               list index + href of every link on the page
forms               list forms (index, method, action)
inputs N            list form N's inputs (index, type, name, value)
set N NAME [VALUE]  set input NAME in form N (VALUE from stdin if omitted)
submit N [NAME]     submit form N, as if button NAME was the one clicked
follow N            open link N
resolve HREF        print HREF resolved against the current page URL
```

Call it **~200–250 lines across four files**, no new subsystems.

### The parts that are actually hard

- **Clicking anything but a link index.** `html->links` holds hrefs and nothing
  else — no anchor text, no coordinates. Addressing a link by its visible text
  (never mind a CSS selector) means walking the dw widget tree's `Textblock`
  word lists, and there is no accessor for that today. This is the one piece
  that is a real project rather than an afternoon.
- **You may not need it.** `dilloc dump | <parse> | dilloc open <href>` already
  clicks links, as long as you can resolve relative hrefs — hence the cheap
  `resolve` verb above, which is just `a_Url_new(href, base)` from `url.c`.
- **Modal dialogs will hang a script.** `DilloHtmlForm::submit` calls
  `a_Dialog_choice` when an https page posts to http (`src/form.cc:1094`), and
  file inputs open a picker. A scripted browser needs a non-interactive mode
  that auto-answers or refuses instead of blocking the event loop forever.
- **Async replies.** `submit` and `follow` start a page load; the socket reply
  has to come back before the load finishes. Reuse the existing `wait`
  machinery (`bw_waiting` / `bw_waiting_req` / `a_Control_notify_finish`) or
  document that callers must run `dilloc wait` afterwards.
- **`select`/`option` quirks** — `ensureSelection()` and option ordering need
  care when setting by value rather than index.

### Note

The control socket is upstream code (Rodrigo Arias Mallo, 2025). If any of this
is meant to go anywhere but a private fork, it's worth asking upstream first —
this is close to the design they'd have opinions about.
