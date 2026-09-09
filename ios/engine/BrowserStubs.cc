/*
 * File: BrowserStubs.cc
 *
 * iOS spike stubs for the browser-shell layer the HTML parser and style
 * engine link against but never meaningfully exercise on a static, local
 * page: the cache/capi network stack, the FLTK UI command layer, dialogs,
 * and HSTS. Every stub reports "nothing available / nothing to do", which
 * the callers all handle (it is the same behavior as a fully offline
 * desktop Dillo with an empty cache).
 *
 * a_Web_* is a copy of the data-structure half of src/web.cc (a_Web_new /
 * a_Web_valid / a_Web_free); the dispatch half needs IO/mime and is not
 * needed until real networking lands.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../src/msg.h"
#include "../../src/bw.h"
#include "../../src/web.hh"
#include "../../src/hsts.h"
#include "../../src/cache.h"
#include "../../src/uicmd.hh"
#include "../../src/dialog.hh"

#include "IosShell.hh"

/*
 * ----- IosShell hook registry -----
 */

namespace dw {
namespace ios {

static OpenUrlHook openUrlHook = NULL;
static TitleHook titleHook = NULL;
static ImageFetchHook imageFetchHook = NULL;
static SelectFileHook selectFileHook = NULL;
static CopyHook copyHook = NULL;
static ContextHook contextHook = NULL;
static void *hookCtx = NULL;

void setShellHooks (OpenUrlHook openUrl, TitleHook title,
                    ImageFetchHook imageFetch, SelectFileHook selectFile,
                    CopyHook copy, ContextHook context, void *ctx)
{
   openUrlHook = openUrl;
   titleHook = title;
   imageFetchHook = imageFetch;
   selectFileHook = selectFile;
   copyHook = copy;
   contextHook = context;
   hookCtx = ctx;
}

void dispatchOpenUrl (const DilloUrl *url)
{
   if (openUrlHook)
      openUrlHook (hookCtx, url);
}

void dispatchTitle (const char *title)
{
   if (titleHook)
      titleHook (hookCtx, title);
}

bool dispatchImageFetch (DilloWeb *web, int clientKey)
{
   if (!imageFetchHook)
      return false;
   imageFetchHook (hookCtx, web, clientKey);
   return true;
}

const char *dispatchSelectFile ()
{
   return selectFileHook ? selectFileHook (hookCtx) : NULL;
}

void dispatchCopy (const char *text)
{
   if (copyHook && text)
      copyHook (hookCtx, text);
}

void dispatchContext (ContextKind kind, const DilloUrl *url,
                      const DilloUrl *linkUrl)
{
   if (contextHook)
      contextHook (hookCtx, kind, url, linkUrl);
}

} // namespace ios
} // namespace dw

/*
 * ----- web.cc (data half) -----
 */

static Dlist *ValidWebs = NULL;

void a_Web_init(void)
{
   if (!ValidWebs)
      ValidWebs = dList_new(32);
}

DilloWeb* a_Web_new(BrowserWindow *bw, const DilloUrl *url,
                    const DilloUrl *requester)
{
   DilloWeb *web = dNew(DilloWeb, 1);

   a_Web_init();
   web->url = a_Url_dup(url);
   web->requester = a_Url_dup(requester);
   web->bw = bw;
   web->flags = 0;
   web->Image = NULL;
   web->filename = NULL;
   web->stream = NULL;
   web->SavedBytes = 0;
   web->bgColor = 0x000000;
   dList_append(ValidWebs, (void *)web);
   return web;
}

int a_Web_valid(DilloWeb *web)
{
   return (ValidWebs && dList_find(ValidWebs, web) != NULL);
}

void a_Web_free(DilloWeb *web)
{
   if (!web) return;
   a_Url_free(web->url);
   a_Url_free(web->requester);
   a_Image_unref(web->Image);
   dFree(web->filename);
   dList_remove(ValidWebs, (void *)web);
   dFree(web);
}

/*
 * ----- hsts.c -----
 * (a_Capi_* now lives in IosCapi.cc, backed by the shell mini-cache.)
 */

bool_t a_Hsts_require_https(const char *host)
{
   return FALSE;
}

/* about:history is generated into the desktop cache. The iOS shell keeps
 * navigation history itself and never requests that internal cache page. */
void a_Cache_entry_inject(const DilloUrl *url, const char *buf,
                          size_t len, int flags)
{
}

/*
 * ----- uicmd.cc (FLTK UI command layer) -----
 */

void a_UIcmd_open_url(BrowserWindow *bw, const DilloUrl *url)
{
   if (url)
      dw::ios::dispatchOpenUrl (url);
}

void a_UIcmd_open_url_nw(BrowserWindow *bw, const DilloUrl *url)
{
   /* no windows/tabs (yet): open in place */
   a_UIcmd_open_url (bw, url);
}

void a_UIcmd_open_url_nt(void *vbw, const DilloUrl *url, int focus)
{
   a_UIcmd_open_url ((BrowserWindow *) vbw, url);
}

void a_UIcmd_finish_loading(BrowserWindow *bw)
{
}

void a_UIcmd_repush(void *vbw)
{
}

void a_UIcmd_redirection0(void *vbw, const DilloUrl *url)
{
}

const char *a_UIcmd_select_file(void)
{
   return dw::ios::dispatchSelectFile ();
}

void a_UIcmd_page_popup(void *vbw, bool_t has_bugs, void *v_cssUrls)
{
   dw::ios::dispatchContext (dw::ios::CONTEXT_PAGE, NULL, NULL);
}

void a_UIcmd_link_popup(void *vbw, const DilloUrl *url,
                        const DilloUrl *page_url)
{
   dw::ios::dispatchContext (dw::ios::CONTEXT_LINK, url, NULL);
}

void a_UIcmd_image_popup(void *vbw, const DilloUrl *url, bool_t loaded_img,
                         DilloUrl *page_url, DilloUrl *link_url)
{
   dw::ios::dispatchContext (dw::ios::CONTEXT_IMAGE, url, link_url);
}

void a_UIcmd_form_popup(void *vbw, const DilloUrl *url, void *vform,
                        bool_t showing_hiddens)
{
}

void a_UIcmd_set_img_prog(BrowserWindow *bw, int n_img, int t_img, int cmd)
{
}

void a_UIcmd_set_bug_prog(BrowserWindow *bw, int n_bug)
{
}

void a_UIcmd_set_page_title(BrowserWindow *bw, const char *label)
{
   dw::ios::dispatchTitle (label ? label : "");
}

void a_UIcmd_set_msg(BrowserWindow *bw, const char *format, ...)
{
}

void a_UIcmd_set_buttons_sens(BrowserWindow *bw)
{
}

/*
 * ----- dialog.cc -----
 */

int a_Dialog_choice(const char *title, const char *msg, ...)
{
   return 0;   /* "cancel" */
}
