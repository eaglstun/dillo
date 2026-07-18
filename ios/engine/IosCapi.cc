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

#include "../../src/msg.h"
#include "../../src/bw.h"
#include "../../src/web.hh"
#include "../../src/capi.h"

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
   /* Everything fetchable was prefetched; anything else (images for now,
    * or CSS the prefetch scan missed) is unavailable. Callers drop their
    * reference on a zero key, so free the web here. */
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
