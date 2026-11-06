/*
 * Dillo Widget
 *
 * Copyright 2005-2007 Sebastian Geerken <sgeerken@dillo.org>
 * Copyright 2024 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DW_FLTKVIEWPORT_HH__
#define __DW_FLTKVIEWPORT_HH__

#include <FL/Fl_Group.H>
#include <FL/Fl_Scrollbar.H>

#include "core.hh"
#include "fltkcore.hh"
#include "fltkviewbase.hh"

class Fl_Window;

namespace dw {
namespace fltk {

/**
 * \brief Optional platform scrollbars drawn by the toolkit instead of FLTK.
 *
 * Installed once with FltkViewport::setNativeScrollbarHooks() before any
 * viewport is created. When present, the FLTK scrollbars stay hidden and
 * only serve as storage for the line size. The platform side owns one
 * scrolling container per viewport, laid over it in window coordinates,
 * that mirrors the canvas size and scroll position and reports back when
 * the user moves its scrollbars.
 */
struct FltkNativeScrollbarHooks {
   /** The user scrolled with the platform scrollbars to canvas (x, y). */
   typedef void (*ScrolledCb) (void *viewport, int x, int y);

   void *(*create) (void *viewport, ScrolledCb scrolled);
   void (*destroy) (void *handle);
   /** (x, y, w, h) is the whole viewport in window coordinates.
    *  flash reveals overlay scrollers; dark styles them for a dark page. */
   void (*sync) (void *handle, Fl_Window *window, int x, int y, int w, int h,
                 int visible, int canvasWidth, int canvasHeight,
                 int scrollX, int scrollY, int showH, int showV,
                 int onLeft, int flash, int dark);
   /** Space to reserve beside the page; 0 for overlay scrollers. */
   int (*thickness) ();
};

class FltkViewport: public FltkWidgetView
{
public:
   enum GadgetOrientation { GADGET_VERTICAL, GADGET_HORIZONTAL };

private:
   enum { SCROLLBAR_THICKNESS = 15 };

   static const FltkNativeScrollbarHooks *nativeHooks;
   static FltkViewport *nativeViewports;
   FltkViewport *nativePrev, *nativeNext;
   void *nativeScroller;

   /* Space reserved for each scrollbar, and whether each one is logically
    * shown. With overlay scrollers the thickness is 0 while a scrollbar is
    * still shown, so the two cannot be derived from each other. */
   int sbThickness;
   bool hsVisible, vsVisible;
   bool darkBg;   /* page background is dark: match the scroller style */
   Fl_Color gutterColor;   /* page background, painted under native bars */

   int scrollX, scrollY;
   int scrollDX, scrollDY;
   int scrollbarOnLeft;
   int hasDragScroll, dragScrolling, dragX, dragY;
   int horScrolling, verScrolling;
   bool scrollbarPageMode;
   int pageOverlap;
   enum dw::core::ScrollCommand pageScrolling;
   float pageScrollInterval;
   float pageScrollDelay;

   Fl_Scrollbar *vscrollbar, *hscrollbar;

   GadgetOrientation gadgetOrientation[4];
   lout::container::typed::List <lout::object::TypedPointer < Fl_Widget> >
      *gadgets;

   void adjustScrollbarsAndGadgetsAllocation ();
   void adjustScrollbarValues ();
   void hscrollbarChanged ();
   void vscrollbarChanged ();
   void positionChanged ();

   static void hscrollbarCallback (Fl_Widget *hscrollbar, void *viewportPtr);
   static void vscrollbarCallback (Fl_Widget *vscrollbar, void *viewportPtr);

   void selectionScroll();
   static void selectionScroll(void *vport);

   void repeatPageScroll ();
   static void repeatPageScroll (void *data);

   void updateCanvasWidgets (int oldScrollX, int oldScrollY);
   static void draw_area (void *data, int x, int y, int w, int h);

   void syncNativeScrollbars (bool flash);
   static void nativeScrolledCb (void *viewport, int x, int y);

protected:
   int translateViewXToCanvasX (int x);
   int translateViewYToCanvasY (int y);
   int translateCanvasXToViewX (int x);
   int translateCanvasYToViewY (int y);

public:
   FltkViewport (int x, int y, int w, int h, const char *label = 0);
   ~FltkViewport ();

   static void setNativeScrollbarHooks (const FltkNativeScrollbarHooks *hooks);
   static void nativeScrollbarStyleChanged ();

   void resize(int x, int y, int w, int h);
   void draw ();
   int handle (int event);
   void show ();
   void hide ();

   void setCanvasSize (int width, int ascent, int descent);
   void setBgColor (core::style::Color *color);

   bool usesViewport ();
   int getHScrollbarThickness ();
   int getVScrollbarThickness ();
   int getScrollbarOnLeft () {return scrollbarOnLeft; };
   void scroll(int dx, int dy);
   void scroll(dw::core::ScrollCommand cmd);
   void scrollTo (int x, int y);
   void setViewportSize (int width, int height,
                         int hScrollbarThickness, int vScrollbarThickness);
   void setScrollStep(int step);

   void setGadgetOrientation (bool hscrollbarVisible, bool vscrollbarVisible,
                              GadgetOrientation gadgetOrientation);
   void setDragScroll (bool enable) { hasDragScroll = enable ? 1 : 0; }
   void addGadget (Fl_Widget *gadget);
   void setScrollbarOnLeft (bool enable);
   void setScrollbarPageMode(bool enable);
   void setPageOverlap(int overlap);
};

} // namespace fltk
} // namespace dw

#endif // __DW_FLTKVIEWPORT_HH__

