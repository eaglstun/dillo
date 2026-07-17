/*
 * Dillo iOS spike — Objective-C bridge around the dw engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

/// Owns a dw::core::Layout with an ApplePlatform and a CoreGraphicsView,
/// builds the dw_example test page (ten wrapping paragraphs), and paints it
/// into whatever CGContext the canvas view hands over.
@interface SpikeBridge : NSObject

/// Build the layout for a canvas of the given width (points). Wrapping
/// happens against this width. Height is only an initial viewport hint.
- (instancetype)initWithViewportWidth:(int)width height:(int)height;

/// Canvas size the engine settled on after layouting (height grows with
/// the content; width normally equals the viewport width).
@property (nonatomic, readonly) int canvasWidth;
@property (nonatomic, readonly) int canvasHeight;

/// Called when the engine queues a redraw (e.g. from a late resize).
@property (nonatomic, copy, nullable) void (^onInvalidate)(void);

/// Paint the full canvas into a UIKit-oriented context (top-left origin),
/// as obtained inside -[UIView drawRect:].
- (void)drawIntoContext:(CGContextRef)ctx;

@end

NS_ASSUME_NONNULL_END
