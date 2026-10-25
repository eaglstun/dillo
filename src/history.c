/*
 * File: history.c
 *
 * Copyright (C) 2001-2007 Jorge Arellano Cid <jcid@dillo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

/** @file
 * Linear history (it also provides indexes for the navigation stack)
 */

#include "msg.h"
#include "list.h"
#include "cache.h"
#include "history.h"
#include "prefs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


typedef struct {
   DilloUrl *url;
   char *title;
} H_Item;

typedef struct {
   DilloUrl *url;
   char *title;
   time_t visited_at;
} H_Visit;


/* Global history list */
static H_Item *history = NULL;
static int history_size = 0;        /* [1 based] */
static int history_size_max = 16;

/* Persistent browsing history, ordered from oldest to newest. This is kept
 * separate from history[] because navigation stacks store indexes into that
 * array and rely on them staying stable for the life of the process. */
static H_Visit *visits = NULL;
static int visits_size = 0;
static int visits_size_max = 16;
static char *history_filename = NULL;

static const char HistoryFileHeader[] = "# Dillo browsing history v1\n";


static int History_limit(void)
{
   /* A zero persistence limit still retains session history for visited-link
    * styling and about:history. */
   return prefs.history_max_entries > 0 ? prefs.history_max_entries : 1000;
}

static int History_recordable(const DilloUrl *url)
{
   const char *scheme;

   if (!url)
      return FALSE;
   scheme = URL_SCHEME(url);
   return URL_STR(url)[0] &&
      (dStrAsciiCasecmp(scheme, "http") == 0 ||
       dStrAsciiCasecmp(scheme, "https") == 0);
}

static void History_visit_free(H_Visit *visit)
{
   a_Url_free(visit->url);
   dFree(visit->title);
}

static int History_find_visit(const DilloUrl *url)
{
   int i;

   for (i = visits_size - 1; i >= 0; --i)
      if (a_Url_cmp(visits[i].url, url) == 0)
         return i;
   return -1;
}

static void History_trim_visits(void)
{
   int remove = visits_size - History_limit();

   if (remove > 0) {
      int i;
      for (i = 0; i < remove; ++i)
         History_visit_free(&visits[i]);
      memmove(visits, visits + remove,
              (visits_size - remove) * sizeof(H_Visit));
      visits_size -= remove;
   }
}

static void History_remember(const DilloUrl *url, const char *title,
                             time_t visited_at)
{
   H_Visit visit;
   int idx;

   if (!History_recordable(url))
      return;

   idx = History_find_visit(url);
   if (idx >= 0) {
      visit = visits[idx];
      if (idx + 1 < visits_size)
         memmove(visits + idx, visits + idx + 1,
                 (visits_size - idx - 1) * sizeof(H_Visit));
      --visits_size;
      a_Url_free(visit.url);
      visit.url = a_Url_dup(url);
      if (title) {
         dFree(visit.title);
         visit.title = dStrdup(title);
      }
      visit.visited_at = visited_at;
   } else {
      visit.url = a_Url_dup(url);
      visit.title = title ? dStrdup(title) : NULL;
      visit.visited_at = visited_at;
   }

   a_List_add(visits, visits_size, visits_size_max);
   visits[visits_size++] = visit;
   History_trim_visits();
}

static char *History_hex_encode(const char *str)
{
   static const char Hex[] = "0123456789ABCDEF";
   const unsigned char *src = (const unsigned char *)(str ? str : "");
   size_t len = strlen((const char *)src), i;
   char *encoded = dNew(char, 2 * len + 1);

   for (i = 0; i < len; ++i) {
      encoded[2*i] = Hex[src[i] >> 4];
      encoded[2*i+1] = Hex[src[i] & 15];
   }
   encoded[2*len] = 0;
   return encoded;
}

static int History_hex_value(char c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
   if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
   return -1;
}

static char *History_hex_decode(const char *encoded)
{
   size_t len = strlen(encoded), i;
   char *decoded;

   if (len & 1)
      return NULL;
   decoded = dNew(char, len / 2 + 1);
   for (i = 0; i < len; i += 2) {
      int hi = History_hex_value(encoded[i]);
      int lo = History_hex_value(encoded[i+1]);
      if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) {
         dFree(decoded);
         return NULL;
      }
      decoded[i/2] = (char)((hi << 4) | lo);
   }
   decoded[len/2] = 0;
   return decoded;
}

static int History_lock(int fd, short type)
{
   struct flock lock;
   int rc;

   memset(&lock, 0, sizeof(lock));
   lock.l_type = type;
   lock.l_whence = SEEK_SET;
   do
      rc = fcntl(fd, F_SETLKW, &lock);
   while (rc < 0 && errno == EINTR);
   return rc;
}

static void History_write_visit(FILE *fp, const H_Visit *visit)
{
   char *url = History_hex_encode(URL_STR(visit->url));
   char *title = History_hex_encode(visit->title);

   fprintf(fp, "%lld\t%s\t%s\n", (long long)visit->visited_at, url, title);
   dFree(title);
   dFree(url);
}

static void History_append_latest(void)
{
   FILE *fp;

   if (!prefs.save_history || prefs.history_max_entries <= 0 ||
       !history_filename || visits_size == 0)
      return;

   fp = fopen(history_filename, "a");
   if (!fp) {
      MSG_WARN("history: Cannot open %s: %s\n", history_filename,
               dStrerror(errno));
      return;
   }
   fchmod(fileno(fp), 0600);
   if (History_lock(fileno(fp), F_WRLCK) == 0) {
      History_write_visit(fp, &visits[visits_size-1]);
      fflush(fp);
      History_lock(fileno(fp), F_UNLCK);
   } else {
      MSG_WARN("history: Cannot lock %s: %s\n", history_filename,
               dStrerror(errno));
   }
   fclose(fp);
}

static void History_rewrite(FILE *fp)
{
   int i, fd = fileno(fp);

   rewind(fp);
   if (ftruncate(fd, 0) < 0) {
      MSG_WARN("history: Cannot truncate %s: %s\n", history_filename,
               dStrerror(errno));
      return;
   }
   fprintf(fp, "%s", HistoryFileHeader);
   for (i = 0; i < visits_size; ++i)
      History_write_visit(fp, &visits[i]);
   fflush(fp);
}

/**
 * Load and compact the persistent browsing history.
 */
void a_History_init(const char *filename)
{
   FILE *fp;
   char *line;

   history_filename = filename ? dStrdup(filename) :
      dStrconcat(dGethomedir(), "/.dillo/history.txt", NULL);
   if (!prefs.save_history || prefs.history_max_entries <= 0)
      return;

   fp = fopen(history_filename, "a+");
   if (!fp) {
      MSG_WARN("history: Cannot open %s: %s\n", history_filename,
               dStrerror(errno));
      return;
   }
   fchmod(fileno(fp), 0600);
   if (History_lock(fileno(fp), F_WRLCK) < 0) {
      MSG_WARN("history: Cannot lock %s: %s\n", history_filename,
               dStrerror(errno));
      fclose(fp);
      return;
   }

   rewind(fp);
   while ((line = dGetline(fp)) != NULL) {
      char *tab1, *tab2, *end;
      long long timestamp;
      size_t line_len = strlen(line);

      while (line_len > 0 &&
             (line[line_len-1] == '\n' || line[line_len-1] == '\r'))
         line[--line_len] = 0;

      if (line[0] == '#' || line[0] == 0) {
         dFree(line);
         continue;
      }
      tab1 = strchr(line, '\t');
      tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
      if (!tab1 || !tab2) {
         dFree(line);
         continue;
      }
      *tab1 = *tab2 = 0;
      timestamp = strtoll(line, &end, 10);
      if (*end == 0) {
         char *urlstr = History_hex_decode(tab1 + 1);
         char *title = History_hex_decode(tab2 + 1);
         DilloUrl *url = urlstr ? a_Url_new(urlstr, NULL) : NULL;
         if (url && title)
            History_remember(url, title[0] ? title : NULL, (time_t)timestamp);
         a_Url_free(url);
         dFree(title);
         dFree(urlstr);
      }
      dFree(line);
   }
   History_rewrite(fp);
   History_lock(fileno(fp), F_UNLCK);
   fclose(fp);
   MSG("History loaded: %d entr%s.\n", visits_size,
       visits_size == 1 ? "y" : "ies");
}


/**
 * Debug procedure.
 */
void History_show(void)
{
   int i;

   MSG("  {");
   for (i = 0; i < history_size; ++i)
      MSG(" %s", URL_STR(history[i].url));
   MSG(" }\n");
}

/**
 * Add a new H_Item at the end of the history list
 * (taking care of not making a duplicate entry)
 */
int a_History_add_url(DilloUrl *url)
{
   int i, idx;

   _MSG("a_History_add_url: '%s' ", URL_STR(url));
   for (i = 0; i < history_size; ++i)
      if (!a_Url_cmp(history[i].url, url) &&
          !strcmp(URL_FRAGMENT(history[i].url), URL_FRAGMENT(url)))
         break;

   if (i < history_size) {
      idx = i;
      _MSG("FOUND at idx=%d\n", idx);
   } else {
      idx = history_size;
      a_List_add(history, history_size, history_size_max);
      history[idx].url = a_Url_dup(url);
      history[idx].title = NULL;
      ++history_size;
      _MSG("ADDED at idx=%d\n", idx);
   }

   History_remember(url, NULL, time(NULL));
   History_append_latest();

   /* History_show(); */

   return idx;
}

/**
 * Return the DilloUrl field (by index)
 */
const DilloUrl *a_History_get_url(int idx)
{
   _MSG("a_History_get_url: ");
   /* History_show(); */

   dReturn_val_if_fail(idx >= 0 && idx < history_size, NULL);

   return history[idx].url;
}

/**
 * Return the title field (by index)
 * ('force' returns URL_STR when there's no title)
 */
const char *a_History_get_title(int idx, int force)
{
   dReturn_val_if_fail(idx >= 0 && idx < history_size, NULL);

   if (history[idx].title)
      return history[idx].title;
   else if (force)
      return URL_STR(history[idx].url);
   else
      return NULL;
}

/**
 * Return the title field (by url)
 * ('force' returns URL_STR when there's no title)
 */
const char *a_History_get_title_by_url(const DilloUrl *url, int force)
{
   int i;

   dReturn_val_if_fail(url != NULL, NULL);

   for (i = 0; i < history_size; ++i)
      if (a_Url_cmp(url, history[i].url) == 0)
         break;

   if (i < history_size && history[i].title)
      return history[i].title;
   else if (force)
      return URL_STR_(url);
   return NULL;
}

/**
 * Set the page-title for a given URL
 */
void a_History_set_title_by_url(const DilloUrl *url, const char *title)
{
   int i;

   dReturn_if (url == NULL);

   for (i = history_size - 1; i >= 0; --i)
      if (a_Url_cmp(url, history[i].url) == 0)
         break;

   if (i >= 0) {
      dFree(history[i].title);
      history[i].title = dStrdup(title);
      History_remember(url, title, time(NULL));
      History_append_latest();
   } else {
      MSG_ERR("a_History_set_title_by_url: %s not found\n", URL_STR(url));
   }
}

/**
 * Return whether a URL has been visited in this or a previous session.
 */
bool_t a_History_is_visited(const DilloUrl *url)
{
   return url && History_find_visit(url) >= 0;
}

static void History_html_escape(Dstr *html, const char *text)
{
   const unsigned char *p = (const unsigned char *)(text ? text : "");

   for (; *p; ++p) {
      switch (*p) {
      case '&': dStr_append(html, "&amp;"); break;
      case '<': dStr_append(html, "&lt;"); break;
      case '>': dStr_append(html, "&gt;"); break;
      case '"': dStr_append(html, "&quot;"); break;
      default: dStr_append_c(html, *p); break;
      }
   }
}

/**
 * Regenerate about:history from the current visit list.
 */
void a_History_generate_page(void)
{
   Dstr *html = dStr_new(
      "<!DOCTYPE HTML>\n<html><head><title>History</title>\n"
      "<style>body{background:white;margin:3em;font:16px sans-serif;"
      "line-height:1.4}.main{max-width:60em}table{border-collapse:collapse;"
      "width:100%}th,td{text-align:left;padding:.35em .6em;"
      "border-bottom:1px solid #ccc}th{background:#eee}td.when{"
      "white-space:nowrap;color:#555}small{color:#555}</style>\n"
      "</head><body><div class=main><h1>History</h1>\n");
   DilloUrl *page_url;
   int i;

   if (!prefs.save_history)
      dStr_append(html, "<p><small>Saving is disabled; only pages from this "
                        "session are shown.</small></p>\n");
   else if (prefs.history_max_entries <= 0)
      dStr_append(html, "<p><small>Persistent history is set to zero entries; "
                        "only pages from this session are shown.</small></p>\n");

   if (visits_size == 0) {
      dStr_append(html, "<p>No browsing history yet.</p>\n");
   } else {
      dStr_append(html, "<table><tr><th>Visited<th>Page</tr>\n");
      for (i = visits_size - 1; i >= 0; --i) {
         char when[64] = "";
         struct tm tm;
         const char *title = visits[i].title && visits[i].title[0] ?
                             visits[i].title : URL_STR(visits[i].url);

         if (localtime_r(&visits[i].visited_at, &tm))
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm);
         dStr_append(html, "<tr><td class=when>");
         History_html_escape(html, when);
         dStr_append(html, "<td><a href=\"");
         History_html_escape(html, URL_STR(visits[i].url));
         dStr_append(html, "\">");
         History_html_escape(html, title);
         dStr_append(html, "</a></tr>\n");
      }
      dStr_append(html, "</table>\n");
   }
   dStr_append(html, "</div></body></html>\n");

   page_url = a_Url_new("about:history", NULL);
   a_Cache_entry_inject(page_url, html->str, html->len,
                        CA_GotHeader | CA_GotLength | CA_InternalUrl);
   a_Url_free(page_url);
   dStr_free(html, 1);
}

/**
 * Remove all persistent and session visit records. Navigation stacks remain
 * intact, so Back and Forward continue to work in already-open tabs.
 */
void a_History_clear(void)
{
   FILE *fp;
   int i;

   for (i = 0; i < visits_size; ++i)
      History_visit_free(&visits[i]);
   visits_size = 0;

   if (!history_filename)
      return;
   fp = fopen(history_filename, "a+");
   if (!fp) {
      MSG_WARN("history: Cannot open %s: %s\n", history_filename,
               dStrerror(errno));
      return;
   }
   fchmod(fileno(fp), 0600);
   if (History_lock(fileno(fp), F_WRLCK) == 0) {
      History_rewrite(fp);
      History_lock(fileno(fp), F_UNLCK);
   } else {
      MSG_WARN("history: Cannot lock %s: %s\n", history_filename,
               dStrerror(errno));
   }
   fclose(fp);
}


/**
 * Free all the memory used by this module
 */
void a_History_freeall(void)
{
   int i;

   for (i = 0; i < history_size; ++i) {
      a_Url_free(history[i].url);
      dFree(history[i].title);
   }
   dFree(history);
   for (i = 0; i < visits_size; ++i)
      History_visit_free(&visits[i]);
   dFree(visits);
   dFree(history_filename);
   history = NULL;
   history_size = 0;
   history_size_max = 16;
   visits = NULL;
   visits_size = 0;
   visits_size_max = 16;
   history_filename = NULL;
}
