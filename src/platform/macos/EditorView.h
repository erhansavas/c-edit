#pragma once

#import <AppKit/AppKit.h>

#include <string>
#include <vector>

#include "core/diagnostics.hpp"
#include "core/editor.hpp"

// Draws and edits the text of one document. Lives in an NSScrollView.
@interface CEEditorView : NSView <NSTextInputClient>

- (instancetype)initWithText:(std::string)text;

@property(nonatomic, readonly) cedit::Editor* editor;    // owned by the view, valid for its whole lifetime
@property(nonatomic, copy) void (^onChange)(void);     // text changed
@property(nonatomic, copy) void (^onCaretMove)(void);  // caret or selection moved

- (void)setDiagnostics:(std::vector<cedit::Diagnostic>)diagnostics;
- (void)goToLine:(std::size_t)line column:(std::size_t)column;

@end
