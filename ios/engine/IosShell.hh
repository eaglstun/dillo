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

typedef void (*OpenUrlHook) (void *ctx, const DilloUrl *url);
typedef void (*TitleHook) (void *ctx, const char *title);

/** Register the app-shell callbacks (pass NULLs to clear). */
void setShellHooks (OpenUrlHook openUrl, TitleHook title, void *ctx);

/* Called by the a_UIcmd_* stubs in BrowserStubs.cc. */
void dispatchOpenUrl (const DilloUrl *url);
void dispatchTitle (const char *title);

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
