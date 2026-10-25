/*
 * Native macOS save-panel adapter for Dillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <string>

#include "macosfiledialog.hh"

const char *a_Mac_save_file_dialog(const char *title, const char *pattern,
                                   const char *fname)
{
   static std::string chosenPath;
   NSSavePanel *panel = [NSSavePanel savePanel];

   chosenPath.clear ();
   (void)pattern;  // FLTK glob patterns do not map to macOS content types.

   if (title && *title) {
      NSString *nativeTitle = [NSString stringWithUTF8String:title];
      if (nativeTitle)
         panel.title = nativeTitle;
   }

   panel.canCreateDirectories = YES;
   panel.extensionHidden = NO;

   if (fname && *fname) {
      NSString *suggestedPath = [NSString stringWithUTF8String:fname];
      if (suggestedPath) {
         if (![suggestedPath isAbsolutePath]) {
            suggestedPath = [[[NSFileManager defaultManager]
               currentDirectoryPath] stringByAppendingPathComponent:suggestedPath];
         }

         if ([suggestedPath hasSuffix:@"/"]) {
            panel.directoryURL = [NSURL fileURLWithPath:suggestedPath
                                            isDirectory:YES];
         } else {
            NSString *directory = [suggestedPath stringByDeletingLastPathComponent];
            NSString *filename = [suggestedPath lastPathComponent];
            if (directory.length)
               panel.directoryURL = [NSURL fileURLWithPath:directory
                                               isDirectory:YES];
            if (filename.length)
               panel.nameFieldStringValue = filename;
         }
      }
   }

   if ([panel runModal] != NSModalResponseOK)
      return NULL;

   const char *path = panel.URL.fileSystemRepresentation;
   if (!path)
      return NULL;

   chosenPath = path;
   return chosenPath.c_str ();
}
