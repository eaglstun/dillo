/*
 * Dillo Widget — Apple (Core Text) platform implementation.
 *
 * Structure modeled on dw/fltkplatform.cc; FLTK drawing/measurement calls
 * replaced by Core Text, the FLTK idle loop replaced by a queue the app
 * shell drains synchronously (ApplePlatform::runIdles).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "../../lout/msg.h"
#include "../../lout/misc.hh"
#include "AppleCoreTextPlatform.hh"

namespace dw {
namespace apple {

using namespace lout;

/*
 * UTF-8 helpers. Same codepoint-stepping semantics as FLTK's fl_utf8fwd /
 * fl_utf8back (which the engine's measurement loops were written against):
 * continuation bytes are 0x80..0xBF, malformed sequences advance one byte.
 */

static inline bool utf8IsContinuation (char c)
{
   return (c & 0xc0) == 0x80;
}

/* Decode the codepoint starting at text[i] (end exclusive). */
unsigned utf8Decode (const char *text, int i, int end)
{
   const unsigned char *p = (const unsigned char*)text + i;
   int left = end - i;

   if (left >= 1 && p[0] < 0x80)
      return p[0];
   if (left >= 2 && (p[0] & 0xe0) == 0xc0)
      return ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
   if (left >= 3 && (p[0] & 0xf0) == 0xe0)
      return ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
   if (left >= 4 && (p[0] & 0xf8) == 0xf0)
      return ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) |
             ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
   return left >= 1 ? p[0] : 0;
}

/* Encode a codepoint as UTF-8; returns the number of bytes written. */
int utf8Encode (unsigned c, char *buf)
{
   if (c < 0x80) {
      buf[0] = c;
      return 1;
   } else if (c < 0x800) {
      buf[0] = 0xc0 | (c >> 6);
      buf[1] = 0x80 | (c & 0x3f);
      return 2;
   } else if (c < 0x10000) {
      buf[0] = 0xe0 | (c >> 12);
      buf[1] = 0x80 | ((c >> 6) & 0x3f);
      buf[2] = 0x80 | (c & 0x3f);
      return 3;
   } else {
      buf[0] = 0xf0 | (c >> 18);
      buf[1] = 0x80 | ((c >> 12) & 0x3f);
      buf[2] = 0x80 | ((c >> 6) & 0x3f);
      buf[3] = 0x80 | (c & 0x3f);
      return 4;
   }
}

/* Case-transform a UTF-8 buffer with CFString (full Unicode folding);
 * returns a malloc'd, 0-terminated string like the FLTK implementation. */
static char *utf8Transform (const char *text, int len, bool toUpper)
{
   if (len <= 0)
      return NULL;

   CFStringRef s = CFStringCreateWithBytes (kCFAllocatorDefault,
                                            (const UInt8*)text, len,
                                            kCFStringEncodingUTF8, false);
   if (s == NULL) {
      /* invalid UTF-8: return a copy unchanged */
      char *copy = (char*) malloc (len + 1);
      memcpy (copy, text, len);
      copy[len] = '\0';
      return copy;
   }

   CFMutableStringRef m = CFStringCreateMutableCopy (kCFAllocatorDefault, 0, s);
   CFRelease (s);
   if (toUpper)
      CFStringUppercase (m, NULL);
   else
      CFStringLowercase (m, NULL);

   CFIndex maxLen =
      CFStringGetMaximumSizeForEncoding (CFStringGetLength (m),
                                         kCFStringEncodingUTF8) + 1;
   char *newstr = (char*) malloc (maxLen);
   if (!CFStringGetCString (m, newstr, maxLen, kCFStringEncodingUTF8))
      newstr[0] = '\0';
   CFRelease (m);
   return newstr;
}

// ----------------------------------------------------------------------

container::typed::HashTable <dw::core::style::FontAttrs,
                             AppleFont> *AppleFont::fontsTable =
   new container::typed::HashTable <dw::core::style::FontAttrs,
                                    AppleFont> (false, false);

static CTFontRef createCTFont (const char *name, double size, int weight,
                               core::style::FontStyle style)
{
   CFStringRef family =
      CFStringCreateWithCString (kCFAllocatorDefault, name,
                                 kCFStringEncodingUTF8);

   CTFontSymbolicTraits traits = 0;
   if (weight >= 500)
      traits |= kCTFontTraitBold;
   if (style != core::style::FONT_STYLE_NORMAL)
      traits |= kCTFontTraitItalic;

   CFMutableDictionaryRef attrs =
      CFDictionaryCreateMutable (kCFAllocatorDefault, 2,
                                 &kCFTypeDictionaryKeyCallBacks,
                                 &kCFTypeDictionaryValueCallBacks);
   CFDictionarySetValue (attrs, kCTFontFamilyNameAttribute, family);

   if (traits) {
      CFNumberRef traitsNum =
         CFNumberCreate (kCFAllocatorDefault, kCFNumberSInt32Type, &traits);
      CFMutableDictionaryRef traitsDict =
         CFDictionaryCreateMutable (kCFAllocatorDefault, 1,
                                    &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
      CFDictionarySetValue (traitsDict, kCTFontSymbolicTrait, traitsNum);
      CFDictionarySetValue (attrs, kCTFontTraitsAttribute, traitsDict);
      CFRelease (traitsDict);
      CFRelease (traitsNum);
   }

   CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes (attrs);
   CTFontRef font = CTFontCreateWithFontDescriptor (desc, size, NULL);

   CFRelease (desc);
   CFRelease (attrs);
   CFRelease (family);

   /* CTFontCreateWithFontDescriptor never returns NULL for a sane size; if
    * the family is unknown it falls back to the system font, which plays the
    * role FL_HELVETICA plays in the FLTK platform. */
   return font;
}

AppleFont::AppleFont (core::style::FontAttrs *attrs)
{
   copyAttrs (attrs);

   font = createCTFont (name, size, weight, style);
   smallCapsFont = NULL;

   spaceWidth = misc::max (0, (int)measureWidth (font, " ", 1)
                              + letterSpacing);
   zeroWidth = (int) measureWidth (font, "0", 1);
   xHeight = (int) (CTFontGetXHeight (font) + 0.5);
   descent = (int) (CTFontGetDescent (font) + 0.5);
   ascent = (int) (CTFontGetAscent (font) + 0.5);
}

AppleFont::~AppleFont ()
{
   if (font)
      CFRelease (font);
   if (smallCapsFont)
      CFRelease (smallCapsFont);
   fontsTable->remove (this);
}

CTFontRef AppleFont::getSmallCapsFont ()
{
   if (smallCapsFont == NULL) {
      int scSize = misc::roundInt (size * 0.78);
      smallCapsFont = createCTFont (name, scSize, weight, style);
   }
   return smallCapsFont;
}

double AppleFont::measureWidth (CTFontRef font, const char *text, int len)
{
   if (len <= 0)
      return 0.0;

   CFStringRef s = CFStringCreateWithBytes (kCFAllocatorDefault,
                                            (const UInt8*)text, len,
                                            kCFStringEncodingUTF8, false);
   if (s == NULL)
      return 0.0;

   const void *keys[] = { kCTFontAttributeName };
   const void *values[] = { font };
   CFDictionaryRef attrs =
      CFDictionaryCreate (kCFAllocatorDefault, keys, values, 1,
                          &kCFTypeDictionaryKeyCallBacks,
                          &kCFTypeDictionaryValueCallBacks);
   CFAttributedStringRef as =
      CFAttributedStringCreate (kCFAllocatorDefault, s, attrs);
   CTLineRef line = CTLineCreateWithAttributedString (as);

   double width = CTLineGetTypographicBounds (line, NULL, NULL, NULL);

   CFRelease (line);
   CFRelease (as);
   CFRelease (attrs);
   CFRelease (s);
   return width;
}

AppleFont *AppleFont::create (core::style::FontAttrs *attrs)
{
   AppleFont *font = fontsTable->get (attrs);

   if (font == NULL) {
      font = new AppleFont (attrs);
      fontsTable->put (font, font);
   }

   return font;
}

bool AppleFont::fontExists (const char *name)
{
   CTFontRef font = createCTFont (name, 12, 400,
                                  core::style::FONT_STYLE_NORMAL);
   CFStringRef requested =
      CFStringCreateWithCString (kCFAllocatorDefault, name,
                                 kCFStringEncodingUTF8);
   CFStringRef actual = CTFontCopyFamilyName (font);

   bool exists =
      CFStringCompare (requested, actual, kCFCompareCaseInsensitive)
      == kCFCompareEqualTo;

   CFRelease (actual);
   CFRelease (requested);
   CFRelease (font);
   return exists;
}

// ----------------------------------------------------------------------

container::typed::HashTable <dw::core::style::ColorAttrs,
                             AppleColor> *AppleColor::colorsTable =
   new container::typed::HashTable <dw::core::style::ColorAttrs,
                                    AppleColor> (false, false);

AppleColor::AppleColor (int color): Color (color)
{
   colors[SHADING_NORMAL] = shadeColor (color, SHADING_NORMAL);
   colors[SHADING_INVERSE] = shadeColor (color, SHADING_INVERSE);
   colors[SHADING_DARK] = shadeColor (color, SHADING_DARK);
   colors[SHADING_LIGHT] = shadeColor (color, SHADING_LIGHT);
}

AppleColor::~AppleColor ()
{
   colorsTable->remove (this);
}

AppleColor *AppleColor::create (int col)
{
   ColorAttrs attrs (col);
   AppleColor *color = colorsTable->get (&attrs);

   if (color == NULL) {
      color = new AppleColor (col);
      colorsTable->put (color, color);
   }

   return color;
}

// ----------------------------------------------------------------------

core::ui::LabelButtonResource *
ApplePlatform::StubResourceFactory::createLabelButtonResource (const char
                                                               *label)
{
   return NULL;
}

core::ui::ComplexButtonResource *
ApplePlatform::StubResourceFactory::createComplexButtonResource (core::Widget
                                                                 *widget,
                                                                 bool relief)
{
   return NULL;
}

core::ui::ListResource *
ApplePlatform::StubResourceFactory::createListResource (core::ui::ListResource
                                                        ::SelectionMode
                                                        selectionMode,
                                                        int rows)
{
   return NULL;
}

core::ui::OptionMenuResource *
ApplePlatform::StubResourceFactory::createOptionMenuResource ()
{
   return NULL;
}

core::ui::EntryResource *
ApplePlatform::StubResourceFactory::createEntryResource (int size,
                                                         bool password,
                                                         const char *label,
                                                         const char
                                                         *placeholder)
{
   return NULL;
}

core::ui::MultiLineTextResource *
ApplePlatform::StubResourceFactory::createMultiLineTextResource (int cols,
                                                                 int rows,
                                                                 const char
                                                                 *placeholder)
{
   return NULL;
}

core::ui::CheckButtonResource *
ApplePlatform::StubResourceFactory::createCheckButtonResource (bool activated)
{
   return NULL;
}

core::ui::RadioButtonResource *
ApplePlatform::StubResourceFactory::createRadioButtonResource (core::ui
                                                               ::RadioButtonResource
                                                               *groupedWith,
                                                               bool activated)
{
   return NULL;
}

// ----------------------------------------------------------------------

ApplePlatform::ApplePlatform ()
{
   layout = NULL;
   view = NULL;
   idleQueue = new container::typed::List <IdleFunc> (true);
   idleFuncId = 0;
}

ApplePlatform::~ApplePlatform ()
{
   delete idleQueue;
}

void ApplePlatform::setLayout (core::Layout *layout)
{
   this->layout = layout;
}

void ApplePlatform::attachView (core::View *view)
{
   if (this->view)
      MSG_ERR("ApplePlatform::attachView: multiple views!\n");
   this->view = view;
}

void ApplePlatform::detachView (core::View *view)
{
   if (this->view != view)
      MSG_ERR("ApplePlatform::detachView: this->view: %p view: %p\n",
              (void *) this->view, (void *) view);
   this->view = NULL;
}

int ApplePlatform::textWidth (core::style::Font *font, const char *text,
                              int len)
{
   AppleFont *af = (AppleFont*) font;
   int width = 0;

   if (font->fontVariant == core::style::FONT_VARIANT_SMALL_CAPS) {
      /* Mirrors the FLTK implementation: lowercase glyphs are drawn as
       * uppercase at 0.78 size, so they must be measured that way too. */
      char chbuf[4];
      int curr = 0, next = 0, nb;
      unsigned c, cu;

      for (curr = 0; next < len; curr = next) {
         next = nextGlyph (text, curr);
         c = utf8Decode (text, curr, next);
         if ((cu = towupper (c)) == c) {
            width += font->letterSpacing;
            width += (int)AppleFont::measureWidth (af->font, text + curr, next - curr);
         } else {
            nb = utf8Encode (cu, chbuf);
            width += font->letterSpacing;
            width += (int)AppleFont::measureWidth (af->getSmallCapsFont (), chbuf, nb);
         }
      }
   } else {
      width = (int) AppleFont::measureWidth (af->font, text, len);

      if (font->letterSpacing) {
         int curr = 0, next = 0;

         while (next < len) {
            next = nextGlyph (text, curr);
            width += font->letterSpacing;
            curr = next;
         }
      }
   }

   return width;
}

char *ApplePlatform::textToUpper (const char *text, int len)
{
   return utf8Transform (text, len, true);
}

char *ApplePlatform::textToLower (const char *text, int len)
{
   return utf8Transform (text, len, false);
}

int ApplePlatform::nextGlyph (const char *text, int idx)
{
   int end = strlen (text);
   int i = idx + 1;

   if (i >= end)
      return end;
   while (i < end && utf8IsContinuation (text[i]))
      i++;
   return i;
}

int ApplePlatform::prevGlyph (const char *text, int idx)
{
   int i = idx - 1;

   if (i <= 0)
      return 0;
   while (i > 0 && utf8IsContinuation (text[i]))
      i--;
   return i;
}

/*
 * Dillo's CSS engine (not part of the spike) converts pt/em sizes to px with
 * these; a "px" font size like the spike's reaches Core Text unscaled either
 * way. 96 matches the CSS reference pixel.
 */
float ApplePlatform::dpiX ()
{
   return 96.0f;
}

float ApplePlatform::dpiY ()
{
   return 96.0f;
}

int ApplePlatform::addIdle (void (core::Layout::*func) ())
{
   idleFuncId++;

   IdleFunc *idleFunc = new IdleFunc ();
   idleFunc->id = idleFuncId;
   idleFunc->func = func;
   idleQueue->append (idleFunc);

   return idleFuncId;
}

void ApplePlatform::removeIdle (int idleId)
{
   bool found;
   container::typed::Iterator <IdleFunc> it;
   IdleFunc *idleFunc;

   for (found = false, it = idleQueue->iterator (); !found && it.hasNext (); ) {
      idleFunc = it.getNext ();
      if (idleFunc->id == idleId) {
         idleQueue->removeRef (idleFunc);
         found = true;
      }
   }
}

void ApplePlatform::runIdles ()
{
   /* An idle may queue further idles (resizeIdle does); keep draining.
    * The engine's own resizeCounter guards against true runaway loops. */
   while (!idleQueue->isEmpty ()) {
      IdleFunc *idleFunc = idleQueue->getFirst ();
      void (core::Layout::*func) () = idleFunc->func;
      idleQueue->removeRef (idleFunc);
      (layout->*func) ();
   }
}

core::style::Font *ApplePlatform::createFont (core::style::FontAttrs *attrs,
                                              bool tryEverything)
{
   return AppleFont::create (attrs);
}

bool ApplePlatform::fontExists (const char *name)
{
   return AppleFont::fontExists (name);
}

core::style::Color *ApplePlatform::createColor (int color)
{
   return AppleColor::create (color);
}

core::style::Tooltip *ApplePlatform::createTooltip (const char *text)
{
   return NULL;   /* no tooltips on a touch screen; unused in the spike */
}

void ApplePlatform::cancelTooltip ()
{
}

core::Imgbuf *ApplePlatform::createImgbuf (core::Imgbuf::Type type,
                                           int width, int height, double gamma)
{
   return NULL;   /* spike is text-only; images are the next milestone */
}

void ApplePlatform::copySelection (const char *text, int destination)
{
}

core::ui::ResourceFactory *ApplePlatform::getResourceFactory ()
{
   return &resourceFactory;
}

} // namespace apple
} // namespace dw
