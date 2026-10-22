#pragma once

#import <AppKit/AppKit.h>

// One source file in one window: editing, saving in the file's own encoding, compile and run.
// It is the window's delegate, so File/Format/Run menu actions reach it through the responder chain.
@interface CEDocumentWindow : NSObject <NSWindowDelegate>

+ (instancetype)untitled;
+ (instancetype)openPath:(NSString*)path;  // nil (after an alert) if the file cannot be read

@property(nonatomic, readonly) NSWindow* window;
@property(nonatomic, readonly) NSString* path;  // nil while untitled
@property(nonatomic, copy) void (^onClose)(CEDocumentWindow* document);

- (BOOL)isPristine;     // untitled, empty and unmodified: an open replaces it
- (BOOL)confirmClose;   // asks to save unsaved changes; NO if the user cancels

@end
