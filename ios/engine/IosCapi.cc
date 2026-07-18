/*
 * Dillo iOS — a_Capi_* implementation backed by the shell mini-cache.
 *
 * The desktop capi routes through the cache and the socket/TLS IO stack.
 * On iOS the app shell fetches with NSURLSession and seeds finished
 * resources into the mini-cache (IosShell.hh) BEFORE parsing starts, so the
 * parser's cached-and-completed fast path (see a_Html_load_stylesheet)
 * serves everything synchronously. Anything not in the cache is reported
 * unavailable, exactly like an offline desktop Dillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <map>
#include <string>

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include "../../src/msg.h"
#include "../../src/bw.h"
#include "../../src/web.hh"
#include "../../src/capi.h"
#include "../../src/image.hh"
#include "../../src/imgbuf.hh"

#include "IosShell.hh"

namespace dw {
namespace ios {

struct CacheEntry {
   std::string contentType;
   std::string data;
};

/* Keyed by absolute URL string. Only touched from the main thread (the
 * shell seeds before parse; the parser reads during parse). */
static std::map<std::string, CacheEntry> *cache = NULL;

static std::map<std::string, CacheEntry> *getCache ()
{
   if (!cache)
      cache = new std::map<std::string, CacheEntry> ();
   return cache;
}

void cachePut (const char *url, const char *contentType,
               const char *data, int len)
{
   CacheEntry &e = (*getCache ())[std::string (url)];
   e.contentType = contentType ? contentType : "";
   e.data.assign (data, len);
}

void cacheClear ()
{
   getCache ()->clear ();
}

bool cacheGet (const char *url, const char **contentType,
               const char **data, int *len)
{
   std::map<std::string, CacheEntry>::iterator it =
      getCache ()->find (std::string (url));
   if (it == getCache ()->end ())
      return false;
   if (contentType)
      *contentType = it->second.contentType.c_str ();
   if (data)
      *data = it->second.data.data ();
   if (len)
      *len = (int) it->second.data.size ();
   return true;
}

void cacheSetContentType (const char *url, const char *contentType)
{
   std::map<std::string, CacheEntry>::iterator it =
      getCache ()->find (std::string (url));
   if (it != getCache ()->end ())
      it->second.contentType = contentType;
}

/*
 * ----- Image delivery -----
 *
 * The shell fetches image bytes asynchronously; these helpers replay what
 * the desktop's dicache + decoders do: decode (ImageIO instead of the
 * libjpeg/libpng decoders), fill an Imgbuf with RGB rows, and drive the
 * DilloImage state machine (set_parms → write rows → close).
 */

void deliverImageData (DilloWeb *web, int clientKey,
                       const void *bytes, int len)
{
   DilloImage *image = web->Image;
   BrowserWindow *bw = web->bw;
   void *layout = bw->render_layout;
   bool fed = false;

   CFDataRef data =
      CFDataCreateWithBytesNoCopy (kCFAllocatorDefault,
                                   (const UInt8 *) bytes, len,
                                   kCFAllocatorNull);
   CGImageSourceRef source = CGImageSourceCreateWithData (data, NULL);
   CGImageRef cgImage =
      source ? CGImageSourceCreateImageAtIndex (source, 0, NULL) : NULL;

   if (cgImage && image && layout) {
      int w = (int) CGImageGetWidth (cgImage);
      int h = (int) CGImageGetHeight (cgImage);

      /* Composite over the image's background color (like the desktop
       * decoders do for alpha), yielding plain RGB rows. */
      CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB ();
      CGContextRef ctx =
         CGBitmapContextCreate (NULL, w, h, 8, (size_t) w * 4, space,
                                kCGImageAlphaPremultipliedLast);
      if (ctx) {
         int bg = image->bg_color;
         CGContextSetRGBFillColor (ctx,
                                   ((bg >> 16) & 0xff) / 255.0,
                                   ((bg >> 8) & 0xff) / 255.0,
                                   (bg & 0xff) / 255.0, 1.0);
         CGContextFillRect (ctx, CGRectMake (0, 0, w, h));
         CGContextDrawImage (ctx, CGRectMake (0, 0, w, h), cgImage);

         const unsigned char *rgba =
            (const unsigned char *) CGBitmapContextGetData (ctx);
         size_t stride = CGBitmapContextGetBytesPerRow (ctx);

         /* sRGB-encoded data; 2.2 gives gamma-aware scaling like the
          * desktop decoders' default. */
         void *imgbuf = a_Imgbuf_new (layout, DILLO_IMG_TYPE_RGB,
                                      (uint_t) w, (uint_t) h, 2.2);

         a_Image_set_parms (image, imgbuf, web->url, 1,
                            (uint_t) w, (uint_t) h, DILLO_IMG_TYPE_RGB);

         unsigned char *row = (unsigned char *) malloc ((size_t) w * 3);
         for (int y = 0; y < h; y++) {
            const unsigned char *src = rgba + (size_t) y * stride;
            for (int x = 0; x < w; x++) {
               row[x * 3] = src[x * 4];
               row[x * 3 + 1] = src[x * 4 + 1];
               row[x * 3 + 2] = src[x * 4 + 2];
            }
            a_Imgbuf_update (imgbuf, row, DILLO_IMG_TYPE_RGB, NULL,
                             (uint_t) w, (uint_t) h, (uint_t) y);
            a_Image_write (image, (uint_t) y);
         }
         free (row);

         a_Image_close (image);
         a_Imgbuf_unref (imgbuf);   /* the Image widget holds its own ref */
         fed = true;

         CGContextRelease (ctx);
      }
      CGColorSpaceRelease (space);
   }

   if (cgImage)
      CGImageRelease (cgImage);
   if (source)
      CFRelease (source);
   if (data)
      CFRelease (data);

   if (!fed) {
      MSG("IosCapi: could not decode image %s\n", URL_STR (web->url));
      if (image)
         a_Image_abort (image);
   }

   a_Bw_close_client (bw, clientKey);
   a_Web_free (web);
}

void abortImageWeb (DilloWeb *web, int clientKey)
{
   if (web->Image)
      a_Image_abort (web->Image);
   a_Bw_close_client (web->bw, clientKey);
   a_Web_free (web);
}

void cancelImageWeb (DilloWeb *web, int clientKey)
{
   a_Bw_remove_client (web->bw, clientKey);
   a_Web_free (web);
}

} // namespace ios
} // namespace dw

/*
 * ----- capi.c replacement -----
 */

int a_Capi_get_flags_with_redirection(const DilloUrl *Url)
{
   if (dw::ios::cacheGet (URL_STR (Url), NULL, NULL, NULL))
      return CAPI_IsCached | CAPI_Completed;
   return 0;
}

int a_Capi_get_buf(const DilloUrl *Url, char **PBuf, int *BufSize)
{
   const char *data;
   int len;

   if (dw::ios::cacheGet (URL_STR (Url), NULL, &data, &len)) {
      /* Consumers of this buffer (CSS parse) only read it. */
      *PBuf = (char *) data;
      *BufSize = len;
      return 1;
   }
   *PBuf = NULL;
   *BufSize = 0;
   return 0;
}

void a_Capi_unref_buf(const DilloUrl *Url)
{
   /* The mini-cache owns the buffer for the page's lifetime. */
}

const char *a_Capi_set_content_type(const DilloUrl *url, const char *ctype,
                                    const char *from)
{
   dw::ios::cacheSetContentType (URL_STR (url), ctype);
   return ctype;
}

int a_Capi_open_url(DilloWeb *web, CA_Callback_t Call, void *CbData)
{
   static int keyCounter = 0;

   /* Images are fetched asynchronously by the shell; the client key lets
    * bw's image-progress bookkeeping work as on the desktop. */
   if ((web->flags & WEB_Image)) {
      int key = ++keyCounter;
      if (dw::ios::dispatchImageFetch (web, key))
         return key;
   }

   /* Everything else was prefetched (stylesheets) or is unavailable.
    * Callers drop their reference on a zero key, so free the web here. */
   MSG("IosCapi: not fetching %s\n", URL_STR(web->url));
   a_Web_free(web);
   return 0;
}

void a_Capi_stop_client(int Key, int force)
{
}

int a_Capi_dpi_verify_request(BrowserWindow *bw, DilloUrl *url)
{
   return 0;
}
