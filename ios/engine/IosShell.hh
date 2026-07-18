/*
 * Dillo iOS — seam between the engine/parser world and the app shell.
 *
 * Two things live behind this header:
 *
 * 1. Shell hooks: the browser-shell entry points the parser calls
 *    (open a URL, set the page title) get forwarded to whatever the app
 *    registers here, instead of FLTK's uicmd layer.
 *
 * 2. The mini-cache backing the a_Capi_* implementation in IosCapi.cc:
 *    the app shell prefetches subresources (stylesheets) and seeds them
 *    here; the parser then finds them "cached and completed", which is the
 *    same synchronous path a warm cache hits on the desktop.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_IOS_SHELL_HH__
#define __DW_IOS_SHELL_HH__

#include "../../src/url.h"

namespace dw {
namespace ios {

typedef struct _DilloWeb DilloWeb;

typedef void (*OpenUrlHook) (void *ctx, const DilloUrl *url);
typedef void (*TitleHook) (void *ctx, const char *title);

/**
 * Called when the parser wants an image fetched (a_Capi_open_url with an
 * image web). The shell fetches asynchronously and must eventually call
 * either deliverImageData or abortImageWeb with the same web/clientKey —
 * unless the web was already freed by a cancelImageWeb sweep.
 */
typedef void (*ImageFetchHook) (void *ctx, DilloWeb *web, int clientKey);

/**
 * Called when a form's file input wants a file (a_UIcmd_select_file).
 * Must run a modal picker and return a readable local path (storage owned
 * by the shell, valid until the next call), or NULL on cancel.
 */
typedef const char *(*SelectFileHook) (void *ctx);

/** Selected text should reach the system pasteboard. */
typedef void (*CopyHook) (void *ctx, const char *text);

/** Register the app-shell callbacks (pass NULLs to clear). */
void setShellHooks (OpenUrlHook openUrl, TitleHook title,
                    ImageFetchHook imageFetch, SelectFileHook selectFile,
                    CopyHook copy, void *ctx);

/* Called by the a_UIcmd_* stubs in BrowserStubs.cc / IosCapi.cc,
 * and by ApplePlatform::copySelection. */
void dispatchOpenUrl (const DilloUrl *url);
void dispatchTitle (const char *title);
bool dispatchImageFetch (DilloWeb *web, int clientKey);
const char *dispatchSelectFile ();
void dispatchCopy (const char *text);

/**
 * Decode fetched image bytes (any ImageIO format) and feed them into the
 * web's DilloImage: imgbuf creation, set_parms, row writes, close. Frees
 * the web and closes the bw client. Failure to decode behaves like abort.
 */
void deliverImageData (DilloWeb *web, int clientKey,
                       const void *bytes, int len);

/** Abort an image fetch: tell the image, close the client, free the web. */
void abortImageWeb (DilloWeb *web, int clientKey);

/** Drop a pending image web without touching its (possibly dead) image
    widget state beyond the normal unref — used when leaving the page. */
void cancelImageWeb (DilloWeb *web, int clientKey);

/** Store a completed resource (keyed by its absolute URL string). */
void cachePut (const char *url, const char *contentType,
               const char *data, int len);

/** Drop all cached resources (called at the start of each page load). */
void cacheClear ();

/* Lookup used by IosCapi.cc; returns false if absent. */
bool cacheGet (const char *url, const char **contentType,
               const char **data, int *len);
void cacheSetContentType (const char *url, const char *contentType);

} // namespace ios
} // namespace dw

#endif // __DW_IOS_SHELL_HH__
