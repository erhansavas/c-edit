#pragma once

#import <AppKit/AppKit.h>

#include <string_view>

// One run of a compiled program in a console pane under the code: the program's terminal is a
// pty, and after it exits the pane reports the exit and closes on any key.
// The document owns the pane's place; the pane only says how tall it wants to be.
@interface CEConsoleSession : NSObject

// `header` (e.g. the build summary) is the pane's first line.
+ (instancetype)runProgram:(NSString*)program inDirectory:(NSString*)directory header:(std::string_view)header;

@property(nonatomic, readonly) NSScrollView* pane;
@property(nonatomic, readonly) NSView* keyView;          // takes the program's keyboard input
@property(nonatomic, copy) void (^onResize)(void);       // the pane wants another height
@property(nonatomic, copy) void (^onDismiss)(void);      // a key after the program ended

// The height for the rows written so far, from 8 up to the terminal's 25, with at most `limit`.
- (CGFloat)heightWithin:(CGFloat)limit;
- (void)layoutPane;  // after the owner changed the pane's frame
- (void)close;       // ends the program if it is still running

@end
