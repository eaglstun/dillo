/*
 * Native macOS context-menu adapter for Dillo's FLTK menu descriptions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Window.H>

#include <string>

#include "macoscontextmenu.hh"
#include "macosnative_tabs.hh"

@interface DilloContextMenuTarget : NSObject
@property(nonatomic, assign) const Fl_Menu_Item *selectedItem;
- (void)chooseMenuItem:(NSMenuItem *)sender;
@end

@implementation DilloContextMenuTarget
- (void)chooseMenuItem:(NSMenuItem *)sender
{
   self.selectedItem =
      (const Fl_Menu_Item *)[(NSValue *)sender.representedObject pointerValue];
}
@end

/* Strip FLTK's '&' mnemonic markup. A doubled ampersand is literal. */
static NSString *nativeTitle(const char *label)
{
   std::string title;

   for (const char *p = label; *p; p++) {
      if (*p == '&' && p[1] != '&')
         continue;
      title += *p;
      if (*p == '&' && p[1] == '&')
         p++;
   }

   return [NSString stringWithUTF8String:title.c_str ()];
}

static void addMenuItems(NSMenu *nativeMenu, const Fl_Menu_Item *items,
                         DilloContextMenuTarget *target)
{
   for (const Fl_Menu_Item *item = items; item && item->label ();
        item = item->next ()) {
      if (!item->visible ())
         continue;

      NSMenuItem *nativeItem = [[NSMenuItem alloc]
         initWithTitle:nativeTitle (item->label ())
                action:@selector(chooseMenuItem:)
         keyEquivalent:@""];
      nativeItem.target = target;
      nativeItem.representedObject = [NSValue valueWithPointer:item];
      nativeItem.enabled = item->active ();

      if (item->checkbox () || item->radio ())
         nativeItem.state = item->value () ? NSControlStateValueOn :
                                             NSControlStateValueOff;

      if (item->submenu ()) {
         const Fl_Menu_Item *children =
            (item->flags & FL_SUBMENU_POINTER) ?
               (const Fl_Menu_Item *)item->user_data () : item + 1;
         NSMenu *submenu = [[NSMenu alloc]
            initWithTitle:nativeItem.title];
         submenu.autoenablesItems = NO;
         addMenuItems (submenu, children, target);
         nativeItem.submenu = submenu;
      }

      [nativeMenu addItem:nativeItem];
      if (item->flags & FL_MENU_DIVIDER)
         [nativeMenu addItem:[NSMenuItem separatorItem]];
   }
}

/*
 * Build the native menu, show it at location in view (screen coordinates
 * when view is nil), and return the chosen original item.
 */
static const Fl_Menu_Item *popupAt(const Fl_Menu_Item *menu, NSPoint location,
                                   NSView *view)
{
   if (!menu)
      return NULL;

   DilloContextMenuTarget *target = [DilloContextMenuTarget new];
   NSMenu *nativeMenu = [[NSMenu alloc] initWithTitle:@""];
   nativeMenu.autoenablesItems = NO;
   addMenuItems (nativeMenu, menu, target);

   /* Dillo deliberately opens its popup from a zero-delay timeout, after
    * the originating mouse event has unwound. This API needs no retained
    * NSEvent. */
   [nativeMenu popUpMenuPositioningItem:nil atLocation:location inView:view];
   return target.selectedItem;
}

const Fl_Menu_Item *a_Mac_context_menu_popup(const Fl_Menu_Item *menu)
{
   return popupAt (menu, [NSEvent mouseLocation], nil);
}

const Fl_Menu_Item *a_Mac_context_menu_popup_at(const Fl_Menu_Item *menu,
                                                void *fl_window, int x, int y)
{
   Fl_Window *window = static_cast<Fl_Window *>(fl_window);
   NSWindow *nativeWindow = (window && window->shown ())
      ? (__bridge NSWindow *)a_Mac_tabs_native_window (window) : nil;
   NSView *contentView = nativeWindow.contentView;

   if (!contentView)
      return a_Mac_context_menu_popup (menu);

   /* FLTK window coordinates grow downward; the content view's upward. */
   NSPoint location = NSMakePoint (x, NSHeight (contentView.bounds) - y);
   return popupAt (menu, location, contentView);
}
