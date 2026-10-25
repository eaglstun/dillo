/*
 * File: utf8_ios.cc
 *
 * iOS replacement for src/utf8.cc, which backs onto FLTK's fl_utf8*
 * functions. The decode logic is ported from FLTK's fl_utf8decode —
 * including its deliberate CP1252/ISO-8859-1 fallback for illegal bytes,
 * which the tag-soup HTML parser relies on — so behavior matches the
 * desktop build byte for byte.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../dlib/dlib.h"    /* TRUE/FALSE */
#include "../../src/utf8.hh"

// C++ functions with C linkage ----------------------------------------------

/* Unicode values for CP1252 bytes 0x80..0x9f (fl_utf8.c's table). */
static const unsigned short cp1252[32] = {
   0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
   0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
   0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
   0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
};

/**
 * Return index of the last byte of the UTF-8-encoded character that str + i
 * points to or into. (Same simpleminded logic as src/utf8.cc.)
 */
uint_t a_Utf8_end_of_char(const char *str, uint_t i)
{
   if (str && *str && (str[i] & 0x80)) {
      int internal_bytes = (str[i] & 0x40) ? 0 : 1;

      while (((str[i + 1] & 0xc0) == 0x80) && (++internal_bytes < 4))
         i++;
   }
   return i;
}

/**
 * Decode a single UTF-8-encoded character starting at p; ported from
 * FLTK's fl_utf8decode. Illegal sequences are interpreted as CP1252 /
 * ISO-8859-1 one-byte characters (len = 1), never rejected.
 */
uint_t a_Utf8_decode(const char* str, const char* end, int* len)
{
   const unsigned char *p = (const unsigned char*)str;
   unsigned char c = p[0];

   if (c < 0x80) {
      if (len) *len = 1;
      return c;
   } else if (c < 0xa0) {
      if (len) *len = 1;
      return cp1252[c - 0x80];
   } else if (c < 0xc2) {         /* stray continuation or overlong lead */
      goto FAIL;
   }

   if (str + 1 >= end || (p[1] & 0xc0) != 0x80)
      goto FAIL;

   if (c < 0xe0) {
      if (len) *len = 2;
      return ((c & 0x1f) << 6) + (p[1] & 0x3f);
   } else if (c == 0xe0 && p[1] < 0xa0) {   /* overlong 3-byte */
      goto FAIL;
   } else if (c < 0xf0) {
      if (str + 2 >= end || (p[2] & 0xc0) != 0x80)
         goto FAIL;
      if (len) *len = 3;
      return ((c & 0x0f) << 12) + ((p[1] & 0x3f) << 6) + (p[2] & 0x3f);
   } else if (c == 0xf0 && p[1] < 0x90) {   /* overlong 4-byte */
      goto FAIL;
   } else if (c < 0xf4 || (c == 0xf4 && p[1] <= 0x8f)) {
      if (str + 3 >= end || (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80)
         goto FAIL;
      if (len) *len = 4;
      return ((c & 0x07) << 18) + ((p[1] & 0x3f) << 12) +
             ((p[2] & 0x3f) << 6) + (p[3] & 0x3f);
   }

FAIL:
   if (len) *len = 1;
   return c;   /* ISO-8859-1 interpretation */
}

/**
 * Write UTF-8 encoding of ucs into buf and return number of bytes written.
 */
int a_Utf8_encode(unsigned int ucs, char *buf)
{
   if (ucs < 0x80U) {
      buf[0] = ucs;
      return 1;
   } else if (ucs < 0x800U) {
      buf[0] = 0xc0 | (ucs >> 6);
      buf[1] = 0x80 | (ucs & 0x3f);
      return 2;
   } else if (ucs < 0x10000U) {
      buf[0] = 0xe0 | (ucs >> 12);
      buf[1] = 0x80 | ((ucs >> 6) & 0x3f);
      buf[2] = 0x80 | (ucs & 0x3f);
      return 3;
   } else if (ucs <= 0x10ffffU) {
      buf[0] = 0xf0 | (ucs >> 18);
      buf[1] = 0x80 | ((ucs >> 12) & 0x3f);
      buf[2] = 0x80 | ((ucs >> 6) & 0x3f);
      buf[3] = 0x80 | (ucs & 0x3f);
      return 4;
   } else {
      /* out of range: encode U+FFFD REPLACEMENT CHARACTER */
      buf[0] = 0xef;
      buf[1] = 0xbf;
      buf[2] = 0xbd;
      return 3;
   }
}

/**
 * Examine first srclen bytes of src.
 * Return 0 if not legal UTF-8, 1 if all ASCII, 2 if all below 0x800,
 * 3 if all below 0x10000, and 4 otherwise. (fl_utf8test semantics.)
 */
int a_Utf8_test(const char* src, unsigned int srclen)
{
   int ret = 1;
   const char *p = src, *e = src + srclen;

   while (p < e) {
      if (*p & 0x80) {
         int len;
         a_Utf8_decode(p, e, &len);
         if (len < 2)
            return 0;   /* decode fell back to CP1252 → not legal UTF-8 */
         if (len > ret)
            ret = len;
         p += len;
      } else {
         p++;
      }
   }
   return ret;
}

/**
 * Does s point to a UTF-8-encoded ideographic character?
 */
bool_t a_Utf8_ideographic(const char *s, const char *end, int *len)
{
   bool_t ret = FALSE;

   if ((uchar_t)*s >= 0xe2) {
      /* Unicode char >= U+2000. */
      unsigned unicode = a_Utf8_decode(s, end, len);

      if (unicode >= 0x2e80 &&
           ((unicode <= 0xa4cf) ||
            (unicode >= 0xf900 && unicode <= 0xfaff) ||
            (unicode >= 0xff00 && unicode <= 0xff9f))) {
         ret = TRUE;
     }
   } else {
      *len = 1 + (int)a_Utf8_end_of_char(s, 0);
   }
   return ret;
}

bool_t a_Utf8_combining_char(int unicode)
{
   return ((unicode >= 0x0300 && unicode <= 0x036f) ||
           (unicode >= 0x1dc0 && unicode <= 0x1dff) ||
           (unicode >= 0x20d0 && unicode <= 0x20ff) ||
           (unicode >= 0xfe20 && unicode <= 0xfe2f));
}

int a_Utf8_char_count(const char *str, int len)
{
   int i = 0, nbc = 0;

   while (i < len) {
      /* step over one encoded char: lead byte plus continuations */
      i++;
      while (i < len && (str[i] & 0xc0) == 0x80)
         i++;
      nbc++;
   }
   return nbc;
}
