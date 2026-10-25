/*
 * Native macOS window-tab adapter for Dillo's BrowserWindow lifecycle.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <FL/Fl_Window.H>
#include <FL/x.H>

#include "macosnative_tabs.hh"

static NSString *const dilloTabbingIdentifier =
   @"org.dillo-browser.browser-window";
static id tabKeyMonitor;
static NSMutableDictionary<NSValue *, NSValue *> *nativeWindowMap;

static void selectRelative(NSWindow *window, int direction)
{
   NSWindowTabGroup *group = window.tabGroup;

   if (!window || !group || group.windows.count < 2 || direction == 0)
      return;

   NSArray<NSWindow *> *windows = group.windows;
   NSWindow *selected = group.selectedWindow ?: window;
   NSUInteger index = [windows indexOfObjectIdenticalTo:selected];
   if (index == NSNotFound)
      return;

   NSUInteger next;
   if (direction < 0)
      next = index > 0 ? index - 1 : windows.count - 1;
   else
      next = index + 1 < windows.count ? index + 1 : 0;
   selected = windows[next];
   group.selectedWindow = selected;
   [selected makeKeyAndOrderFront:nil];
}

static void installTabKeyMonitor(void)
{
   if (tabKeyMonitor)
      return;

   tabKeyMonitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
      handler:^NSEvent *(NSEvent *event) {
         NSWindow *window = NSApp.keyWindow ?: NSApp.mainWindow;
         if (window.tabGroup.selectedWindow)
            window = window.tabGroup.selectedWindow;
         NSEventModifierFlags modifiers = event.modifierFlags &
            NSEventModifierFlagDeviceIndependentFlagsMask;
         if (!window)
            return event;

         if (!(modifiers & NSEventModifierFlagControl))
            return event;

         if (event.keyCode == 48 || event.keyCode == 116 ||
             event.keyCode == 121) {
            BOOL previous = event.keyCode == 116 ||
               (event.keyCode == 48 &&
                (modifiers & NSEventModifierFlagShift));
            selectRelative(window, previous ? -1 : 1);
            return (NSEvent *)nil;
         }
         return event;
      }];
}

static NSWindow *nativeWindow(void *fl_window)
{
   Fl_Window *window = static_cast<Fl_Window *>(fl_window);

   if (!window || !window->shown())
      return nil;
   NSValue *mapped = nativeWindowMap[[NSValue valueWithPointer:fl_window]];
   return mapped ? (NSWindow *)mapped.pointerValue : (NSWindow *)fl_xid(window);
}

void a_Mac_tabs_configure(void *fl_window)
{
   NSWindow *window = nativeWindow(fl_window);

   if (!window)
      return;
   if (!nativeWindowMap)
      nativeWindowMap = [NSMutableDictionary dictionary];
   nativeWindowMap[[NSValue valueWithPointer:fl_window]] =
      [NSValue valueWithPointer:(__bridge void *)window];
   installTabKeyMonitor();
   window.tabbingMode = NSWindowTabbingModeAutomatic;
   window.tabbingIdentifier = dilloTabbingIdentifier;
}

void a_Mac_tabs_add(void *source_fl_window, void *new_fl_window, int focus)
{
   NSWindow *source = nativeWindow(source_fl_window);
   NSWindow *newWindow = nativeWindow(new_fl_window);
   NSWindow *previousSelection;

   if (!source || !newWindow || source == newWindow)
      return;

   a_Mac_tabs_configure(source_fl_window);
   a_Mac_tabs_configure(new_fl_window);
   previousSelection = source.tabGroup.selectedWindow ?: source;
   [source addTabbedWindow:newWindow ordered:NSWindowAbove];

   NSWindowTabGroup *group = source.tabGroup;
   if (group)
      group.selectedWindow = focus ? newWindow : previousSelection;
   if (focus)
      [newWindow makeKeyAndOrderFront:nil];
}

void a_Mac_tabs_remove(void *fl_window)
{
   NSWindow *window = nativeWindow(fl_window);
   NSWindowTabGroup *group = window.tabGroup;

   if (!window)
      return;

   if (group && group.windows.count > 1) {
      NSArray<NSWindow *> *windows = group.windows;
      NSUInteger index = [windows indexOfObjectIdenticalTo:window];
      NSWindow *replacement = nil;
      if (index != NSNotFound) {
         NSUInteger replacementIndex = index > 0 ? index - 1 : index + 1;
         if (replacementIndex < windows.count)
            replacement = windows[replacementIndex];
      }

      [group removeWindow:window];
      if (replacement) {
         replacement.tabGroup.selectedWindow = replacement;
         [replacement makeKeyAndOrderFront:nil];
      }
   }
   [nativeWindowMap removeObjectForKey:[NSValue valueWithPointer:fl_window]];
}

void a_Mac_tabs_select_relative(void *fl_window, int direction)
{
   NSWindow *window = nativeWindow(fl_window);
   selectRelative(window, direction);
}

void a_Mac_tabs_select_index(void *fl_window, int index)
{
   NSWindow *window = nativeWindow(fl_window);
   NSWindowTabGroup *group = window.tabGroup;

   if (!window || index < 0)
      return;
   if (!group) {
      if (index == 0)
         [window makeKeyAndOrderFront:nil];
      return;
   }

   NSArray<NSWindow *> *windows = group.windows;
   if ((NSUInteger)index < windows.count) {
      NSWindow *selected = windows[(NSUInteger)index];
      group.selectedWindow = selected;
      [selected makeKeyAndOrderFront:nil];
   }
}

void a_Mac_tabs_set_title(void *fl_window, const char *title)
{
   NSWindow *window = nativeWindow(fl_window);
   NSString *nativeTitle;

   if (!window)
      return;
   nativeTitle = title ? [NSString stringWithUTF8String:title] : @"";
   window.tab.title = nativeTitle ?: @"";
}

int a_Mac_tabs_close_selected(void)
{
   NSWindow *front = NSApp.keyWindow ?: NSApp.mainWindow;
   NSWindowTabGroup *group = front.tabGroup;

   if (!front || !group || group.windows.count < 2 || !group.selectedWindow)
      return 0;
   [group.selectedWindow performClose:nil];
   return 1;
}

int a_Mac_tabs_is_front(void *fl_window)
{
   NSWindow *window = nativeWindow(fl_window);
   NSWindow *front = NSApp.keyWindow ?: NSApp.mainWindow;

   if (!window || !front)
      return 0;

   if (front.tabGroup.selectedWindow)
      front = front.tabGroup.selectedWindow;
   return window == front;
}

int a_Mac_tabs_is_selected(void *fl_window)
{
   NSWindow *window = nativeWindow(fl_window);
   NSWindowTabGroup *group = window.tabGroup;

   return window && (!group || group.selectedWindow == window);
}
