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

#include <string>

#include "macoscontextmenu.hh"

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

const Fl_Menu_Item *a_Mac_context_menu_popup(const Fl_Menu_Item *menu)
{
   if (!menu)
      return NULL;

   DilloContextMenuTarget *target = [DilloContextMenuTarget new];
   NSMenu *nativeMenu = [[NSMenu alloc] initWithTitle:@""];
   nativeMenu.autoenablesItems = NO;
   addMenuItems (nativeMenu, menu, target);

   /* Dillo deliberately opens its popup from a zero-delay timeout, after
    * the originating mouse event has unwound. This API needs no retained
    * NSEvent and accepts screen coordinates when the view is nil. */
   [nativeMenu popUpMenuPositioningItem:nil
                             atLocation:[NSEvent mouseLocation]
                                 inView:nil];
   return target.selectedItem;
}
