/*
 * Dillo iOS — Objective-C bridge around the dw engine + parser.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

/// Owns one dw::core::Layout (ApplePlatform + CoreGraphicsView) — the iOS
/// equivalent of a desktop browser window — and creates a fresh DilloHtml
/// per page load. The Swift shell fetches; the bridge parses and paints.
@interface DilloBridge : NSObject

- (instancetype)initWithViewportWidth:(int)width height:(int)height;

/// Engine-computed canvas size after layout (height grows with content).
@property (nonatomic, readonly) int canvasWidth;
@property (nonatomic, readonly) int canvasHeight;

/// Engine queued a redraw (reflow, late resize). Also a good moment for the
/// shell to re-read canvasHeight.
@property (nonatomic, copy, nullable) void (^onInvalidate)(void);

/// A link was activated (tap on <a>). Absolute URL string.
@property (nonatomic, copy, nullable) void (^onOpenURL)(NSString *url);

/// The parser saw <title>.
@property (nonatomic, copy, nullable) void (^onTitle)(NSString *title);

/// Resolve href against a base URL with dillo's own url.c (so cache keys
/// match what the parser computes for the same href). Returns absolute URL.
- (nullable NSString *)resolveURL:(NSString *)href baseURL:(NSString *)base;

/// Drop all cached subresources (start of a page load).
- (void)clearCache;

/// Seed a fetched stylesheet (UTF-8 text) under its absolute URL.
- (void)seedCSS:(NSString *)cssText forURL:(NSString *)url;

/// Parse and lay out a UTF-8 HTML document. Replaces the current page.
- (void)loadHTML:(NSString *)html url:(NSString *)url;

/// Reflow the current page for a new viewport (e.g. rotation).
- (void)setViewportWidth:(int)width height:(int)height;

/// Deliver a tap at canvas coordinates (engine hit-tests links).
- (void)tapAtX:(int)x y:(int)y;

/// Paint the given canvas-coordinate rect into a UIKit-oriented context
/// whose origin corresponds to the rect's origin (i.e. the visible slice).
- (void)drawIntoContext:(CGContextRef)ctx visibleRect:(CGRect)rect;

@end

NS_ASSUME_NONNULL_END
