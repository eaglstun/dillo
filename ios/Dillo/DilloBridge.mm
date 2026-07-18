/*
 * Dillo iOS — Objective-C++ bridge around the dw engine + parser.
 *
 * One DilloBridge is one browser window: it owns the Layout/Platform/View
 * for the app's lifetime and creates a fresh DilloHtml per page load
 * (Layout::setWidget frees the previous page's widget tree, which in turn
 * frees the previous DilloHtml via its delete callback — the same
 * lifecycle the desktop uses).
 *
 * Compiled with -fno-exceptions/-fno-rtti to match the engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import "DilloBridge.h"

#include <stdio.h>
#include <string.h>

#include "dw/core.hh"
#include "dw/textblock.hh"

#include "src/prefs.h"
#include "src/bw.h"
#include "src/web.hh"
#include "src/html_common.hh"
#include "src/styleengine.hh"

#include "AppleCoreTextPlatform.hh"
#include "CoreGraphicsView.hh"
#include "IosShell.hh"
#include "NativeFormResources.h"

/// One in-flight image fetch (owned by _pendingImages until it completes
/// or the page is left).
@interface PendingImage : NSObject
@property (nonatomic, assign) DilloWeb *web;
@property (nonatomic, assign) int clientKey;
@property (nonatomic, strong) NSURLSessionDataTask *task;
@end
@implementation PendingImage
@end

@implementation DilloBridge {
   dw::apple::ApplePlatform *_platform;   // owned by _layout
   dw::core::Layout *_layout;
   dw::apple::CoreGraphicsView *_view;    // owned by _layout
   BrowserWindow *_bw;
   char *_pageBuf;       // current page's parser buffer (mutated, retained)
   char *_oldPageBuf;    // previous page's buffer, freed after widget swap
   NSURLSession *_imageSession;
   NSMutableDictionary<NSNumber *, PendingImage *> *_pendingImages;
   dw::core::ui::ResourceFactory *_formFactory;
}

static void invalidateTrampoline (void *data)
{
   DilloBridge *self = (__bridge DilloBridge *)data;
   if (self.onInvalidate)
      self.onInvalidate ();
}

static void openUrlTrampoline (void *ctx, const DilloUrl *url)
{
   DilloBridge *self = (__bridge DilloBridge *)ctx;
   NSLog (@"DilloBridge: navigate: %s%s", URL_STR (url),
          (URL_FLAGS (url) & URL_Post) ? " (POST)" : "");
   if (!self.onOpenURL)
      return;

   NSString *urlString = [NSString stringWithUTF8String:URL_STR (url)];
   NSData *body = nil;
   NSString *contentType = nil;

   if ((URL_FLAGS (url) & URL_Post) && URL_DATA (url)) {
      Dstr *data = URL_DATA (url);
      body = [NSData dataWithBytes:data->str length:data->len];
      if (URL_FLAGS (url) & URL_MultipartEnc) {
         /* The boundary is the first body line minus the leading dashes
          * (this is how the desktop http.c derives it too). */
         NSString *boundary = nil;
         if (data->len > 2 && data->str[0] == '-' && data->str[1] == '-') {
            const char *end = (const char *) memchr (data->str, '\r',
                                                     data->len);
            if (end)
               boundary = [[NSString alloc]
                             initWithBytes:data->str + 2
                                    length:end - data->str - 2
                                  encoding:NSUTF8StringEncoding];
         }
         contentType = boundary ?
            [NSString stringWithFormat:
                @"multipart/form-data; boundary=%@", boundary] :
            @"multipart/form-data";
      } else {
         contentType = @"application/x-www-form-urlencoded";
      }
   }

   self.onOpenURL (urlString, body, contentType);
}

static void titleTrampoline (void *ctx, const char *title)
{
   DilloBridge *self = (__bridge DilloBridge *)ctx;
   if (self.onTitle)
      self.onTitle ([NSString stringWithUTF8String:title]);
}

static void imageFetchTrampoline (void *ctx, DilloWeb *web, int clientKey)
{
   DilloBridge *self = (__bridge DilloBridge *)ctx;
   [self fetchImageForWeb:web clientKey:clientKey];
}

/* One-time engine-wide initialization; mirrors the relevant subset (and
 * order) of dillo.cc's main(). */
static void initDilloOnce (void)
{
   static bool done = false;
   if (done)
      return;
   done = true;

   a_Prefs_init ();

   /* The desktop defaults (DejaVu) don't exist on iOS; use fonts that do. */
   dFree (prefs.font_serif);      prefs.font_serif = dStrdup ("Georgia");
   dFree (prefs.font_sans_serif); prefs.font_sans_serif =
                                     dStrdup ("Helvetica Neue");
   dFree (prefs.font_monospace);  prefs.font_monospace =
                                     dStrdup ("Courier New");
   dFree (prefs.font_cursive);    prefs.font_cursive =
                                     dStrdup ("Snell Roundhand");
   dFree (prefs.font_fantasy);    prefs.font_fantasy = dStrdup ("Papyrus");

   prefs.load_images = TRUE;

   a_Web_init ();
   a_Bw_init ();
   StyleEngine::init ();
}

- (instancetype)initWithViewportWidth:(int)width height:(int)height
{
   self = [super init];
   if (!self)
      return nil;

   initDilloOnce ();

   _platform = new dw::apple::ApplePlatform ();
   _layout = new dw::core::Layout (_platform);

   _view = new dw::apple::CoreGraphicsView ();
   _view->setInvalidateFunc (invalidateTrampoline, (__bridge void *)self);
   _layout->attachView (_view);

   _bw = a_Bw_new ();
   _bw->render_layout = _layout;

   _pageBuf = NULL;
   _oldPageBuf = NULL;

   _imageSession =
      [NSURLSession sessionWithConfiguration:
                       [NSURLSessionConfiguration defaultSessionConfiguration]];
   _pendingImages = [NSMutableDictionary dictionary];

   _layout->viewportSizeChanged (_view, width, height);

   dw::ios::setShellHooks (openUrlTrampoline, titleTrampoline,
                           imageFetchTrampoline, (__bridge void *)self);

   return self;
}

/* Fetch one image asynchronously; deliver (or abort) on the main thread.
 * The engine is single-threaded — every engine call stays on main. */
- (void)fetchImageForWeb:(DilloWeb *)web clientKey:(int)clientKey
{
   NSString *urlString =
      [NSString stringWithUTF8String:URL_STR (web->url)];
   NSURL *url = [NSURL URLWithString:urlString];
   if (!url) {
      dw::ios::abortImageWeb (web, clientKey);
      return;
   }

   PendingImage *pending = [PendingImage new];
   pending.web = web;
   pending.clientKey = clientKey;
   NSNumber *key = @(clientKey);
   _pendingImages[key] = pending;

   __weak DilloBridge *weakSelf = self;
   pending.task = [_imageSession dataTaskWithURL:url
                 completionHandler:^(NSData *data, NSURLResponse *response,
                                     NSError *error) {
      dispatch_async (dispatch_get_main_queue (), ^{
         DilloBridge *self = weakSelf;
         if (!self)
            return;
         /* If the page was left, cancelPendingImages already freed the
          * web; the entry's absence tells us to do nothing. */
         PendingImage *p = self->_pendingImages[key];
         if (!p)
            return;
         [self->_pendingImages removeObjectForKey:key];

         if (error || !data || data.length == 0)
            dw::ios::abortImageWeb (p.web, p.clientKey);
         else
            dw::ios::deliverImageData (p.web, p.clientKey,
                                       data.bytes, (int) data.length);
         self->_platform->runIdles ();
         if (self.onInvalidate)
            self.onInvalidate ();
      });
   }];
   [pending.task resume];
}

- (void)cancelPendingImages
{
   for (NSNumber *key in _pendingImages) {
      PendingImage *p = _pendingImages[key];
      [p.task cancel];
      dw::ios::cancelImageWeb (p.web, p.clientKey);
   }
   [_pendingImages removeAllObjects];
}

- (void)setFormHost:(UIView *)host
{
   if (_formFactory)
      dw::ios::destroyNativeResourceFactory (_formFactory);
   _formFactory = dw::ios::createNativeResourceFactory (host);
   _platform->setResourceFactory (_formFactory);
}

- (void)dealloc
{
   [self cancelPendingImages];
   dw::ios::setShellHooks (NULL, NULL, NULL, NULL);
   delete _layout;   // deletes platform, view, widget tree → DilloHtml
   a_Bw_free (_bw);
   dFree (_pageBuf);
   dFree (_oldPageBuf);
   if (_formFactory)
      dw::ios::destroyNativeResourceFactory (_formFactory);
}

- (NSString *)resolveURL:(NSString *)href baseURL:(NSString *)base
{
   DilloUrl *baseUrl = a_Url_new ([base UTF8String], NULL);
   if (!baseUrl)
      return nil;
   DilloUrl *url = a_Url_new ([href UTF8String], URL_STR (baseUrl));
   NSString *result =
      url ? [NSString stringWithUTF8String:URL_STR (url)] : nil;
   a_Url_free (url);
   a_Url_free (baseUrl);
   return result;
}

- (void)clearCache
{
   dw::ios::cacheClear ();
}

- (void)seedCSS:(NSString *)cssText forURL:(NSString *)url
{
   const char *bytes = [cssText UTF8String];
   dw::ios::cachePut ([url UTF8String], "text/css; charset=UTF-8",
                      bytes, (int) strlen (bytes));
}

- (void)loadHTML:(NSString *)html url:(NSString *)urlString
{
   /* Leaving the page: outstanding image fetches must not touch the old
    * widget tree once it's freed below. */
   [self cancelPendingImages];

   DilloUrl *url = a_Url_new ([urlString UTF8String], NULL);

   DilloHtml *page = new DilloHtml (_bw, url, "text/html; charset=UTF-8");

   /* Mirror a_Web_dispatch_by_type()'s RootUrl block. */
   {
      dw::core::style::Color *bgColor =
         dw::core::style::Color::create (_layout, prefs.bg_color);
      _layout->setBgColor (bgColor);

      StyleEngine styleEngine (_layout, url, url, _bw->zoom);
      styleEngine.startElement ("body", _bw);
      page->dw->setStyle (styleEngine.style (_bw));
   }

   /* This frees the previous widget tree → previous DilloHtml; only after
    * that may the previous page buffer be released. */
   _layout->setWidget (page->dw);
   dFree (_oldPageBuf);
   _oldPageBuf = NULL;

   /* The parser tokenizes the buffer IN PLACE and retains the pointer
    * (Start_Buf), so it needs writable heap memory that outlives it. */
   _pageBuf = dStrdup ([html UTF8String]);
   page->write (_pageBuf, strlen (_pageBuf), 1 /* Eof */);
   page->finishParsing (0 /* ClientKey; no cache client exists */);
   _oldPageBuf = _pageBuf;   // becomes "previous" on the next load
   _pageBuf = NULL;

   a_Url_free (url);

   _platform->runIdles ();

   if (self.onInvalidate)
      self.onInvalidate ();
}

- (void)setViewportWidth:(int)width height:(int)height
{
   _layout->viewportSizeChanged (_view, width, height);
   _platform->runIdles ();
   if (self.onInvalidate)
      self.onInvalidate ();
}

- (void)tapAtX:(int)x y:(int)y
{
   NSLog (@"DilloBridge: tap at canvas (%d, %d)", x, y);
   _layout->buttonPress (_view, 1, x, y, dw::core::BUTTON1_MASK, 1);
   _layout->buttonRelease (_view, 1, x, y, dw::core::BUTTON1_MASK, 1);
}

- (int)canvasWidth
{
   return _view->getCanvasWidth ();
}

- (int)canvasHeight
{
   return _view->getCanvasHeight ();
}

- (void)drawIntoContext:(CGContextRef)ctx visibleRect:(CGRect)rect
{
   // Late-queued idles (e.g. a reflow) must run before painting.
   _platform->runIdles ();

   dw::core::Rectangle area;
   area.x = (int) rect.origin.x;
   area.y = (int) rect.origin.y;
   area.width = (int) rect.size.width;
   area.height = (int) rect.size.height;

   /* The context's origin is the visible slice's top-left; shift it so
    * canvas coordinates land in the right place, then expose only the
    * visible area. */
   CGContextSaveGState (ctx);
   CGContextTranslateCTM (ctx, -rect.origin.x, -rect.origin.y);

   _view->setCGContext (ctx);
   _view->clearBackground (area.x, area.y, area.width, area.height);
   _layout->expose (_view, &area);
   _view->setCGContext (NULL);

   CGContextRestoreGState (ctx);
}

@end
