/*
 * Dillo iOS — UIKit-backed form-widget resources.
 *
 * The iOS counterpart of dw/fltkui.cc: each dw::core::ui::Resource owns a
 * real UIControl, added to the host view (the browser's scroll view, whose
 * content coordinates are canvas coordinates — so controls track the page
 * during scrolling with no extra work). Engine layout drives geometry via
 * sizeRequest/sizeAllocate; user interaction flows back through the
 * engine's own signal emitters (emitClicked/emitActivate), which is what
 * src/form.cc listens to for submission.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import "NativeFormResources.h"

#include <string>
#include <vector>
#include <string.h>

#include "AppleCoreTextPlatform.hh"
#include "CoreGraphicsView.hh"

namespace ui = dw::core::ui;
namespace core = dw::core;

/* Bridges UIControl target-action to a C++ callback. */
@interface FormControlTarget : NSObject
@property (nonatomic, copy) void (^onAction)(void);
- (void)fire;
@end

@implementation FormControlTarget
- (void)fire
{
   if (self.onAction)
      self.onAction ();
}
@end

namespace dw {
namespace ios {

static const int CTRL_ASCENT = 18, CTRL_DESCENT = 5;
static const CGFloat CTRL_FONT_SIZE = 14;

static void fillClickEvent (core::EventButton *event)
{
   event->xCanvas = event->yCanvas = -1;
   event->state = (core::ButtonState) 0;
   event->button = 1;
   event->numPressed = 1;
}

/*
 * Shared base: geometry from the engine, a UIView on the host, an
 * enabled flag. The control is hidden until the first sizeAllocate so it
 * never flashes at (0,0).
 */
template <class I> class NativeResource: public I
{
private:
   bool enabled;

protected:
   UIView *host;
   UIView *control;
   FormControlTarget *target;
   int reqWidth, reqAscent, reqDescent;

   void attachControl (UIView *c)
   {
      control = c;
      control.hidden = YES;
      [host addSubview:control];
   }

public:
   NativeResource (UIView *host, int width, int ascent, int descent)
   {
      this->host = host;
      control = nil;
      target = nil;
      enabled = true;
      reqWidth = width;
      reqAscent = ascent;
      reqDescent = descent;
   }

   ~NativeResource ()
   {
      [control removeFromSuperview];
      control = nil;
      target = nil;
   }

   void sizeRequest (core::Requisition *requisition)
   {
      requisition->width = reqWidth;
      requisition->ascent = reqAscent;
      requisition->descent = reqDescent;
   }

   void sizeAllocate (core::Allocation *allocation)
   {
      if (control) {
         control.frame = CGRectMake (allocation->x, allocation->y,
                                     allocation->width,
                                     allocation->ascent +
                                     allocation->descent);
         control.hidden = NO;
      }
   }

   void setDisplayed (bool displayed)
   {
      if (control)
         control.hidden = displayed ? NO : YES;
   }

   core::Iterator *iterator (core::Content::Type mask, bool atEnd)
   {
      return new core::EmptyIterator (this->getEmbed (), mask, atEnd);
   }

   bool isEnabled () { return enabled; }

   void setEnabled (bool enabled)
   {
      this->enabled = enabled;
      if (control) {
         control.userInteractionEnabled = enabled ? YES : NO;
         control.alpha = enabled ? 1.0 : 0.5;
      }
   }
};

// ----------------------------------------------------------------------

class NativeEntryResource: public NativeResource<ui::EntryResource>
{
   mutable std::string textBuf;

   UITextField *field () { return (UITextField *) control; }

public:
   NativeEntryResource (UIView *host, int size, bool password,
                        const char *label, const char *placeholder):
      NativeResource<ui::EntryResource> (host, (size > 0 ? size : 10) * 8,
                                         CTRL_ASCENT, CTRL_DESCENT)
   {
      UITextField *f = [[UITextField alloc] init];
      f.font = [UIFont systemFontOfSize:CTRL_FONT_SIZE];
      f.borderStyle = UITextBorderStyleRoundedRect;
      f.autocapitalizationType = UITextAutocapitalizationTypeNone;
      f.autocorrectionType = UITextAutocorrectionTypeNo;
      f.secureTextEntry = password ? YES : NO;
      f.returnKeyType = UIReturnKeyGo;
      if (placeholder)
         f.placeholder = [NSString stringWithUTF8String:placeholder];

      target = [FormControlTarget new];
      NativeEntryResource *self_ = this;
      target.onAction = ^{ self_->fireActivate (); };
      /* "Did end on exit" = return key; form.cc submits single-entry
       * forms on activate, so search boxes behave like a browser. */
      [f addTarget:target action:@selector(fire)
          forControlEvents:UIControlEventEditingDidEndOnExit];

      attachControl (f);
   }

   /* Blocks have no access to protected members; public trampoline. */
   void fireActivate () { emitActivate (); }

   const char *getText ()
   {
      textBuf = field ().text ? field ().text.UTF8String : "";
      return textBuf.c_str ();
   }

   void setText (const char *text)
   {
      field ().text = [NSString stringWithUTF8String:(text ? text : "")];
   }

   bool isEditable () { return true; }
   void setEditable (bool editable) { }
   void setMaxLength (int maxlen) { }
};

// ----------------------------------------------------------------------

class NativeMultiLineTextResource:
   public NativeResource<ui::MultiLineTextResource>
{
   mutable std::string textBuf;

   UITextView *view () { return (UITextView *) control; }

public:
   NativeMultiLineTextResource (UIView *host, int cols, int rows,
                                const char *placeholder):
      NativeResource<ui::MultiLineTextResource>
         (host, (cols > 0 ? cols : 20) * 8,
          (rows > 0 ? rows : 3) * (CTRL_ASCENT + CTRL_DESCENT),
          CTRL_DESCENT)
   {
      UITextView *v = [[UITextView alloc] init];
      v.font = [UIFont systemFontOfSize:CTRL_FONT_SIZE];
      v.layer.borderWidth = 1;
      v.layer.borderColor = [UIColor systemGray3Color].CGColor;
      v.layer.cornerRadius = 5;
      v.autocapitalizationType = UITextAutocapitalizationTypeNone;
      v.autocorrectionType = UITextAutocorrectionTypeNo;
      attachControl (v);
   }

   const char *getText ()
   {
      textBuf = view ().text ? view ().text.UTF8String : "";
      return textBuf.c_str ();
   }

   void setText (const char *text)
   {
      view ().text = [NSString stringWithUTF8String:(text ? text : "")];
   }

   bool isEditable () { return true; }
   void setEditable (bool editable) { }
};

// ----------------------------------------------------------------------

class NativeLabelButtonResource:
   public NativeResource<ui::LabelButtonResource>
{
   std::string label;

   UIButton *button () { return (UIButton *) control; }

   static int widthForLabel (const char *label)
   {
      NSString *s = [NSString stringWithUTF8String:(label ? label : "")];
      CGSize size = [s sizeWithAttributes:
         @{NSFontAttributeName: [UIFont systemFontOfSize:CTRL_FONT_SIZE]}];
      return (int) size.width + 24;
   }

public:
   NativeLabelButtonResource (UIView *host, const char *label):
      NativeResource<ui::LabelButtonResource> (host, widthForLabel (label),
                                               CTRL_ASCENT, CTRL_DESCENT)
   {
      this->label = label ? label : "";

      UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
      UIButtonConfiguration *conf =
         [UIButtonConfiguration grayButtonConfiguration];
      conf.title = [NSString stringWithUTF8String:this->label.c_str ()];
      conf.contentInsets = NSDirectionalEdgeInsetsMake (2, 8, 2, 8);
      b.configuration = conf;
      b.titleLabel.font = [UIFont systemFontOfSize:CTRL_FONT_SIZE];

      target = [FormControlTarget new];
      NativeLabelButtonResource *self_ = this;
      target.onAction = ^{ self_->fireClicked (); };
      [b addTarget:target action:@selector(fire)
          forControlEvents:UIControlEventTouchUpInside];

      attachControl (b);
   }

   void fireClicked ()
   {
      core::EventButton event;
      fillClickEvent (&event);
      emitClicked (&event);
   }

   const char *getLabel () { return label.c_str (); }

   void setLabel (const char *label)
   {
      this->label = label ? label : "";
      UIButtonConfiguration *conf = button ().configuration;
      conf.title = [NSString stringWithUTF8String:this->label.c_str ()];
      button ().configuration = conf;
   }
};

// ----------------------------------------------------------------------

class NativeComplexButtonResource:
   public NativeResource<ui::ComplexButtonResource>
{
public:
   NativeComplexButtonResource (UIView *host, core::Widget *widget,
                                bool relief):
      NativeResource<ui::ComplexButtonResource> (host, 80, CTRL_ASCENT,
                                                 CTRL_DESCENT)
   {
      UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
      UIButtonConfiguration *conf = relief ?
         [UIButtonConfiguration grayButtonConfiguration] :
         [UIButtonConfiguration plainButtonConfiguration];
      b.configuration = conf;

      target = [FormControlTarget new];
      NativeComplexButtonResource *self_ = this;
      target.onAction = ^{ self_->fireClicked (); };
      [b addTarget:target action:@selector(fire)
          forControlEvents:UIControlEventTouchUpInside];

      attachControl (b);
      init (widget);
   }

   void fireClicked ()
   {
      core::EventButton event;
      fillClickEvent (&event);
      emitClicked (&event);
   }

   core::Platform *createPlatform ()
   {
      return new dw::apple::ApplePlatform ();
   }

   void setLayout (core::Layout *layout)
   {
      /* The child-content layout still needs a (flat, contextless) view;
       * the button itself is native and the HTML content inside it is
       * not rendered yet (same as the previous stub). */
      layout->attachView (new dw::apple::CoreGraphicsView ());
   }

   int reliefXThickness () { return 2; }
   int reliefYThickness () { return 2; }
};

// ----------------------------------------------------------------------

class NativeCheckButtonResource:
   public NativeResource<ui::CheckButtonResource>
{
   bool activated;

   void updateImage ()
   {
      UIButton *b = (UIButton *) control;
      NSString *name = activated ? @"checkmark.square.fill" : @"square";
      [b setImage:[UIImage systemImageNamed:name]
          forState:UIControlStateNormal];
   }

public:
   NativeCheckButtonResource (UIView *host, bool activated):
      NativeResource<ui::CheckButtonResource> (host, 24, CTRL_ASCENT,
                                               CTRL_DESCENT)
   {
      this->activated = activated;

      UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
      target = [FormControlTarget new];
      NativeCheckButtonResource *self_ = this;
      target.onAction = ^{ self_->toggle (); };
      [b addTarget:target action:@selector(fire)
          forControlEvents:UIControlEventTouchUpInside];

      attachControl (b);
      updateImage ();
   }

   void toggle ()
   {
      activated = !activated;
      updateImage ();
   }

   bool isActivated () { return activated; }

   void setActivated (bool activated)
   {
      this->activated = activated;
      updateImage ();
   }
};

// ----------------------------------------------------------------------

class NativeRadioButtonResource:
   public NativeResource<ui::RadioButtonResource>
{
   /* All radios of one group share this vector (form.cc walks the group
    * via groupIterator to collect values and enforce exclusivity). */
   typedef std::vector<NativeRadioButtonResource *> Group;

   Group *group;
   bool activated;

   void updateImage ()
   {
      UIButton *b = (UIButton *) control;
      NSString *name = activated ? @"inset.filled.circle" : @"circle";
      [b setImage:[UIImage systemImageNamed:name]
          forState:UIControlStateNormal];
   }

   void activateExclusively ()
   {
      for (size_t i = 0; i < group->size (); i++) {
         NativeRadioButtonResource *r = (*group)[i];
         bool a = (r == this);
         if (r->activated != a) {
            r->activated = a;
            r->updateImage ();
         }
      }
   }

   class NativeGroupIterator: public GroupIterator
   {
      Group *group;
      size_t index;
   public:
      NativeGroupIterator (Group *group) { this->group = group; index = 0; }
      bool hasNext () { return index < group->size (); }
      ui::RadioButtonResource *getNext () { return (*group)[index++]; }
      void unref () { delete this; }
   };

public:
   NativeRadioButtonResource (UIView *host,
                              NativeRadioButtonResource *groupedWith,
                              bool activated):
      NativeResource<ui::RadioButtonResource> (host, 24, CTRL_ASCENT,
                                               CTRL_DESCENT)
   {
      this->activated = activated;
      group = groupedWith ? groupedWith->group : new Group ();
      group->push_back (this);

      UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
      target = [FormControlTarget new];
      NativeRadioButtonResource *self_ = this;
      target.onAction = ^{ self_->pick (); };
      [b addTarget:target action:@selector(fire)
          forControlEvents:UIControlEventTouchUpInside];

      attachControl (b);
      updateImage ();
   }

   ~NativeRadioButtonResource ()
   {
      for (size_t i = 0; i < group->size (); i++)
         if ((*group)[i] == this) {
            group->erase (group->begin () + i);
            break;
         }
      if (group->empty ())
         delete group;
   }

   void pick () { activateExclusively (); }

   bool isActivated () { return activated; }

   void setActivated (bool activated)
   {
      if (activated)
         activateExclusively ();
      else {
         this->activated = false;
         updateImage ();
      }
   }

   GroupIterator *groupIterator ()
   {
      return new NativeGroupIterator (group);
   }
};

// ----------------------------------------------------------------------

/*
 * <select> as a UIButton with a UIMenu. Handles both the dropdown
 * (OptionMenu) and the list (ListResource) variants; multiple selection
 * is kept in our own state (UIMenu shows checkmarks via UIActionState).
 */
template <class I> class NativeSelectionResource: public NativeResource<I>
{
protected:
   struct Item {
      std::string title;
      bool enabled;
      bool selected;
   };
   std::vector<Item> items;
   bool multiple;

   UIButton *button () { return (UIButton *) this->control; }

   void rebuildMenu ()
   {
      NSMutableArray<UIAction *> *actions = [NSMutableArray array];
      for (size_t i = 0; i < items.size (); i++) {
         NativeSelectionResource<I> *self_ = this;
         size_t index = i;
         UIAction *action = [UIAction
            actionWithTitle:[NSString
                               stringWithUTF8String:items[i].title.c_str ()]
            image:nil identifier:nil
            handler:^(UIAction *a) { self_->itemPicked (index); }];
         if (items[i].selected)
            action.state = UIMenuElementStateOn;
         if (!items[i].enabled)
            action.attributes = UIMenuElementAttributesDisabled;
         [actions addObject:action];
      }
      button ().menu = [UIMenu menuWithChildren:actions];
      updateTitle ();
   }

   void updateTitle ()
   {
      const char *title = "\xe2\x80\x94";   /* em dash: nothing selected */
      for (size_t i = 0; i < items.size (); i++)
         if (items[i].selected) {
            title = items[i].title.c_str ();
            break;
         }
      UIButtonConfiguration *conf = button ().configuration;
      conf.title = [NSString stringWithUTF8String:title];
      button ().configuration = conf;
   }

public:
   void itemPicked (size_t index)
   {
      if (multiple) {
         items[index].selected = !items[index].selected;
      } else {
         for (size_t i = 0; i < items.size (); i++)
            items[i].selected = (i == index);
      }
      rebuildMenu ();
   }
   NativeSelectionResource (UIView *host, bool multiple):
      NativeResource<I> (host, 140, CTRL_ASCENT, CTRL_DESCENT)
   {
      this->multiple = multiple;

      UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
      UIButtonConfiguration *conf =
         [UIButtonConfiguration grayButtonConfiguration];
      conf.title = @"—";
      conf.indicator = UIButtonConfigurationIndicatorPopup;
      conf.contentInsets = NSDirectionalEdgeInsetsMake (2, 8, 2, 8);
      b.configuration = conf;
      b.titleLabel.font = [UIFont systemFontOfSize:CTRL_FONT_SIZE];
      b.showsMenuAsPrimaryAction = YES;

      this->attachControl (b);
   }

   void addItem (const char *str, bool enabled, bool selected)
   {
      Item item;
      item.title = str ? str : "";
      item.enabled = enabled;
      item.selected = selected;
      items.push_back (item);
      rebuildMenu ();
   }

   void setItem (int index, bool selected)
   {
      if (index >= 0 && (size_t) index < items.size ()) {
         items[index].selected = selected;
         rebuildMenu ();
      }
   }

   void pushGroup (const char *name, bool enabled) { }
   void popGroup () { }

   int getNumberOfItems () { return (int) items.size (); }

   bool isSelected (int index)
   {
      return index >= 0 && (size_t) index < items.size () ?
         items[index].selected : false;
   }
};

class NativeListResource:
   public NativeSelectionResource<ui::ListResource>
{
public:
   NativeListResource (UIView *host,
                       ui::ListResource::SelectionMode selectionMode,
                       int rows):
      NativeSelectionResource<ui::ListResource>
         (host, selectionMode == ui::ListResource::SELECTION_MULTIPLE)
   {
   }
};

class NativeOptionMenuResource:
   public NativeSelectionResource<ui::OptionMenuResource>
{
public:
   NativeOptionMenuResource (UIView *host):
      NativeSelectionResource<ui::OptionMenuResource> (host, false)
   {
   }
};

// ----------------------------------------------------------------------

class NativeResourceFactory: public ui::ResourceFactory
{
   UIView *host;

public:
   NativeResourceFactory (UIView *host) { this->host = host; }

   ui::LabelButtonResource *createLabelButtonResource (const char *label)
   {
      return new NativeLabelButtonResource (host, label);
   }

   ui::ComplexButtonResource *
   createComplexButtonResource (core::Widget *widget, bool relief)
   {
      return new NativeComplexButtonResource (host, widget, relief);
   }

   ui::ListResource *
   createListResource (ui::ListResource::SelectionMode selectionMode,
                       int rows)
   {
      return new NativeListResource (host, selectionMode, rows);
   }

   ui::OptionMenuResource *createOptionMenuResource ()
   {
      return new NativeOptionMenuResource (host);
   }

   ui::EntryResource *createEntryResource (int size, bool password,
                                           const char *label,
                                           const char *placeholder)
   {
      return new NativeEntryResource (host, size, password, label,
                                      placeholder);
   }

   ui::MultiLineTextResource *
   createMultiLineTextResource (int cols, int rows, const char *placeholder)
   {
      return new NativeMultiLineTextResource (host, cols, rows, placeholder);
   }

   ui::CheckButtonResource *createCheckButtonResource (bool activated)
   {
      return new NativeCheckButtonResource (host, activated);
   }

   ui::RadioButtonResource *
   createRadioButtonResource (ui::RadioButtonResource *groupedWith,
                              bool activated)
   {
      return new NativeRadioButtonResource
         (host, (NativeRadioButtonResource *) groupedWith, activated);
   }
};

core::ui::ResourceFactory *createNativeResourceFactory (UIView *hostView)
{
   return new NativeResourceFactory (hostView);
}

void destroyNativeResourceFactory (core::ui::ResourceFactory *factory)
{
   delete (NativeResourceFactory *) factory;
}

} // namespace ios
} // namespace dw
