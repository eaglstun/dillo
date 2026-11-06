/*
 * Native macOS scrollbars for Dillo page viewports.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

/*
 * Each FltkViewport gets a transparent NSScrollView laid over it as a
 * subview of the window's content view, the same way macoslocation.mm
 * overlays the address field. Its document view is an empty, page-sized
 * placeholder; FLTK still draws the page underneath.
 *
 * Why a whole NSScrollView rather than bare NSScrollers: overlay scroller
 * knobs are faded in and out by the scroll view that owns them. A
 * standalone NSScroller in overlay style never becomes visible at all.
 * With a real scroll view, AppKit handles fading, flashing, hover
 * expansion, track clicks, and the System Settings scroll bar preference.
 *
 * The scroll view only accepts mouse events over its scrollers; everywhere
 * else hit testing falls through to FLTK, which keeps doing wheel
 * scrolling, selection, and links. Positions flow both ways:
 *   FLTK scrolled  -> sync() moves the clip view to match;
 *   user drags bar -> clip view bounds change -> ScrolledCb -> FLTK.
 *
 * dw/ stays free of AppKit: it only sees the hook table at the bottom.
 */

#import <AppKit/AppKit.h>

#include <math.h>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include "../dw/fltkviewport.hh"
#include "macosnative_tabs.hh"
#include "macosscrollers.hh"

using dw::fltk::FltkNativeScrollbarHooks;
using dw::fltk::FltkViewport;

/* Stands in for the page so the scroll view knows the canvas size. */
@interface DilloScrollDocument : NSView
@end

@implementation DilloScrollDocument
- (BOOL)isFlipped
{
   return YES;   /* canvas y grows downward, like FLTK's scrollY */
}

- (NSView *)hitTest:(NSPoint)point
{
   (void)point;
   return nil;
}
@end

@interface DilloScrollView : NSScrollView
@property(nonatomic, assign) void *viewport;
@property(nonatomic, assign) FltkNativeScrollbarHooks::ScrolledCb scrolled;
@property(nonatomic, assign) BOOL syncing;
@end

@implementation DilloScrollView

/* Only the scrollers are ours; the page underneath belongs to FLTK. */
- (NSView *)hitTest:(NSPoint)point
{
   NSView *hit = [super hitTest:point];
   NSScroller *v = self.hasVerticalScroller ? self.verticalScroller : nil;
   NSScroller *h = self.hasHorizontalScroller ? self.horizontalScroller : nil;

   if (hit && ((v && [hit isDescendantOf:v]) || (h && [hit isDescendantOf:h])))
      return hit;
   return nil;
}

- (void)clipViewMoved:(NSNotification *)note
{
   (void)note;
   if (self.syncing || !self.viewport || !self.scrolled)
      return;

   NSPoint origin = self.contentView.bounds.origin;
   self.scrolled(self.viewport, (int)lround(origin.x), (int)lround(origin.y));
   /* Knob tracking runs AppKit's own event loop; without this FLTK would
    * not repaint the page until the mouse is released. */
   Fl::flush();
}

@end

static void *scroller_create(void *viewport,
                             FltkNativeScrollbarHooks::ScrolledCb scrolled)
{
   DilloScrollView *scrollView =
      [[DilloScrollView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];

   scrollView.viewport = viewport;
   scrollView.scrolled = scrolled;
   scrollView.drawsBackground = NO;
   scrollView.contentView.drawsBackground = NO;
   scrollView.borderType = NSNoBorder;
   scrollView.autohidesScrollers = NO;
   /* FLTK does the actual scrolling; a rubber-banding clip view would
    * report positions the page can never reach. */
   scrollView.verticalScrollElasticity = NSScrollElasticityNone;
   scrollView.horizontalScrollElasticity = NSScrollElasticityNone;
   scrollView.documentView =
      [[DilloScrollDocument alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
   scrollView.hidden = YES;

   scrollView.contentView.postsBoundsChangedNotifications = YES;
   [[NSNotificationCenter defaultCenter]
      addObserver:scrollView
         selector:@selector(clipViewMoved:)
             name:NSViewBoundsDidChangeNotification
           object:scrollView.contentView];

   return (void *)CFBridgingRetain(scrollView);
}

static void scroller_destroy(void *handle)
{
   if (!handle)
      return;
   DilloScrollView *scrollView = (DilloScrollView *)CFBridgingRelease(handle);
   [[NSNotificationCenter defaultCenter] removeObserver:scrollView];
   scrollView.viewport = NULL;
   scrollView.scrolled = NULL;
   [scrollView removeFromSuperview];
}

static void scroller_sync(void *handle, Fl_Window *window,
                          int x, int y, int w, int h, int visible,
                          int canvasWidth, int canvasHeight,
                          int scrollX, int scrollY, int showH, int showV,
                          int onLeft, int flash, int dark)
{
   DilloScrollView *scrollView = (__bridge DilloScrollView *)handle;
   NSWindow *nativeWindow;
   NSView *contentView;

   if (!scrollView)
      return;
   nativeWindow = (visible && window && window->shown())
      ? (__bridge NSWindow *)a_Mac_tabs_native_window(window) : nil;
   contentView = nativeWindow.contentView;
   if (!contentView || w <= 0 || h <= 0 || !(showH || showV)) {
      scrollView.hidden = YES;
      return;
   }

   scrollView.syncing = YES;

   if (scrollView.superview != contentView)
      [contentView addSubview:scrollView];

   /* Follow the page, not the window: a light page in a dark-mode window
    * still gets the light scroller style. */
   NSAppearanceName appearanceName = dark ? NSAppearanceNameDarkAqua
                                          : NSAppearanceNameAqua;
   if (![scrollView.appearance.name isEqualToString:appearanceName])
      scrollView.appearance = [NSAppearance appearanceNamed:appearanceName];

   /* The vertical scroller goes on the left in a right-to-left layout. */
   NSUserInterfaceLayoutDirection direction = onLeft
      ? NSUserInterfaceLayoutDirectionRightToLeft
      : NSUserInterfaceLayoutDirectionLeftToRight;
   if (scrollView.userInterfaceLayoutDirection != direction)
      scrollView.userInterfaceLayoutDirection = direction;

   if (scrollView.hasVerticalScroller != (BOOL)showV)
      scrollView.hasVerticalScroller = showV;
   if (scrollView.hasHorizontalScroller != (BOOL)showH)
      scrollView.hasHorizontalScroller = showH;

   NSRect frame = NSMakeRect(x, NSHeight(contentView.bounds) - y - h, w, h);
   if (!NSEqualRects(scrollView.frame, frame))
      scrollView.frame = frame;

   NSSize docSize = NSMakeSize(canvasWidth, canvasHeight);
   NSView *document = scrollView.documentView;
   if (!NSEqualSizes(document.frame.size, docSize))
      [document setFrameSize:docSize];

   /* AppKit animates track clicks through fractional origins, and FLTK
    * only keeps whole pixels. Treat sub-pixel differences as in sync, or
    * the echo of our own rounding cancels the animation mid-page. */
   NSClipView *clip = scrollView.contentView;
   NSPoint origin = NSMakePoint(scrollX, scrollY);
   NSPoint current = clip.bounds.origin;
   if (fabs(current.x - origin.x) >= 1.0 || fabs(current.y - origin.y) >= 1.0) {
      [clip scrollToPoint:[clip constrainBoundsRect:
         (NSRect){origin, clip.bounds.size}].origin];
      [scrollView reflectScrolledClipView:clip];
   }

   scrollView.hidden = NO;
   scrollView.syncing = NO;

   if (flash)
      [scrollView flashScrollers];
}

static int scroller_thickness()
{
   NSScrollerStyle style = [NSScroller preferredScrollerStyle];

   if (style == NSScrollerStyleOverlay)
      return 0;
   return (int)[NSScroller scrollerWidthForControlSize:NSControlSizeRegular
                                         scrollerStyle:style];
}

static const FltkNativeScrollbarHooks scrollerHooks = {
   scroller_create,
   scroller_destroy,
   scroller_sync,
   scroller_thickness,
};

void a_Mac_scrollers_init(void)
{
   static BOOL installed = NO;

   if (installed)
      return;
   installed = YES;
   FltkViewport::setNativeScrollbarHooks(&scrollerHooks);

   /* System Settings > Appearance > Show scroll bars, or plugging in a
    * mouse while set to "Automatically". NSScrollView restyles itself; the
    * viewports have to re-reserve space for the new thickness. */
   [[NSNotificationCenter defaultCenter]
      addObserverForName:NSPreferredScrollerStyleDidChangeNotification
                  object:nil
                   queue:[NSOperationQueue mainQueue]
              usingBlock:^(NSNotification *note) {
                 (void)note;
                 FltkViewport::nativeScrollbarStyleChanged();
                 Fl::awake();
              }];
}
