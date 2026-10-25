/*
 * Native macOS automation URL adapter for Dillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <AppKit/AppKit.h>

#include <string>

#include "macosautomation.hh"
#include "uicmd.hh"

static bool automationReady;
static std::string pendingURL;

/**
 * Return the target carried by either of these forms:
 *
 *   dillo:https://example.com/path
 *   dillo://open?url=https%3A%2F%2Fexample.com%2Fpath
 */
static NSString *automationTarget(NSString *eventURL)
{
   NSURLComponents *components =
      [NSURLComponents componentsWithString:eventURL];
   if (!components ||
       [components.scheme caseInsensitiveCompare:@"dillo"] != NSOrderedSame)
      return nil;

   NSString *target = nil;
   if (components.host &&
       [components.host caseInsensitiveCompare:@"open"] == NSOrderedSame) {
      for (NSURLQueryItem *item in components.queryItems) {
         if ([item.name caseInsensitiveCompare:@"url"] == NSOrderedSame) {
            target = item.value;
            break;
         }
      }
   } else {
      NSRange colon = [eventURL rangeOfString:@":"];
      if (colon.location != NSNotFound)
         target = [eventURL substringFromIndex:colon.location + 1];
   }

   NSURL *url = target.length ? [NSURL URLWithString:target] : nil;
   if (!url.scheme.length ||
       [url.scheme caseInsensitiveCompare:@"dillo"] == NSOrderedSame)
      return nil;
   return target;
}

static void openAutomationTarget(NSString *target)
{
   const char *utf8 = target.UTF8String;
   if (!utf8)
      return;

   if (!automationReady) {
      pendingURL = utf8;
      return;
   }

   BrowserWindow *bw = a_UIcmd_get_active_bw();
   if (bw)
      a_UIcmd_open_urlstr(bw, utf8);
}

@interface DilloAutomationHandler : NSObject
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)replyEvent;
@end

@implementation DilloAutomationHandler
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
   (void)replyEvent;
   NSAppleEventDescriptor *parameter =
      [event paramDescriptorForKeyword:keyDirectObject];
   NSString *target = automationTarget(parameter.stringValue);
   if (target)
      openAutomationTarget(target);
}
@end

static DilloAutomationHandler *automationHandler;

void a_Mac_automation_init(void)
{
   if (automationHandler)
      return;

   automationHandler = [DilloAutomationHandler new];
   [[NSAppleEventManager sharedAppleEventManager]
      setEventHandler:automationHandler
           andSelector:@selector(handleGetURLEvent:withReplyEvent:)
         forEventClass:kInternetEventClass
            andEventID:kAEGetURL];
}

void a_Mac_automation_ready(void)
{
   automationReady = true;
   if (!pendingURL.empty ()) {
      BrowserWindow *bw = a_UIcmd_get_active_bw();
      if (bw)
         a_UIcmd_open_urlstr(bw, pendingURL.c_str ());
      pendingURL.clear ();
   }
}
