/*
 * Dillo Widget — Core Graphics view implementation.
 *
 * Paint primitives modeled on dw/fltkviewbase.cc / fltkviewport.cc, with the
 * async damage machinery cut down to a single invalidate callback: this view
 * has no viewport, so canvas coordinates are context coordinates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <CoreText/CoreText.h>
#include <string.h>
#include <wctype.h>

#include "../../lout/msg.h"
#include "../../lout/misc.hh"
#include "CoreGraphicsView.hh"
#include "AppleCoreTextPlatform.hh"

namespace dw {
namespace apple {

using namespace lout;

CoreGraphicsView::CoreGraphicsView ()
{
   layout = NULL;
   cg = NULL;
   canvasWidth = canvasAscent = canvasDescent = 0;
   bgColor = 0xffffff;
   invalidateFunc = NULL;
   invalidateData = NULL;
}

CoreGraphicsView::~CoreGraphicsView ()
{
}

void CoreGraphicsView::setCGContext (CGContextRef cg)
{
   this->cg = cg;
}

void CoreGraphicsView::setInvalidateFunc (void (*func) (void *data),
                                          void *data)
{
   invalidateFunc = func;
   invalidateData = data;
}

static inline void setCGColor (CGContextRef cg, int rgb, bool fill)
{
   CGFloat r = ((rgb >> 16) & 0xff) / 255.0;
   CGFloat g = ((rgb >> 8) & 0xff) / 255.0;
   CGFloat b = (rgb & 0xff) / 255.0;
   if (fill)
      CGContextSetRGBFillColor (cg, r, g, b, 1.0);
   else
      CGContextSetRGBStrokeColor (cg, r, g, b, 1.0);
}

void CoreGraphicsView::setStrokeColor (core::style::Color *color,
                                       core::style::Color::Shading shading)
{
   setCGColor (cg, ((AppleColor*)color)->colors[shading], false);
}

void CoreGraphicsView::setFillColor (core::style::Color *color,
                                     core::style::Color::Shading shading)
{
   setCGColor (cg, ((AppleColor*)color)->colors[shading], true);
}

// ----------------------------------------------------------------------

void CoreGraphicsView::setLayout (core::Layout *layout)
{
   this->layout = layout;
}

void CoreGraphicsView::setCanvasSize (int width, int ascent, int descent)
{
   canvasWidth = width;
   canvasAscent = ascent;
   canvasDescent = descent;
}

void CoreGraphicsView::setCursor (core::style::Cursor cursor)
{
   /* no cursors on a touch screen */
}

void CoreGraphicsView::setBgColor (core::style::Color *color)
{
   bgColor = color ?
      ((AppleColor*)color)->colors[core::style::Color::SHADING_NORMAL] :
      0xffffff;
}

void CoreGraphicsView::clearBackground (int x, int y, int width, int height)
{
   if (!cg)
      return;
   setCGColor (cg, bgColor, true);
   CGContextFillRect (cg, CGRectMake (x, y, width, height));
}

bool CoreGraphicsView::usesViewport ()
{
   return false;
}

/* Not called when usesViewport() is false. */

int CoreGraphicsView::getHScrollbarThickness ()
{
   return 0;
}

int CoreGraphicsView::getVScrollbarThickness ()
{
   return 0;
}

int CoreGraphicsView::getScrollbarOnLeft ()
{
   return 0;
}

void CoreGraphicsView::scrollTo (int x, int y)
{
}

void CoreGraphicsView::setViewportSize (int width, int height,
                                        int hScrollbarThickness,
                                        int vScrollbarThickness)
{
}

void CoreGraphicsView::startDrawing (core::Rectangle *area)
{
}

void CoreGraphicsView::finishDrawing (core::Rectangle *area)
{
}

void CoreGraphicsView::queueDraw (core::Rectangle *area)
{
   /* Coarse: invalidate everything. Fine-grained dirty rects are post-spike
    * (see FltkViewBase::drawRegion for the model). */
   if (invalidateFunc)
      invalidateFunc (invalidateData);
}

void CoreGraphicsView::queueDrawTotal ()
{
   if (invalidateFunc)
      invalidateFunc (invalidateData);
}

void CoreGraphicsView::cancelQueueDraw ()
{
}

// ----------------------------------------------------------------------

void CoreGraphicsView::drawPoint (core::style::Color *color,
                                  core::style::Color::Shading shading,
                                  int x, int y)
{
   if (!cg)
      return;
   setFillColor (color, shading);
   CGContextFillRect (cg, CGRectMake (x, y, 1, 1));
}

void CoreGraphicsView::drawLine (core::style::Color *color,
                                 core::style::Color::Shading shading,
                                 int x1, int y1, int x2, int y2)
{
   if (!cg)
      return;
   setStrokeColor (color, shading);
   CGContextSetLineWidth (cg, 1.0);
   /* +0.5 puts a 1px stroke exactly on the pixel row/column, matching
    * FLTK's integer-grid lines */
   CGContextBeginPath (cg);
   CGContextMoveToPoint (cg, x1 + 0.5, y1 + 0.5);
   CGContextAddLineToPoint (cg, x2 + 0.5, y2 + 0.5);
   CGContextStrokePath (cg);
}

void CoreGraphicsView::drawTypedLine (core::style::Color *color,
                                      core::style::Color::Shading shading,
                                      core::style::LineType type, int width,
                                      int x1, int y1, int x2, int y2)
{
   if (!cg)
      return;

   CGFloat w = (width == 1) ? 1 : width;

   CGContextSaveGState (cg);
   if (type == core::style::LINE_DOTTED) {
      CGFloat dash[] = { w, w };
      CGContextSetLineDash (cg, 0, dash, 2);
      CGContextSetLineCap (cg, kCGLineCapRound);
   } else if (type == core::style::LINE_DASHED) {
      CGFloat dash[] = { 3 * w, 3 * w };
      CGContextSetLineDash (cg, 0, dash, 2);
      CGContextSetLineCap (cg, kCGLineCapRound);
   }

   setStrokeColor (color, shading);
   CGContextSetLineWidth (cg, w);
   CGContextBeginPath (cg);
   CGContextMoveToPoint (cg, x1 + 0.5, y1 + 0.5);
   CGContextAddLineToPoint (cg, x2 + 0.5, y2 + 0.5);
   CGContextStrokePath (cg);
   CGContextRestoreGState (cg);
}

void CoreGraphicsView::drawRectangle (core::style::Color *color,
                                      core::style::Color::Shading shading,
                                      bool filled,
                                      int x, int y, int width, int height)
{
   if (!cg)
      return;

   if (width < 0) {
      x += width;
      width = -width;
   }
   if (height < 0) {
      y += height;
      height = -height;
   }

   if (filled) {
      setFillColor (color, shading);
      CGContextFillRect (cg, CGRectMake (x, y, width, height));
   } else {
      setStrokeColor (color, shading);
      CGContextSetLineWidth (cg, 1.0);
      /* inset by half the line width so the 1px outline covers the same
       * pixels as FLTK's fl_rect */
      CGContextStrokeRect (cg,
                           CGRectMake (x + 0.5, y + 0.5,
                                       width - 1.0, height - 1.0));
   }
}

void CoreGraphicsView::drawArc (core::style::Color *color,
                                core::style::Color::Shading shading,
                                bool filled,
                                int centerX, int centerY,
                                int width, int height,
                                int angle1, int angle2)
{
   if (!cg)
      return;

   /* FLTK: angles in degrees, counter-clockwise, 0 = three-o'clock, inside
    * the box (centerX - width/2, ...). The context is y-flipped (UIKit), so
    * counter-clockwise angles become clockwise ones. */
   CGContextSaveGState (cg);
   CGContextTranslateCTM (cg, centerX, centerY);
   CGContextScaleCTM (cg, width / 2.0, height / 2.0);

   CGFloat a1 = -angle1 * M_PI / 180.0;
   CGFloat a2 = -angle2 * M_PI / 180.0;

   CGContextBeginPath (cg);
   if (filled)
      CGContextMoveToPoint (cg, 0, 0);
   CGContextAddArc (cg, 0, 0, 1.0, a1, a2, 1 /* clockwise in flipped CTM */);
   if (filled)
      CGContextClosePath (cg);

   /* Reset the CTM before stroking so line width is in device space, but
    * keep the path built in the scaled space. */
   CGContextRestoreGState (cg);
   if (filled) {
      setFillColor (color, shading);
      CGContextFillPath (cg);
   } else {
      setStrokeColor (color, shading);
      CGContextSetLineWidth (cg, 1.0);
      CGContextStrokePath (cg);
   }
}

void CoreGraphicsView::drawPolygon (core::style::Color *color,
                                    core::style::Color::Shading shading,
                                    bool filled, bool convex,
                                    core::Point *points, int npoints)
{
   if (!cg || npoints <= 0)
      return;

   CGContextBeginPath (cg);
   CGContextMoveToPoint (cg, points[0].x, points[0].y);
   for (int i = 1; i < npoints; i++)
      CGContextAddLineToPoint (cg, points[i].x, points[i].y);
   CGContextClosePath (cg);

   if (filled) {
      setFillColor (color, shading);
      if (convex)
         CGContextFillPath (cg);
      else
         CGContextEOFillPath (cg);   /* matches fl_*_complex_polygon */
   } else {
      setStrokeColor (color, shading);
      CGContextSetLineWidth (cg, 1.0);
      CGContextStrokePath (cg);
   }
}

/*
 * Draw a UTF-8 run with baseline at (x, y) in canvas (top-left, y-down)
 * coordinates. Uses the context fill color for the glyphs.
 */
void CoreGraphicsView::drawTextRun (CTFontRef font, double x, double y,
                                    const char *text, int len)
{
   CFStringRef s = CFStringCreateWithBytes (kCFAllocatorDefault,
                                            (const UInt8*)text, len,
                                            kCFStringEncodingUTF8, false);
   if (s == NULL)
      return;

   const void *keys[] = { kCTFontAttributeName,
                          kCTForegroundColorFromContextAttributeName };
   const void *values[] = { font, kCFBooleanTrue };
   CFDictionaryRef attrs =
      CFDictionaryCreate (kCFAllocatorDefault, keys, values, 2,
                          &kCFTypeDictionaryKeyCallBacks,
                          &kCFTypeDictionaryValueCallBacks);
   CFAttributedStringRef as =
      CFAttributedStringCreate (kCFAllocatorDefault, s, attrs);
   CTLineRef line = CTLineCreateWithAttributedString (as);

   /* Core Text draws y-up; the UIKit context is y-down. Flip locally around
    * the baseline so glyphs come out upright. */
   CGContextSaveGState (cg);
   CGContextSetTextMatrix (cg, CGAffineTransformIdentity);
   CGContextTranslateCTM (cg, x, y);
   CGContextScaleCTM (cg, 1.0, -1.0);
   CGContextSetTextPosition (cg, 0, 0);
   CTLineDraw (line, cg);
   CGContextRestoreGState (cg);

   CFRelease (line);
   CFRelease (as);
   CFRelease (attrs);
   CFRelease (s);
}

void CoreGraphicsView::drawText (core::style::Font *font,
                                 core::style::Color *color,
                                 core::style::Color::Shading shading,
                                 int x, int y, const char *text, int len)
{
   if (!cg)
      return;

   AppleFont *af = (AppleFont*) font;
   setFillColor (color, shading);

   if (!font->letterSpacing && !font->fontVariant) {
      drawTextRun (af->font, x, y, text, len);
   } else {
      /* Nonzero letter spacing or small caps: draw glyph by glyph, advancing
       * by measured widths — the same widths ApplePlatform::textWidth sums,
       * so painting stays in step with layout. */
      double viewX = x;
      int curr = 0, next = 0;

      if (font->fontVariant == core::style::FONT_VARIANT_SMALL_CAPS) {
         char chbuf[4];

         for (curr = 0; next < len; curr = next) {
            next = layout->nextGlyph (text, curr);
            unsigned c = utf8Decode (text, curr, next);
            unsigned cu = towupper (c);

            if (cu == c) {
               double width =
                  AppleFont::measureWidth (af->font, text + curr, next - curr);
               if (curr && width)
                  viewX += font->letterSpacing;
               drawTextRun (af->font, viewX, y, text + curr, next - curr);
               viewX += width;
            } else {
               int nb = utf8Encode (cu, chbuf);
               CTFontRef scFont = af->getSmallCapsFont ();
               double width = AppleFont::measureWidth (scFont, chbuf, nb);
               if (curr && width)
                  viewX += font->letterSpacing;
               drawTextRun (scFont, viewX, y, chbuf, nb);
               viewX += width;
            }
         }
      } else {
         while (next < len) {
            next = layout->nextGlyph (text, curr);
            double width =
               AppleFont::measureWidth (af->font, text + curr, next - curr);
            if (curr && width)
               viewX += font->letterSpacing;
            drawTextRun (af->font, viewX, y, text + curr, next - curr);
            viewX += width;
            curr = next;
         }
      }
   }
}

/*
 * "Simple" wrapped text (image alt text): greedy word wrap into the given
 * box, ignoring letter spacing — mirrors what FLTK's boxed fl_draw does.
 */
void CoreGraphicsView::drawSimpleWrappedText (core::style::Font *font,
                                              core::style::Color *color,
                                              core::style::Color::Shading
                                              shading,
                                              int x, int y, int w, int h,
                                              const char *text)
{
   if (!cg)
      return;

   AppleFont *af = (AppleFont*) font;
   setFillColor (color, shading);

   int lineY = y + af->ascent;
   const char *p = text;

   while (*p && lineY - af->ascent < y + h) {
      /* take words until the line would overflow */
      const char *lineStart = p, *lastFit = NULL;

      while (*p) {
         const char *wordEnd = p;
         while (*wordEnd && *wordEnd != ' ')
            wordEnd++;
         double width =
            AppleFont::measureWidth (af->font, lineStart,
                                     wordEnd - lineStart);
         if (width > w && lastFit != NULL) {
            p = lastFit;
            break;
         }
         lastFit = wordEnd;
         p = (*wordEnd == ' ') ? wordEnd + 1 : wordEnd;
         if (width > w)
            break;   /* single over-wide word: draw it anyway */
      }

      const char *lineEnd = lastFit ? lastFit : p;
      drawTextRun (af->font, x, lineY, lineStart, lineEnd - lineStart);
      lineY += af->ascent + af->descent;
   }
}

void CoreGraphicsView::drawImage (core::Imgbuf *imgbuf, int xRoot, int yRoot,
                                  int x, int y, int width, int height)
{
   /* spike is text-only; images are the next milestone */
}

// ----------------------------------------------------------------------

core::View *CoreGraphicsView::getClippingView (int x, int y,
                                               int width, int height)
{
   /* Clipping views share this context; save/clip now, restore in
    * mergeClippingView. Nesting works as a GState stack, like FLTK's
    * fl_push_clip/fl_pop_clip pair. */
   if (cg) {
      CGContextSaveGState (cg);
      CGContextClipToRect (cg, CGRectMake (x, y, width, height));
   }
   return this;
}

void CoreGraphicsView::mergeClippingView (core::View *clippingView)
{
   if (cg)
      CGContextRestoreGState (cg);
}

} // namespace apple
} // namespace dw
