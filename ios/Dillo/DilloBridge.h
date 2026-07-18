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
#import <UIKit/UIKit.h>

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

/// A link was activated or a form submitted. For GET navigations body and
/// contentType are nil; for POST they carry the encoded form data.
@property (nonatomic, copy, nullable) void (^onOpenURL)(
   NSString *url, NSData *_Nullable body, NSString *_Nullable contentType);

/// Install the view that hosts native form controls (the browser's scroll
/// view: its content coordinates are canvas coordinates). Must be called
/// before the first loadHTML for forms to be interactive.
- (void)setFormHost:(UIView *)host;

/// The parser saw <title>.
@property (nonatomic, copy, nullable) void (^onTitle)(NSString *title);

/// The engine wants the viewport scrolled (anchor jump, find-in-page,
/// scroll-into-view). Canvas coordinates.
@property (nonatomic, copy, nullable) void (^onScrollTo)(CGPoint origin);

/// Text was selected and copied (already on the pasteboard) — for UI hints.
@property (nonatomic, copy, nullable) void (^onCopy)(NSString *text);

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

/// Long-press selection gesture, forwarded as engine mouse events
/// (press starts a selection, motion extends it, release copies it).
- (void)selectionBeganAtX:(int)x y:(int)y
   NS_SWIFT_NAME(selectionBegan(x:y:));
- (void)selectionMovedToX:(int)x y:(int)y
   NS_SWIFT_NAME(selectionMoved(x:y:));
- (void)selectionEndedAtX:(int)x y:(int)y
   NS_SWIFT_NAME(selectionEnded(x:y:));

/// The user scrolled; keeps the engine's scroll position in sync (used
/// by scroll-into-view calculations).
- (void)reportScrollX:(int)x y:(int)y NS_SWIFT_NAME(reportScroll(x:y:));

/// If urlString points into the CURRENT page (same document, different
/// #fragment), scroll to the anchor and return YES; else return NO and
/// the caller performs a normal load.
- (BOOL)navigateFragment:(NSString *)urlString;

/// Find-in-page. Result: 0 = found, 1 = wrapped (restarted from top),
/// 2 = not found. resetFind clears highlights and restarts.
- (int)findText:(NSString *)key caseSensitive:(BOOL)caseSens
       backwards:(BOOL)backwards;
- (void)resetFind;

/// Paint the given canvas-coordinate rect into a UIKit-oriented context
/// whose origin corresponds to the rect's origin (i.e. the visible slice).
- (void)drawIntoContext:(CGContextRef)ctx visibleRect:(CGRect)rect;

@end

NS_ASSUME_NONNULL_END
