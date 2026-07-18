/*
 * Dillo iOS spike — Objective-C++ bridge around the dw engine.
 *
 * Milestone 2: the page is no longer hand-built with Textblock calls — a
 * hardcoded HTML document (with a <style> sheet) goes through the real
 * parser stack: src/html.cc → src/styleengine.cc → src/css*.cc → dw.
 * The setup mirrors what the desktop does across dillo.cc (init order)
 * and web.cc's a_Web_dispatch_by_type (root-widget styling), with
 * DilloHtml::write/finishParsing standing in for the cache callback.
 *
 * Compiled with -fno-exceptions/-fno-rtti to match the engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import "SpikeBridge.h"

#include <stdio.h>
#include <string.h>

#include "dw/core.hh"
#include "dw/textblock.hh"

#include "src/prefs.h"
#include "src/bw.h"
#include "src/web.hh"
#include "src/html_common.hh"
#include "src/styleengine.hh"

#include "AppleCoreTextPlatform.hh"
#include "CoreGraphicsView.hh"

/* The spike page: exercises headings, the cascade (<style> + inline style),
 * links, nested lists, blockquote, hr, pre/code, a table, and the
 * small-caps/underline/strike text decorations. No images, no forms. */
static const char *SPIKE_PAGE =
   "<!DOCTYPE html>"
   "<html><head><title>Dillo on iOS</title>"
   "<style>"
   "  body { background-color: #ffffff; }"
   "  h1 { color: #114488; }"
   "  .note { color: #666666; font-style: italic; }"
   "  .sc { font-variant: small-caps; }"
   "  .boxed { background-color: #eef3ff; margin: 8px; padding: 4px; }"
   "  td.num { text-align: right; color: #884411; }"
   "</style></head>"
   "<body>"
   "<h1>Dillo renders HTML on iOS</h1>"
   "<p class='note'>This page went through the real parser:"
   " html.cc &rarr; styleengine.cc &rarr; css.cc &rarr; dw.</p>"
   "<p>Text can be <b>bold</b>, <i>italic</i>, <u>underlined</u>,"
   " <s>struck</s>, <mark>marked</mark>, and"
   " <span class='sc'>Small Caps</span>. Links look"
   " <a href='http://dillo.org'>like this</a>. Entities: &amp; &lt; &gt;"
   " &copy; &ldquo;curly&rdquo; &mdash; and UTF-8: na&iuml;ve, \xc3\xa9l\xc3\xa8ve.</p>"
   "<h2>Lists</h2>"
   "<ul><li>disc one</li><li>disc two"
   "  <ol><li>nested decimal</li><li>another</li></ol></li>"
   "<li>disc three</li></ul>"
   "<blockquote class='boxed'>Blockquotes are indented 40px by the"
   " user-agent stylesheet, and this one carries a background.</blockquote>"
   "<hr>"
   "<h2>Table</h2>"
   "<table border='1'>"
   "<tr><th>Piece</th><th>Lines</th></tr>"
   "<tr><td>html.cc</td><td class='num'>4400</td></tr>"
   "<tr><td>styleengine.cc</td><td class='num'>1100</td></tr>"
   "<tr><td>cssparser.cc</td><td class='num'>1800</td></tr>"
   "</table>"
   "<h2>Preformatted</h2>"
   "<pre>int main(void) {\n   return 42;   /* monospace */\n}</pre>"
   "<p>That's ten years of engine code drawing through Core Text.</p>"
   "</body></html>";

@implementation SpikeBridge {
   dw::apple::ApplePlatform *_platform;   // owned by _layout
   dw::core::Layout *_layout;
   dw::apple::CoreGraphicsView *_view;    // owned by _layout
   BrowserWindow *_bw;
   DilloUrl *_url;
   char *_pageBuf;   // parser mutates and retains its input buffer
}

static void invalidateTrampoline (void *data)
{
   SpikeBridge *self = (__bridge SpikeBridge *)data;
   if (self.onInvalidate)
      self.onInvalidate ();
}

/* One-time engine-wide initialization; mirrors the relevant subset (and
 * order) of dillo.cc's main(). */
static void initDilloOnce (void)
{
   static bool done = false;
   if (done)
      return;
   done = true;

   a_Prefs_init ();

   /* The desktop defaults (DejaVu) don't exist on iOS; use fonts that do.
    * ApplePlatform would fall back to the system font anyway, but the
    * style engine also matches these names for the CSS generic families. */
   dFree (prefs.font_serif);      prefs.font_serif = dStrdup ("Georgia");
   dFree (prefs.font_sans_serif); prefs.font_sans_serif =
                                     dStrdup ("Helvetica Neue");
   dFree (prefs.font_monospace);  prefs.font_monospace =
                                     dStrdup ("Courier New");
   dFree (prefs.font_cursive);    prefs.font_cursive = dStrdup ("Snell Roundhand");
   dFree (prefs.font_fantasy);    prefs.font_fantasy = dStrdup ("Papyrus");

   a_Web_init ();
   a_Bw_init ();
   StyleEngine::init ();
}

- (instancetype)initWithViewportWidth:(int)width height:(int)height
{
   self = [super init];
   if (!self)
      return nil;

   initDilloOnce ();

   _platform = new dw::apple::ApplePlatform ();
   _layout = new dw::core::Layout (_platform);

   _view = new dw::apple::CoreGraphicsView ();
   _view->setInvalidateFunc (invalidateTrampoline, (__bridge void *)self);
   _layout->attachView (_view);

   _bw = a_Bw_new ();
   _bw->render_layout = _layout;

   _url = a_Url_new ("about:ios-spike", NULL);

   /* Wrap width must be known before parsing: the style engine computes
    * percentage/em lengths against it via the layout. */
   _layout->viewportSizeChanged (_view, width, height);

   DilloHtml *html = new DilloHtml (_bw, _url, "text/html; charset=UTF-8");

   /* Mirror a_Web_dispatch_by_type()'s RootUrl block: canvas background,
    * initial "body" style for the toplevel widget, attach to layout.
    * (html is freed by the widget's delete callback when _layout dies.) */
   {
      dw::core::style::Color *bgColor =
         dw::core::style::Color::create (_layout, prefs.bg_color);
      _layout->setBgColor (bgColor);

      StyleEngine styleEngine (_layout, _url, _url, _bw->zoom);
      styleEngine.startElement ("body", _bw);
      html->dw->setStyle (styleEngine.style (_bw));
   }
   _layout->setWidget (html->dw);

   /* Feed the whole document in one shot, as the cache callback would.
    * The parser tokenizes the buffer IN PLACE and keeps a pointer to it
    * (DilloHtml::Start_Buf), so it must be writable heap memory that
    * outlives parsing — the desktop cache guarantees the same. */
   _pageBuf = dStrdup (SPIKE_PAGE);
   html->write (_pageBuf, strlen (_pageBuf), 1 /* Eof */);
   html->finishParsing (0 /* ClientKey; no cache client exists */);

   _platform->runIdles ();

   return self;
}

- (void)dealloc
{
   delete _layout;   // deletes platform, view, widget tree → DilloHtml
   a_Bw_free (_bw);
   a_Url_free (_url);
   dFree (_pageBuf);
}

- (int)canvasWidth
{
   return _view->getCanvasWidth ();
}

- (int)canvasHeight
{
   return _view->getCanvasHeight ();
}

- (void)drawIntoContext:(CGContextRef)ctx
{
   // Late-queued idles (e.g. a resize triggered after the first pass) must
   // run before painting, or we'd paint a stale allocation.
   _platform->runIdles ();

   dw::core::Rectangle area;
   area.x = 0;
   area.y = 0;
   area.width = _view->getCanvasWidth ();
   area.height = _view->getCanvasHeight ();

   _view->setCGContext (ctx);
   _view->clearBackground (area.x, area.y, area.width, area.height);
   _layout->expose (_view, &area);
   _view->setCGContext (NULL);
}

@end
