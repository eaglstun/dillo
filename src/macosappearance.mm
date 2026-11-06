/*
 * macOS light/dark appearance for CSS prefers-color-scheme.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <FL/Fl.H>

#include "uicmd.hh"
#include "macosappearance.hh"

static BOOL appearanceIsDark(void)
{
   NSAppearanceName match = [NSApp.effectiveAppearance
      bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,
                                          NSAppearanceNameDarkAqua]];
   return [match isEqualToString:NSAppearanceNameDarkAqua];
}

/* NSApp.effectiveAppearance is key-value observable; it changes when the
 * user flips System Settings > Appearance, or at sunset with "Auto". */
@interface DilloAppearanceObserver : NSObject
@end

@implementation DilloAppearanceObserver
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context
{
   (void)keyPath; (void)object; (void)change; (void)context;
   /* KVO can fire on any thread; Dillo's UI and parser are main-thread. */
   dispatch_async(dispatch_get_main_queue(), ^{
      a_UIcmd_set_prefers_dark(appearanceIsDark());
      Fl::awake();   /* run the queued repush without waiting for input */
   });
}
@end

void a_Mac_appearance_init(void)
{
   static DilloAppearanceObserver *observer = nil;

   if (observer)
      return;
   [NSApplication sharedApplication];
   /* No pages yet, so this only records the starting preference. */
   a_UIcmd_set_prefers_dark(appearanceIsDark());

   observer = [DilloAppearanceObserver new];
   [NSApp addObserver:observer
           forKeyPath:@"effectiveAppearance"
              options:0
              context:NULL];
}
