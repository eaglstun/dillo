/*
 * Dillo iOS spike — Objective-C++ bridge around the dw engine.
 *
 * A direct port of test/dw/dw_example.cc with the four FLTK nouns swapped
 * for their Apple counterparts (see ios/PLAN.md). Compiled with
 * -fno-exceptions/-fno-rtti to match the engine; nothing here throws.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import "SpikeBridge.h"

#include <stdio.h>

#include "dw/core.hh"
#include "dw/textblock.hh"
#include "AppleCoreTextPlatform.hh"
#include "CoreGraphicsView.hh"

@implementation SpikeBridge {
   dw::apple::ApplePlatform *_platform;   // owned by _layout
   dw::core::Layout *_layout;
   dw::apple::CoreGraphicsView *_view;    // owned by _layout
}

static void invalidateTrampoline (void *data)
{
   SpikeBridge *self = (__bridge SpikeBridge *)data;
   if (self.onInvalidate)
      self.onInvalidate ();
}

- (instancetype)initWithViewportWidth:(int)width height:(int)height
{
   self = [super init];
   if (!self)
      return nil;

   _platform = new dw::apple::ApplePlatform ();
   _layout = new dw::core::Layout (_platform);

   _view = new dw::apple::CoreGraphicsView ();
   _view->setInvalidateFunc (invalidateTrampoline, (__bridge void *)self);
   _layout->attachView (_view);

   dw::core::style::StyleAttrs styleAttrs;
   styleAttrs.initValues ();
   styleAttrs.margin.setVal (5);

   dw::core::style::FontAttrs fontAttrs;
   fontAttrs.name = "Georgia";   // a serif that ships with iOS
   fontAttrs.size = 14;
   fontAttrs.weight = 400;
   fontAttrs.style = dw::core::style::FONT_STYLE_NORMAL;
   fontAttrs.letterSpacing = 0;
   fontAttrs.fontVariant = dw::core::style::FONT_VARIANT_NORMAL;
   styleAttrs.font = dw::core::style::Font::create (_layout, &fontAttrs);

   styleAttrs.color = dw::core::style::Color::create (_layout, 0x000000);
   styleAttrs.backgroundColor =
      dw::core::style::Color::create (_layout, 0xffffff);

   dw::core::style::Style *widgetStyle =
      dw::core::style::Style::create (&styleAttrs);

   dw::Textblock *textblock = new dw::Textblock (false);
   textblock->setStyle (widgetStyle);
   _layout->setWidget (textblock);

   widgetStyle->unref ();

   styleAttrs.margin.setVal (0);
   styleAttrs.backgroundColor = NULL;

   dw::core::style::Style *wordStyle =
      dw::core::style::Style::create (&styleAttrs);

   for (int i = 1; i <= 10; i++) {
      char buf[4];
      snprintf (buf, sizeof (buf), "%d.", i);

      const char *words[] = { "This", "is", "the", buf, "paragraph.",
                              "Here", "comes", "some", "more", "text",
                              "to", "demonstrate", "word", "wrapping.",
                              NULL };

      for (int j = 0; words[j]; j++) {
         textblock->addText (words[j], wordStyle);
         textblock->addSpace (wordStyle);
      }

      textblock->addParbreak (10, wordStyle);
   }

   wordStyle->unref ();

   textblock->flush ();

   // In dw_example, Fl::run() supplies the viewport size and pumps the idle
   // queue; here both are explicit.
   _layout->viewportSizeChanged (_view, width, height);
   _platform->runIdles ();

   return self;
}

- (void)dealloc
{
   delete _layout;   // also deletes platform and view
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
