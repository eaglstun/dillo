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
#include "../../src/capi.h"
#include "../../src/hsts.h"
#include "../../src/uicmd.hh"
#include "../../src/dialog.hh"

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
 * ----- capi.c (network/cache entry points) -----
 * No network, no cache: every URL is unavailable.
 */

int a_Capi_open_url(DilloWeb *web, CA_Callback_t Call, void *CbData)
{
   /* Callers only add a cache client when the key is nonzero; on zero they
    * drop their reference to the web, so free it here to avoid a leak. */
   MSG("iOS spike: not fetching %s\n", URL_STR(web->url));
   a_Web_free(web);
   return 0;
}

int a_Capi_get_buf(const DilloUrl *Url, char **PBuf, int *BufSize)
{
   *PBuf = NULL;
   *BufSize = 0;
   return 0;
}

void a_Capi_unref_buf(const DilloUrl *Url)
{
}

const char *a_Capi_set_content_type(const DilloUrl *url, const char *ctype,
                                    const char *from)
{
   return ctype;
}

int a_Capi_get_flags_with_redirection(const DilloUrl *Url)
{
   return 0;
}

int a_Capi_dpi_verify_request(BrowserWindow *bw, DilloUrl *url)
{
   return 0;
}

void a_Capi_stop_client(int Key, int force)
{
}

/*
 * ----- hsts.c -----
 */

bool_t a_Hsts_require_https(const char *host)
{
   return FALSE;
}

/*
 * ----- uicmd.cc (FLTK UI command layer) -----
 */

void a_UIcmd_open_url(BrowserWindow *bw, const DilloUrl *url)
{
   MSG("iOS spike: a_UIcmd_open_url %s\n", url ? URL_STR(url) : "(null)");
}

void a_UIcmd_open_url_nw(BrowserWindow *bw, const DilloUrl *url)
{
}

void a_UIcmd_open_url_nt(void *vbw, const DilloUrl *url, int focus)
{
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
   return NULL;
}

void a_UIcmd_page_popup(void *vbw, bool_t has_bugs, void *v_cssUrls)
{
}

void a_UIcmd_link_popup(void *vbw, const DilloUrl *url,
                        const DilloUrl *page_url)
{
}

void a_UIcmd_image_popup(void *vbw, const DilloUrl *url, bool_t loaded_img,
                         DilloUrl *page_url, DilloUrl *link_url)
{
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
   MSG("iOS spike: page title: %s\n", label);
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
