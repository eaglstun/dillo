/*
 * Persistent browsing history test.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "src/cache.h"
#include "src/history.h"
#include "src/prefs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

DilloPrefs prefs;
static Dstr *injected_html;

bool_t a_Hsts_require_https(const char *host)
{
   (void)host;
   return FALSE;
}

void a_Cache_entry_inject(const DilloUrl *url, const char *buf, size_t len,
                          int flags)
{
   (void)url;
   (void)flags;
   if (injected_html)
      dStr_free(injected_html, 1);
   injected_html = dStr_sized_new(len + 1);
   dStr_append_l(injected_html, buf, len);
}

static DilloUrl *add(const char *urlstr, const char *title)
{
   DilloUrl *url = a_Url_new(urlstr, NULL);
   assert(url);
   a_History_add_url(url);
   a_History_set_title_by_url(url, title);
   return url;
}

int main(void)
{
   char path[] = "/tmp/dillo-history-test-XXXXXX";
   DilloUrl *one, *two, *three, *local;
   struct stat st;
   int fd = mkstemp(path);

   assert(fd >= 0);
   close(fd);
   prefs.save_history = TRUE;
   prefs.history_max_entries = 2;

   a_History_init(path);
   assert(stat(path, &st) == 0);
   assert((st.st_mode & 0777) == 0600);
   one = add("https://example.test/one", "One");
   two = add("https://example.test/two?a=1&b=2", "Two & <friends>");
   three = add("https://example.test/three", "Three");
   local = add("file:///tmp/private-name", "Local");

   assert(!a_History_is_visited(one));
   assert(a_History_is_visited(two));
   assert(a_History_is_visited(three));
   assert(!a_History_is_visited(local));
   a_History_generate_page();
   assert(strstr(injected_html->str, "Two &amp; &lt;friends&gt;"));
   assert(strstr(injected_html->str, "?a=1&amp;b=2"));
   assert(!strstr(injected_html->str, "private-name"));

   a_Url_free(local);
   a_Url_free(three);
   a_Url_free(two);
   a_Url_free(one);
   a_History_freeall();

   a_History_init(path);
   two = a_Url_new("https://example.test/two?a=1&b=2", NULL);
   three = a_Url_new("https://example.test/three", NULL);
   assert(a_History_is_visited(two));
   assert(a_History_is_visited(three));
   a_History_generate_page();
   assert(strstr(injected_html->str, "Two &amp; &lt;friends&gt;"));

   a_History_clear();
   assert(!a_History_is_visited(two));
   assert(!a_History_is_visited(three));
   a_Url_free(three);
   a_Url_free(two);
   a_History_freeall();
   dStr_free(injected_html, 1);
   unlink(path);
   return 0;
}
