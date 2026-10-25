/*
 * Dillo iOS — minimal form-widget resources (see StubResources.hh).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <stdlib.h>

#include "StubResources.hh"
#include "AppleCoreTextPlatform.hh"
#include "CoreGraphicsView.hh"

namespace dw {
namespace apple {

static char *copyStr (const char *s)
{
   return s ? strdup (s) : strdup ("");
}

/* Rough desktop-ish control sizes (CSS px). */
enum { CTRL_ASCENT = 18, CTRL_DESCENT = 5 };

// ----------------------------------------------------------------------

StubLabelButtonResource::StubLabelButtonResource (const char *label):
   StubResource<ui::LabelButtonResource> (80, CTRL_ASCENT, CTRL_DESCENT)
{
   this->label = copyStr (label);
}

StubLabelButtonResource::~StubLabelButtonResource ()
{
   free (label);
}

const char *StubLabelButtonResource::getLabel ()
{
   return label;
}

void StubLabelButtonResource::setLabel (const char *label)
{
   free (this->label);
   this->label = copyStr (label);
}

// ----------------------------------------------------------------------

StubComplexButtonResource::StubComplexButtonResource (core::Widget *widget,
                                                      bool relief):
   StubResource<ui::ComplexButtonResource> (80, CTRL_ASCENT, CTRL_DESCENT)
{
   init (widget);
}

core::Platform *StubComplexButtonResource::createPlatform ()
{
   return new ApplePlatform ();
}

void StubComplexButtonResource::setLayout (core::Layout *layout)
{
   /* The button's inner layout needs a view (queueResize dereferences it);
    * a contextless CoreGraphicsView is exactly a "flat view" — every
    * operation no-ops until someone hands it a CGContext, which nothing
    * does. The layout's idle queue is likewise never drained. */
   layout->attachView (new CoreGraphicsView ());
}

int StubComplexButtonResource::reliefXThickness ()
{
   return 2;
}

int StubComplexButtonResource::reliefYThickness ()
{
   return 2;
}

// ----------------------------------------------------------------------

StubEntryResource::StubEntryResource (int size, bool password,
                                      const char *label,
                                      const char *placeholder):
   StubResource<ui::EntryResource> ((size > 0 ? size : 10) * 8,
                                    CTRL_ASCENT, CTRL_DESCENT)
{
   text = copyStr (NULL);
   editable = true;
}

StubEntryResource::~StubEntryResource ()
{
   free (text);
}

const char *StubEntryResource::getText ()
{
   return text;
}

void StubEntryResource::setText (const char *text)
{
   free (this->text);
   this->text = copyStr (text);
}

bool StubEntryResource::isEditable ()
{
   return editable;
}

void StubEntryResource::setEditable (bool editable)
{
   this->editable = editable;
}

void StubEntryResource::setMaxLength (int maxlen)
{
}

// ----------------------------------------------------------------------

StubMultiLineTextResource::StubMultiLineTextResource (int cols, int rows,
                                                      const char *placeholder):
   StubResource<ui::MultiLineTextResource> ((cols > 0 ? cols : 20) * 8,
                                            (rows > 0 ? rows : 3) *
                                            (CTRL_ASCENT + CTRL_DESCENT),
                                            CTRL_DESCENT)
{
   text = copyStr (NULL);
   editable = true;
}

StubMultiLineTextResource::~StubMultiLineTextResource ()
{
   free (text);
}

const char *StubMultiLineTextResource::getText ()
{
   return text;
}

void StubMultiLineTextResource::setText (const char *text)
{
   free (this->text);
   this->text = copyStr (text);
}

bool StubMultiLineTextResource::isEditable ()
{
   return editable;
}

void StubMultiLineTextResource::setEditable (bool editable)
{
   this->editable = editable;
}

// ----------------------------------------------------------------------

StubListResource::StubListResource (ui::ListResource::SelectionMode
                                    selectionMode, int rows):
   StubSelectionResource<ui::ListResource> (120,
                                            (rows > 0 ? rows : 3) *
                                            (CTRL_ASCENT + CTRL_DESCENT),
                                            CTRL_DESCENT)
{
}

StubOptionMenuResource::StubOptionMenuResource ():
   StubSelectionResource<ui::OptionMenuResource> (120, CTRL_ASCENT,
                                                  CTRL_DESCENT)
{
}

// ----------------------------------------------------------------------

StubCheckButtonResource::StubCheckButtonResource (bool activated):
   StubResource<ui::CheckButtonResource> (18, CTRL_ASCENT, CTRL_DESCENT)
{
   this->activated = activated;
}

bool StubCheckButtonResource::isActivated ()
{
   return activated;
}

void StubCheckButtonResource::setActivated (bool activated)
{
   this->activated = activated;
}

// ----------------------------------------------------------------------

StubRadioButtonResource::StubRadioButtonResource (StubRadioButtonResource
                                                  *groupedWith,
                                                  bool activated):
   StubResource<ui::RadioButtonResource> (18, CTRL_ASCENT, CTRL_DESCENT)
{
   this->activated = activated;
}

bool StubRadioButtonResource::isActivated ()
{
   return activated;
}

void StubRadioButtonResource::setActivated (bool activated)
{
   this->activated = activated;
}

ui::RadioButtonResource::GroupIterator *
StubRadioButtonResource::groupIterator ()
{
   return new StubGroupIterator ();
}

} // namespace apple
} // namespace dw
