/*
 * Native macOS address-field adapter for Dillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <FL/Fl_Window.H>
#include <FL/x.H>

#include <string>

#include "macoslocation.hh"

struct MacLocation;

@interface DilloLocationTarget : NSObject
@property(nonatomic, assign) MacLocation *location;
- (void)activate:(id)sender;
@end

struct MacLocation {
   __strong NSTextField *field;
   __strong DilloLocationTarget *target;
   std::string value;
   void *callback_data;
   MacLocationActivateCb activate_cb;
};

@implementation DilloLocationTarget
- (void)activate:(id)sender
{
   (void)sender;
   if (self.location && self.location->activate_cb)
      self.location->activate_cb(self.location->callback_data);
}
@end

void *a_Mac_location_new(void *data, MacLocationActivateCb activate_cb)
{
   MacLocation *location = new MacLocation;

   location->field = [[NSTextField alloc] initWithFrame:NSZeroRect];
   location->target = [DilloLocationTarget new];
   location->target.location = location;
   location->callback_data = data;
   location->activate_cb = activate_cb;

   location->field.target = location->target;
   location->field.action = @selector(activate:);
   location->field.bezelStyle = NSTextFieldRoundedBezel;
   location->field.font = [NSFont systemFontOfSize:13.0];
   location->field.placeholderString = @"Enter website address";
   location->field.accessibilityLabel = @"Address";
   location->field.usesSingleLineMode = YES;
   location->field.cell.scrollable = YES;
   location->field.cell.wraps = NO;
   location->field.hidden = YES;
   return location;
}

void a_Mac_location_free(void *handle)
{
   MacLocation *location = static_cast<MacLocation *>(handle);
   if (!location)
      return;

   location->target.location = NULL;
   [location->field removeFromSuperview];
   delete location;
}

void a_Mac_location_sync(void *handle, void *fl_window,
                         int x, int y, int w, int h, int visible)
{
   MacLocation *location = static_cast<MacLocation *>(handle);
   Fl_Window *window = static_cast<Fl_Window *>(fl_window);

   if (!location || !window || !window->shown()) {
      if (location)
         location->field.hidden = YES;
      return;
   }

   NSWindow *nativeWindow = (NSWindow *)fl_xid(window);
   NSView *contentView = nativeWindow.contentView;
   if (!contentView) {
      location->field.hidden = YES;
      return;
   }

   if (location->field.superview != contentView)
      [contentView addSubview:location->field];

   const CGFloat inset = 2.0;
   CGFloat fieldWidth = w > 2 * inset ? w - 2 * inset : 0;
   CGFloat fieldHeight = h > 2 * inset ? h - 2 * inset : 0;
   CGFloat nativeY = NSHeight(contentView.bounds) - y - h + inset;
   location->field.frame = NSMakeRect(x + inset, nativeY,
                                      fieldWidth, fieldHeight);
   location->field.hidden = !visible || fieldWidth <= 0 || fieldHeight <= 0;

   if (location->field.hidden &&
       nativeWindow.firstResponder == location->field.currentEditor)
      [nativeWindow makeFirstResponder:nil];
}

const char *a_Mac_location_get(void *handle)
{
   MacLocation *location = static_cast<MacLocation *>(handle);
   if (!location)
      return "";

   const char *utf8 = location->field.stringValue.UTF8String;
   location->value = utf8 ? utf8 : "";
   return location->value.c_str();
}

void a_Mac_location_set(void *handle, const char *text)
{
   MacLocation *location = static_cast<MacLocation *>(handle);
   if (!location)
      return;

   NSString *value = text ? [NSString stringWithUTF8String:text] : nil;
   location->field.stringValue = value ?: @"";
}

void a_Mac_location_focus(void *handle)
{
   MacLocation *location = static_cast<MacLocation *>(handle);
   NSWindow *window;

   if (!location || location->field.hidden || !location->field.superview)
      return;
   window = location->field.window;
   [window makeFirstResponder:location->field];
   [location->field selectText:nil];
}
