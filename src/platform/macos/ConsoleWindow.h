#pragma once

#import <AppKit/AppKit.h>

// One run of a compiled program in its own console window: the
// program's terminal is a pty, and after it exits the window reports the exit and closes on
// any key.
@interface CEConsoleSession : NSObject <NSWindowDelegate>

// `onClose` runs after the window has finished closing; the session stays alive until then.
+ (instancetype)runProgram:(NSString*)program
               inDirectory:(NSString*)directory
                   onClose:(void (^)(CEConsoleSession* session))onClose;

- (void)close;  // ends the program if it is still running

@end
