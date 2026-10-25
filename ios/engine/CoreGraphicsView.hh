/*
 * Dillo Widget — Core Graphics view implementation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_APPLE_COREGRAPHICS_VIEW_HH__
#define __DW_APPLE_COREGRAPHICS_VIEW_HH__

#include <CoreGraphics/CoreGraphics.h>

#include "dw/core.hh"

namespace dw {
namespace apple {

/**
 * \brief A dw::core::View that paints into a CGContextRef.
 *
 * Reports usesViewport() so the engine's scrolling machinery (anchors,
 * find-in-page, scroll-into-view) is active; the actual scrolling is done
 * by the shell's UIScrollView, driven through the scrollTo hook, and the
 * shell reports user scrolling back via Layout::scrollPosChanged. Canvas
 * coordinates equal context coordinates (the shell translates the CTM for
 * the visible slice). The context is only valid during a paint pass; the
 * app shell sets it (setCGContext) around Layout::expose and clears it
 * afterwards. The context is expected in UIKit orientation (origin
 * top-left, y down), as handed to -[UIView drawRect:].
 */
class CoreGraphicsView: public core::View
{
private:
   core::Layout *layout;
   CGContextRef cg;

   int canvasWidth, canvasAscent, canvasDescent;
   int bgColor;   /* 0xrrggbb, set via setBgColor */

   /* App-shell hook, called on queueDraw* (→ -[UIView setNeedsDisplay]). */
   void (*invalidateFunc) (void *data);
   void *invalidateData;

   /* App-shell hook for engine-driven scrolling (anchors, find-in-page,
    * scroll-into-view): the shell moves its UIScrollView. */
   void (*scrollToFunc) (void *data, int x, int y);
   void *scrollToData;

   void setStrokeColor (core::style::Color *color,
                        core::style::Color::Shading shading);
   void setFillColor (core::style::Color *color,
                      core::style::Color::Shading shading);

   void drawTextRun (CTFontRef font, double x, double y,
                     const char *text, int len);

public:
   CoreGraphicsView ();
   ~CoreGraphicsView ();

   void setCGContext (CGContextRef cg);
   void setInvalidateFunc (void (*func) (void *data), void *data);
   void setScrollToFunc (void (*func) (void *data, int x, int y),
                         void *data);

   int getCanvasWidth () { return canvasWidth; }
   int getCanvasHeight () { return canvasAscent + canvasDescent; }

   /**
    * \brief Fill the given canvas rectangle with the view background color.
    *
    * The app shell calls this before Layout::expose; it plays the role of
    * FltkViewBase::draw's initial fl_rectf with the background color.
    */
   void clearBackground (int x, int y, int width, int height);

   /* dw::core::View */

   void setLayout (core::Layout *layout);
   void setCanvasSize (int width, int ascent, int descent);
   void setCursor (core::style::Cursor cursor);
   void setBgColor (core::style::Color *color);

   bool usesViewport ();
   int getHScrollbarThickness ();
   int getVScrollbarThickness ();
   int getScrollbarOnLeft ();
   void scrollTo (int x, int y);
   void setViewportSize (int width, int height,
                         int hScrollbarThickness, int vScrollbarThickness);

   void startDrawing (core::Rectangle *area);
   void finishDrawing (core::Rectangle *area);
   void queueDraw (core::Rectangle *area);
   void queueDrawTotal ();
   void cancelQueueDraw ();

   void drawPoint (core::style::Color *color,
                   core::style::Color::Shading shading,
                   int x, int y);
   void drawLine (core::style::Color *color,
                  core::style::Color::Shading shading,
                  int x1, int y1, int x2, int y2);
   void drawTypedLine (core::style::Color *color,
                       core::style::Color::Shading shading,
                       core::style::LineType type, int width,
                       int x1, int y1, int x2, int y2);
   void drawRectangle (core::style::Color *color,
                       core::style::Color::Shading shading, bool filled,
                       int x, int y, int width, int height);
   void drawArc (core::style::Color *color,
                 core::style::Color::Shading shading, bool filled,
                 int centerX, int centerY, int width, int height,
                 int angle1, int angle2);
   void drawPolygon (core::style::Color *color,
                     core::style::Color::Shading shading,
                     bool filled, bool convex,
                     core::Point *points, int npoints);
   void drawText (core::style::Font *font,
                  core::style::Color *color,
                  core::style::Color::Shading shading,
                  int x, int y, const char *text, int len);
   void drawSimpleWrappedText (core::style::Font *font,
                               core::style::Color *color,
                               core::style::Color::Shading shading,
                               int x, int y, int w, int h,
                               const char *text);
   void drawImage (core::Imgbuf *imgbuf, int xRoot, int yRoot,
                   int x, int y, int width, int height);

   core::View *getClippingView (int x, int y, int width, int height);
   void mergeClippingView (core::View *clippingView);
};

} // namespace apple
} // namespace dw

#endif // __DW_APPLE_COREGRAPHICS_VIEW_HH__
