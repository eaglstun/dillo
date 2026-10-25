# Plan: click, type, and form submit over the control socket

Roadmap for extending Dillo's `dilloc` control socket from "navigate and read"
to "fill in a form and submit it". Companion to **[AUTOMATION.md](AUTOMATION.md)**,
which documents the surface as it exists today — read that first.

Status: **Stages 1-4 and 6 are implemented and working; Stage 5 is the one left.**
Written 2026-09-08 against branch `ios-render-spike` at `f330e201`, implemented the same
day (uncommitted on that branch at the time of writing), and the result exercised by hand
— see "What has actually been exercised" in [AUTOMATION.md](AUTOMATION.md).

What shipped beyond the plan: the `dilloc` reply-after-stdin fix from Stage 0 item 3 was
taken, and a separate `src/nav.c` bug was found and fixed along the way (re-opening the
URL already displayed wedged the tab in a never-finished state).

**Stage 5 (async replies) is still open, and adding `submit`/`follow` made it matter more
than it did when this plan was written.** Both verbs' help text says "run `wait`
afterwards", and `wait` does not work on a heavy page: measured on the shipped build, it
returns in 1.8 s on `dillo-browser.org` and burns its full timeout, exiting 1, on a Zillow
listing that had already arrived complete. So the two newest verbs hand callers advice that
fails on exactly the pages worth automating. Either do Stage 5's deferred-reply version, or
make `wait` settle on content stability rather than `a_UIcmd_has_finished()`.

Everything below is the original plan, kept as written for the record.

---

## House rules for this work

**Build and verify:**

```sh
cd build
PATH="$(brew --prefix fltk@1.3)/bin:$PATH" make -j$(sysctl -n hw.ncpu)
# -> build/src/dillo, build/src/dilloc
build/src/dillo --version | grep +CTL     # sanity: control socket compiled in
```

**Language boundary.** `src/IO/control.c` is **C** (`-std=c99`). Everything that
touches `DilloHtml`, `DilloHtmlForm`, or dw widgets is **C++**. So all new logic
goes in `src/html.cc` / `src/form.cc` and is exposed as `extern "C" a_Html_*`
declarations in `src/html.hh`. The existing `a_Html_load_images(void *v_html,
DilloUrl *pattern)` is the exact template — read `src/menu.cc:852` for the
calling idiom.

**Style.** C99 for `.c`, C++11 for `.cc`, no RTTI, no exceptions. Warnings are
on (`-Wall -W -pedantic`) and the tree is clean — keep it that way.

**Reply protocol** (`Control_req_prepare_reply`, `src/IO/control.c:203`):
a reply is one status byte, a newline, then the payload. `dilloc` parses it as
`rc = buf[0] - '0'` (`src/dilloc.c:288`), so **status codes are a single digit,
0–9**. Convention in the existing code: `0` = success, `1` = error with a
human-readable message on the following line.

**Threading is a non-issue.** `a_Control_init` registers the socket with
`a_IOwatch_add_fd`, which is `Fl::add_fd` (`src/IO/iowatch.cc:26`). Command
callbacks run on the FLTK main thread. Touch widgets directly; do not add locks
or a deferred queue.

**Don't break `bw == NULL`.** `a_UIcmd_get_first_active_bw()` can return NULL
when no window is open. The existing handlers don't check. New handlers should,
and should return `1` with a message rather than segfaulting.

---

## Stage 0 — Decisions to make before writing code

Settle these first; they change the shape of every later stage.

1. **Addressing.** Forms and links by integer index (`form 0`, `link 12`) is
   what the data structures already give you. Inputs by `name` is friendlier and
   matches HTML. Recommendation: **forms and links by index, inputs by name**,
   with an `inputs N` verb so a script can discover the names.
2. **Values containing whitespace or newlines.** The command is a single
   newline-terminated line (`Control_req_read_cmd`, `src/IO/control.c:155`
   truncates at the first `\n` or `\r`), and `dilloc` joins `argv[1..]` with
   single spaces (`src/dilloc.c:229`). So `set 0 q hello world` works, but a
   value with a newline in it **cannot** be expressed. Options: accept the
   limitation, percent-decode the value, or read it from stdin (see item 3).
3. **The stdin/reply problem.** `dilloc` special-cases `load`/`rawload`: it
   forwards stdin then **closes the socket and returns 0 without reading a
   reply** (`src/dilloc.c:246-270`). Any new stdin-taking verb inherits that —
   you would get no status back. The fix is to `shutdown(fd, SHUT_WR)` after
   forwarding stdin and then fall through to the normal reply-reading path.
   That is a small, self-contained improvement to `dilloc.c` and arguably a bug
   fix for `load` too. Decide whether this work includes it.
4. **Upstream or fork.** The control socket is Rodrigo Arias Mallo's upstream
   work (2025). If any of this is meant for upstream, propose the verb set on
   the mailing list / issue tracker _before_ Stage 1 — this is precisely the
   kind of API they will have opinions about.

---

## Stage 1 — Read-only introspection

**Goal:** a script can see what is on the page. No state changes, so nothing can
break; this stage is the safety net for every stage after it.

**New in `src/html.cc`, declared in `src/html.hh` inside the `extern "C"` block:**

| function                                                     | notes                                                                    |
| ------------------------------------------------------------ | ------------------------------------------------------------------------ |
| `void a_Html_links_describe(void *v_html, Dstr *out)`        | walk `html->links` (`SimpleVector<DilloUrl*>`, `src/html_common.hh:215`) |
| `void a_Html_forms_describe(void *v_html, Dstr *out)`        | walk `html->forms` (`html_common.hh:213`)                                |
| `int a_Html_inputs_describe(void *v_html, int n, Dstr *out)` | form `n`'s `form->inputs`                                                |

`DilloHtmlForm` and `DilloHtmlInput` live in `src/form.cc` and are not visible
from `html.cc`, so either move the class declarations to `form.hh` or put these
three in `form.cc` and forward-declare. **Prefer adding them to `form.cc`** and
declaring them in `html.hh` — smaller diff, no header churn.

Everything needed is already public — `src/form.cc:99` reads
`public: //BUG: for now everything is public`. Relevant members: `form->method`,
`form->action`, `form->content_type`, `form->inputs`; `input->type`
(`DilloHtmlInputType`, enum at `src/form.cc:51`), `input->name`,
`input->init_str`, `input->getResource()`.

Current values come from `dw/ui.hh`: `TextResource::getText()` for
text/password/textarea, `ToggleButtonResource::isActivated()` for
checkbox/radio, `SelectionResource` for select.

**New verbs in `Control_req_prepare_reply` (`src/IO/control.c:203`)** — append to
the existing `else if` chain, and add matching lines to the `help` text there
**and** to the summary in `dilloc`'s own docs:

```
links               list every link: index and absolute href
forms               list every form: index, method, action, enctype
inputs N            list form N's inputs: index, type, name, current value
resolve HREF        print HREF resolved against the current page URL
```

`resolve` is just `a_Url_new(href, a_UIcmd_get_location_text(bw))` — it belongs
in this stage because it makes `dump | parse | open` a complete link-following
story without any click verb at all.

**Watch out:** `html->links` entries can be **NULL** (`src/html.cc:310` stores
NULL when the href is empty). Print an empty field, don't dereference.

**Acceptance:**

```sh
$DILLOC open "file://$PWD/test/dilloc/form.html" && $DILLOC wait
$DILLOC links   | head            # index + href, no crash on NULL entries
$DILLOC forms                     # at least one form with method/action
$DILLOC inputs 0                  # names match the HTML source
$DILLOC inputs 99; echo $?        # -> 1, message, no crash
$DILLOC resolve ../x.html         # absolute URL against the page URL
```

Add `test/dilloc/form.html` (a form with text, password, checkbox, radio group,
select, textarea, hidden, and two named submit buttons) plus
`test/dilloc/cmd-introspect.sh`, registered in `test/dilloc/Makefile.am` under
`TESTS` and `EXTRA_DIST`. Follow `cmd-title.sh` for shape; `driver.sh` provides
`$DILLO` and `$DILLOC`.

---

## Stage 2 — Typing

**Goal:** set an input's value without a keyboard.

**New:** `int a_Html_input_set(void *v_html, int form_n, const char *name,
const char *value)` in `src/form.cc`.

Find the input by `name` in `form->inputs`, then switch on `input->type`:

- `TEXT`, `PASSWORD`, `TEXTAREA` → `((TextResource *)res)->setText(value)`
- `CHECKBOX` → `((ToggleButtonResource *)res)->setActivated(truthy(value))`
- `RADIO` → use `form->getRadioInput(name)` (already public,
  `src/form.cc:118`) and activate the member whose value matches
- `SELECT`, `SEL_LIST` → match `value` against the options via
  `DilloHtmlSelect` (`src/form.cc:221`); mind `ensureSelection()`
- `HIDDEN` → set `input->init_str` directly (there is no widget)
- `FILE` → **refuse** in this stage; `DilloHtmlInput::readFile` opens a dialog
  (see Stage 4)
- anything else → return an error

Return distinct codes so the caller can tell "no such form" from "no such input"
from "type not settable" — remember the single-digit ceiling.

**New verb:** `set N NAME VALUE`.

**Acceptance:** `set` then `inputs N` round-trips every supported type; the value
is visible in the actual widget on screen; setting an unknown name or an
unsupported type returns non-zero with a message and changes nothing.

---

## Stage 3 — Submit and follow

**Goal:** the page actually goes somewhere.

**Submit is half-built already.** `src/html.hh` exports, inside `extern "C"`:

```c
void a_Html_form_submit(void *v_html, void *v_form);   /* validity-checked */
void a_Html_form_reset(void *v_html, void *v_form);
```

and `DilloHtmlForm::submit(DilloHtmlInput *active_input, EventButton *event)` is
public (`src/form.cc:119`). The only missing piece is index → `v_form`, so add:

```c
void *a_Html_get_form(void *v_html, int n);   /* NULL if out of range */
```

`active_input` is what puts the clicked button's `name=value` into the query, so
support `submit N [NAME]`: look up the named submit button in `form->inputs` and
pass it. Passing NULL (as `a_Html_form_submit2` does today) submits with no
active button, which is correct for a form with a single submit.

**Following a link:** `void *a_Html_link_url(void *v_html, int n)` returning the
`DilloUrl*` from `html->links`, then `a_UIcmd_open_url(bw, url)` in control.c.
Note the index is the same link id the renderer uses, so `follow N` and a real
click on link N take the same path (minus ismap coordinates, which
`Html_set_link_coordinates` adds on a real click — out of scope).

**New verbs:** `submit N [NAME]`, `reset N`, `follow N`.

**Acceptance:** `set` + `submit` against a local CGI or `httpbin`-style echo
returns the values that were set; a GET form's query string is visible in
`dilloc url`; `submit 0 btn2` includes `btn2`'s name/value and `submit 0 btn1`
includes `btn1`'s; `follow N` lands on the href that `links` reported for N.

---

## Stage 4 — Non-interactive mode

**Goal:** a scripted browser can never be hostage to a dialog nobody will click.

`DilloHtmlForm::submit` calls `a_Dialog_choice` when an https page submits to
http (`src/form.cc:1092-1100`). File inputs open a picker. Either blocks the
FLTK event loop indefinitely — including the socket's own callback, so the
script hangs too and `dilloc` never gets a reply.

Add a non-interactive flag (a `prefs` entry plus a `dillorc` option, or a
`noninteractive 0|1` verb, or both) that makes modal prompts resolve to a safe
default and report the refusal rather than blocking. At minimum cover the
insecure-form-submission dialog and the file picker. Audit for others:

```sh
grep -rn "a_Dialog_" src/ | grep -v "dialog.cc"
```

**Acceptance:** submitting an http form from an https page under
non-interactive mode returns a non-zero status with an explanatory message and
does not block; `dilloc ping` still answers immediately afterwards.

---

## Stage 5 — Async replies

**Goal:** `submit` and `follow` don't lie about when they're done.

Both kick off a page load. The reply currently returns immediately, so a script
must call `dilloc wait` afterwards. Two options:

- **Cheap:** document that `submit`/`follow` are fire-and-forget, and require
  `wait` after. Zero new machinery.
- **Better:** reuse the existing deferred-reply path — `bw_waiting`,
  `bw_waiting_req`, `done = 0`, `a_Control_notify_finish`,
  `handle_wait_timeout` (all in `src/IO/control.c`, see the `wait` handler at
  line 303). Note `wait` already refuses to nest (`"already waiting"`), so a
  submit-that-waits must not collide with a concurrent `wait`.

Recommendation: ship Stage 3 with the cheap option and a clear note in `help`,
then do the deferred version here if scripts turn out to need it.

Also land the `dilloc` `shutdown(SHUT_WR)` fix from Stage 0 item 3 if it was
accepted, and give it a regression test alongside `cmd-load-deadlock.sh`.

---

## Stage 6 — Documentation and tests

- Extend `doc/user_help.in.html` (the remote-control section is around line 824)
  with the new verbs and a worked example.
- Update the `help` text in `src/IO/control.c` — it is the real reference and
  drifts easily.
- Update `AUTOMATION.md` §1 (the verb table) and §3's "not scriptable" note if
  anything changed on the iOS side.
- Test files: `test/dilloc/form.html`, `cmd-introspect.sh`, `cmd-set.sh`,
  `cmd-submit.sh`, `cmd-follow.sh`, all registered in `test/dilloc/Makefile.am`.

**Test-harness notes.** `test/dilloc/driver.sh` sources `test/lib/dilloc.sh`,
which calls `xorg_start_server` and `readlink -f`. That path is Linux/CI-shaped:

- Xvfb is skipped if `DILLO_DISPLAY` is set (`test/lib/xorg.sh`), but macOS FLTK
  uses Cocoa, not X11, so windows will simply appear on the real desktop.
- `enable_dilloc_tests` requires `render_tools_ok` (`configure.ac:822`), so the
  suite is likely **off** in the local macOS build. Check with
  `grep 'ENABLE_DILLOC_TESTS' build/Makefile` before assuming `make check` runs
  them.

Expect to verify by hand on macOS and let CI run the suite on Linux. Write the
tests anyway.

---

## Scope estimate

Roughly **200–250 lines across four files** (`src/form.cc`, `src/html.hh`,
`src/IO/control.c`, `src/dilloc.c`) plus tests and docs. No new subsystems, no
new build targets, no threading work.

## Explicitly out of scope

- **Clicking by visible text or selector.** `html->links` holds hrefs and
  nothing else — no anchor text, no coordinates. Addressing a link by what it
  says means walking the dw widget tree's `Textblock` word lists, and no
  accessor exists. That is a separate project, and `dump | parse | open` covers
  most of what it would buy.
- **JavaScript.** There is no engine and this plan does not add one.
- **Per-tab addressing.** Commands target `a_UIcmd_get_first_active_bw()`.
  Changing that is a broader redesign of the socket API.
- **The iOS port.** Different fetch stack, different input path, no control
  channel — see `AUTOMATION.md` §3.
