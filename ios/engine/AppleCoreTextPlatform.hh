/*
 * Dillo Widget — Apple (Core Text) platform implementation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_APPLE_CORETEXT_PLATFORM_HH__
#define __DW_APPLE_CORETEXT_PLATFORM_HH__

#include <CoreText/CoreText.h>

#include "dw/core.hh"

namespace dw {

/**
 * \brief Apple (iOS / Core Text / Core Graphics) implementations of the Dw
 *    interfaces. The engine-side counterpart of dw::fltk.
 */
namespace apple {

/* UTF-8 codepoint helpers, shared by platform (measurement) and view
 * (painting) so the two can never disagree on glyph boundaries. */
unsigned utf8Decode (const char *text, int i, int end);
int utf8Encode (unsigned c, char *buf);

class AppleFont: public core::style::Font
{
   static lout::container::typed::HashTable <dw::core::style::FontAttrs,
                                             AppleFont> *fontsTable;

   AppleFont (core::style::FontAttrs *attrs);
   ~AppleFont ();

   CTFontRef smallCapsFont;   /* lazily created, size * 0.78 */

public:
   CTFontRef font;

   static AppleFont *create (core::style::FontAttrs *attrs);
   static bool fontExists (const char *name);

   CTFontRef getSmallCapsFont ();

   /**
    * \brief Measure the advance width of a UTF-8 run with the given CTFont.
    *
    * Both ApplePlatform::textWidth and CoreGraphicsView::drawText go through
    * this, so measurement and painting can never disagree (risk #1 in the
    * spike plan).
    */
   static double measureWidth (CTFontRef font, const char *text, int len);
};


class AppleColor: public core::style::Color
{
   static lout::container::typed::HashTable <dw::core::style::ColorAttrs,
                                             AppleColor> *colorsTable;

   AppleColor (int color);
   ~AppleColor ();

public:
   int colors[SHADING_NUM];   /* 0xrrggbb per shading */

   static AppleColor *create (int color);
};


class ApplePlatform: public core::Platform
{
private:
   class StubResourceFactory: public core::ui::ResourceFactory
   {
   public:
      core::ui::LabelButtonResource *createLabelButtonResource (const char
                                                                *label);
      core::ui::ComplexButtonResource *
      createComplexButtonResource (core::Widget *widget, bool relief);
      core::ui::ListResource *
      createListResource (core::ui::ListResource::SelectionMode selectionMode,
                          int rows);
      core::ui::OptionMenuResource *createOptionMenuResource ();
      core::ui::EntryResource *createEntryResource (int size, bool password,
                                                    const char *label,
                                                    const char *placeholder);
      core::ui::MultiLineTextResource *createMultiLineTextResource (int cols,
                                                                    int rows,
                                                      const char *placeholder);
      core::ui::CheckButtonResource *createCheckButtonResource (bool
                                                                activated);
      core::ui::RadioButtonResource *
      createRadioButtonResource (core::ui::RadioButtonResource
                                 *groupedWith, bool activated);
   };

   StubResourceFactory resourceFactory;

   class IdleFunc: public lout::object::Object
   {
   public:
      int id;
      void (core::Layout::*func) ();
   };

   core::Layout *layout;
   core::View *view;

   lout::container::typed::List <IdleFunc> *idleQueue;
   int idleFuncId;

public:
   ApplePlatform ();
   ~ApplePlatform ();

   /**
    * \brief Synchronously drain the idle queue (Layout::resizeIdle etc.).
    *
    * There is no toolkit event loop in the spike; the bridge calls this
    * after mutating the widget tree and before painting.
    */
   void runIdles ();

   void setLayout (core::Layout *layout);

   void attachView (core::View *view);
   void detachView (core::View *view);

   int textWidth (core::style::Font *font, const char *text, int len);
   char *textToUpper (const char *text, int len);
   char *textToLower (const char *text, int len);
   int nextGlyph (const char *text, int idx);
   int prevGlyph (const char *text, int idx);
   float dpiX ();
   float dpiY ();

   int addIdle (void (core::Layout::*func) ());
   void removeIdle (int idleId);

   core::style::Font *createFont (core::style::FontAttrs *attrs,
                                  bool tryEverything);
   bool fontExists (const char *name);
   core::style::Color *createColor (int color);
   core::style::Tooltip *createTooltip (const char *text);
   void cancelTooltip ();

   core::Imgbuf *createImgbuf (core::Imgbuf::Type type, int width, int height,
                               double gamma);

   void copySelection (const char *text, int destination);

   core::ui::ResourceFactory *getResourceFactory ();
};

} // namespace apple
} // namespace dw

#endif // __DW_APPLE_CORETEXT_PLATFORM_HH__
