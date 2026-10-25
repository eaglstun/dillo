/*
 * Native macOS web-search dialog adapter for Dillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <string>

#include "macossearchdialog.hh"

const char *a_Mac_search_dialog(const char *title, const char *prompt,
                                const char *const *engineLabels,
                                int engineCount, int selectedEngine,
                                int *chosenEngine)
{
   static std::string query;
   NSAlert *alert = [NSAlert new];
   NSView *accessory = [[NSView alloc]
      initWithFrame:NSMakeRect(0, 0, 360, 64)];
   NSTextField *field = [[NSTextField alloc]
      initWithFrame:NSMakeRect(0, 36, 360, 24)];
   NSPopUpButton *engines = [[NSPopUpButton alloc]
      initWithFrame:NSMakeRect(0, 0, 360, 26) pullsDown:NO];

   query.clear ();

   NSString *nativeTitle = title ? [NSString stringWithUTF8String:title] : nil;
   NSString *nativePrompt =
      prompt ? [NSString stringWithUTF8String:prompt] : nil;
   if ([nativePrompt hasSuffix:@":"])
      nativePrompt = [nativePrompt substringToIndex:nativePrompt.length - 1];
   alert.window.title = nativeTitle ?: @"Dillo";
   alert.messageText = nativePrompt ?: @"Search the Web";
   alert.informativeText = @"Enter search terms and choose a search engine.";
   [alert addButtonWithTitle:@"Search"];
   [alert addButtonWithTitle:@"Cancel"];

   field.placeholderString = @"Search terms";
   field.accessibilityLabel = @"Search terms";
   [accessory addSubview:field];

   for (int i = 0; i < engineCount; i++) {
      NSString *label = engineLabels[i] ?
         [NSString stringWithUTF8String:engineLabels[i]] : nil;
      if (label)
         [engines addItemWithTitle:label];
   }
   if (selectedEngine >= 0 && selectedEngine < engines.numberOfItems)
      [engines selectItemAtIndex:selectedEngine];
   engines.accessibilityLabel = @"Search engine";
   engines.enabled = engines.numberOfItems > 1;
   [accessory addSubview:engines];

   alert.accessoryView = accessory;
   alert.window.initialFirstResponder = field;

   if ([alert runModal] != NSAlertFirstButtonReturn)
      return NULL;

   const char *utf8 = field.stringValue.UTF8String;
   if (!utf8)
      return NULL;

   query = utf8;
   if (chosenEngine)
      *chosenEngine = (int)engines.indexOfSelectedItem;
   return query.c_str ();
}
