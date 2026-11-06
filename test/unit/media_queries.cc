/*
 * Unit tests for CSS media query evaluation (CssParser::matchMedia).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>

#include "src/cssparser.hh"
#include "src/prefs.h"

/* cssparser.cc reaches into the HTML loader for @import and selectors;
 * matchMedia never gets there, so inert stubs keep this test standalone. */
DilloPrefs prefs;
DilloUrl *a_Html_url_new(DilloHtml *, const char *, const char *, int)
{
   return NULL;
}
void a_Html_load_stylesheet(DilloHtml *, DilloUrl *) {}
int a_Html_tag_index(const char *) { return -1; }
extern "C" int a_Hsts_require_https(const char *) { return 0; }

static int failures = 0;

static const char *name(CssParser::MediaMatch m)
{
   switch (m) {
   case CssParser::MEDIA_TRUE: return "TRUE";
   case CssParser::MEDIA_FALSE: return "FALSE";
   default: return "UNKNOWN";
   }
}

static void expect(const char *query, CssParser::MediaMatch light,
                   CssParser::MediaMatch dark)
{
   CssParser::setPrefersDark(false);
   CssParser::MediaMatch gotLight = CssParser::matchMedia(query);
   CssParser::setPrefersDark(true);
   CssParser::MediaMatch gotDark = CssParser::matchMedia(query);

   if (gotLight != light || gotDark != dark) {
      printf("FAIL %-55s light=%s (want %s) dark=%s (want %s)\n", query,
             name(gotLight), name(light), name(gotDark), name(dark));
      failures++;
   } else {
      printf("ok   %-55s light=%s dark=%s\n", query, name(gotLight),
             name(gotDark));
   }
}

int main()
{
   const CssParser::MediaMatch T = CssParser::MEDIA_TRUE,
                               F = CssParser::MEDIA_FALSE,
                               U = CssParser::MEDIA_UNKNOWN;

   /* media types, as before */
   expect("", T, T);
   expect("all", T, T);
   expect("screen", T, T);
   expect("SCREEN", T, T);
   expect("print", F, F);
   expect("handheld, screen", T, T);
   expect("only screen", T, T);
   expect("not print", T, T);
   expect("not screen", F, F);

   /* prefers-color-scheme */
   expect("(prefers-color-scheme: dark)", F, T);
   expect("(prefers-color-scheme: light)", T, F);
   expect("(prefers-color-scheme)", T, T);
   expect("(prefers-color-scheme: no-preference)", F, F);
   expect("(PREFERS-COLOR-SCHEME: DARK)", F, T);
   expect("screen and (prefers-color-scheme: dark)", F, T);
   expect("print and (prefers-color-scheme: dark)", F, F);
   expect("only screen and (prefers-color-scheme: dark)", F, T);
   expect("not all and (prefers-color-scheme: dark)", T, F);
   expect("not (prefers-color-scheme: dark)", T, F);
   expect("print, (prefers-color-scheme: dark)", F, T);
   expect("((prefers-color-scheme: dark))", F, T);
   expect("(prefers-color-scheme: dark) or (prefers-color-scheme: light)",
          T, T);

   /* features Dillo cannot evaluate stay unknown, unless decided anyway */
   expect("(max-width: 600px)", U, U);
   expect("screen and (min-width: 768px)", U, U);
   expect("(width >= 600px)", U, U);
   expect("(min-aspect-ratio: 16/9)", U, U);
   expect("(prefers-color-scheme: dark) and (min-width: 768px)", F, U);
   expect("(prefers-color-scheme: light) and (min-width: 768px)", U, F);
   expect("(max-width: 600px), (prefers-color-scheme: dark)", U, T);
   expect("print and (max-width: 600px)", F, F);
   expect("not (max-width: 600px)", U, U);

   /* malformed queries are false, but only that query in the list */
   expect("screen and", F, F);
   expect("(prefers-color-scheme: dark", F, T);
   expect("garbage garbage, (prefers-color-scheme: dark)", F, T);
   expect("screen { }", F, F);

   if (failures) {
      printf("%d failure(s)\n", failures);
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
