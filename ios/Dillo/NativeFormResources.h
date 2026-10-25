/*
 * Dillo iOS — UIKit-backed form-widget resources (factory entry points).
 *
 * ObjC++-only header: include from .mm files.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DILLO_NATIVE_FORM_RESOURCES_H__
#define __DILLO_NATIVE_FORM_RESOURCES_H__

#import <UIKit/UIKit.h>

#include "dw/core.hh"

namespace dw {
namespace ios {

/**
 * Create a ResourceFactory whose resources own real UIControls, added as
 * subviews of hostView (which must live in canvas coordinate space — in
 * this app, the browser's UIScrollView, whose content coords are canvas
 * coords). Destroy with destroyNativeResourceFactory after the layout
 * (and thus every resource) is gone.
 */
core::ui::ResourceFactory *createNativeResourceFactory (UIView *hostView);
void destroyNativeResourceFactory (core::ui::ResourceFactory *factory);

/**
 * Re-emit the click of the last label button the user tapped (used by the
 * async file picker: the second click finds the picked path waiting).
 * Safe no-op if that resource is gone (page navigated).
 */
void refireLastLabelClick ();

} // namespace ios
} // namespace dw

#endif
