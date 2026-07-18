/*
 * Dillo iOS — minimal form-widget resources.
 *
 * The desktop embeds live FLTK widgets for form controls. iOS has no
 * native-widget layer yet, so these resources are display-only: they
 * occupy sensible space in the layout, store their state, and draw
 * nothing (Resource::draw is a no-op by default). This keeps pages with
 * forms — i.e. most of the web — from crashing; making them interactive
 * is a later milestone.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_APPLE_STUB_RESOURCES_HH__
#define __DW_APPLE_STUB_RESOURCES_HH__

#include "dw/core.hh"

namespace dw {
namespace apple {

namespace ui = dw::core::ui;

/* Shared bits: fixed-box sizing, empty iterator, enabled flag. */
template <class I> class StubResource: public I
{
private:
   bool enabled;
   int boxWidth, boxAscent, boxDescent;

public:
   StubResource (int width, int ascent, int descent) {
      enabled = true;
      boxWidth = width;
      boxAscent = ascent;
      boxDescent = descent;
   }

   void sizeRequest (core::Requisition *requisition) {
      requisition->width = boxWidth;
      requisition->ascent = boxAscent;
      requisition->descent = boxDescent;
   }

   core::Iterator *iterator (core::Content::Type mask, bool atEnd) {
      return new core::EmptyIterator (this->getEmbed (), mask, atEnd);
   }

   bool isEnabled () { return enabled; }
   void setEnabled (bool enabled) { this->enabled = enabled; }
};

class StubLabelButtonResource: public StubResource<ui::LabelButtonResource>
{
   char *label;
public:
   StubLabelButtonResource (const char *label);
   ~StubLabelButtonResource ();
   const char *getLabel ();
   void setLabel (const char *label);
};

class StubComplexButtonResource:
   public StubResource<ui::ComplexButtonResource>
{
public:
   StubComplexButtonResource (core::Widget *widget, bool relief);
   core::Platform *createPlatform ();
   void setLayout (core::Layout *layout);
   int reliefXThickness ();
   int reliefYThickness ();
};

class StubEntryResource: public StubResource<ui::EntryResource>
{
   char *text;
   bool editable;
public:
   StubEntryResource (int size, bool password, const char *label,
                      const char *placeholder);
   ~StubEntryResource ();
   const char *getText ();
   void setText (const char *text);
   bool isEditable ();
   void setEditable (bool editable);
   void setMaxLength (int maxlen);
};

class StubMultiLineTextResource:
   public StubResource<ui::MultiLineTextResource>
{
   char *text;
   bool editable;
public:
   StubMultiLineTextResource (int cols, int rows, const char *placeholder);
   ~StubMultiLineTextResource ();
   const char *getText ();
   void setText (const char *text);
   bool isEditable ();
   void setEditable (bool editable);
};

template <class I> class StubSelectionResource: public StubResource<I>
{
protected:
   lout::misc::SimpleVector <bool> *itemsSelected;
public:
   StubSelectionResource (int width, int ascent, int descent):
      StubResource<I> (width, ascent, descent) {
      itemsSelected = new lout::misc::SimpleVector <bool> (4);
   }
   ~StubSelectionResource () {
      delete itemsSelected;
   }

   void addItem (const char *str, bool enabled, bool selected) {
      itemsSelected->increase ();
      itemsSelected->set (itemsSelected->size () - 1, selected);
   }
   void setItem (int index, bool selected) {
      if (index >= 0 && index < itemsSelected->size ())
         itemsSelected->set (index, selected);
   }
   void pushGroup (const char *name, bool enabled) { }
   void popGroup () { }
   int getNumberOfItems () { return itemsSelected->size (); }
   bool isSelected (int index) {
      return index >= 0 && index < itemsSelected->size () ?
         itemsSelected->get (index) : false;
   }
};

class StubListResource: public StubSelectionResource<ui::ListResource>
{
public:
   StubListResource (ui::ListResource::SelectionMode selectionMode, int rows);
};

class StubOptionMenuResource:
   public StubSelectionResource<ui::OptionMenuResource>
{
public:
   StubOptionMenuResource ();
};

class StubCheckButtonResource: public StubResource<ui::CheckButtonResource>
{
   bool activated;
public:
   StubCheckButtonResource (bool activated);
   bool isActivated ();
   void setActivated (bool activated);
};

class StubRadioButtonResource: public StubResource<ui::RadioButtonResource>
{
   class StubGroupIterator: public GroupIterator
   {
   public:
      bool hasNext () { return false; }
      ui::RadioButtonResource *getNext () { return NULL; }
      void unref () { delete this; }
   };

   bool activated;
public:
   StubRadioButtonResource (StubRadioButtonResource *groupedWith,
                            bool activated);
   bool isActivated ();
   void setActivated (bool activated);
   GroupIterator *groupIterator ();
};

} // namespace apple
} // namespace dw

#endif // __DW_APPLE_STUB_RESOURCES_HH__
