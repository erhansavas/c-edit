#import "ConsoleWindow.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include "TextStyle.h"
#include "core/terminal.hpp"
#include "core/utf8.hpp"
#include "posix/process.hpp"

using cedit::mac::text_metrics;
using cedit::mac::text_metrics_changed;

namespace {

constexpr unsigned short console_rows = 25;
constexpr CGFloat inset = 6;
constexpr std::size_t read_limit = 64 * 1024;  // per wake-up, so endless output cannot starve the event loop

CGFloat console_width() { return 2 * inset + static_cast<CGFloat>(cedit::Terminal::columns) * text_metrics().advance; }

NSString* string_of(const std::u32string& line) {
    std::string bytes;
    for (const char32_t c : line) cedit::utf8::append(bytes, c);
    return [[NSString alloc] initWithBytes:bytes.data() length:bytes.size() encoding:NSUTF8StringEncoding];
}

}  // namespace

// Owns the terminal it draws, so the screen stays valid for as long as the view exists.
@interface CEConsoleView : NSView
@property(nonatomic, copy) void (^onKeys)(NSString* bytes);
- (void)show:(std::string_view)output;
@end

@implementation CEConsoleView {
    cedit::Terminal _terminal;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

- (instancetype)initWithFrame:(NSRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(terminalChanged)
                                               name:text_metrics_changed
                                             object:nil];
    return self;
}

- (void)show:(std::string_view)output {
    _terminal.feed(output);
    [self terminalChanged];
}

- (void)terminalChanged {
    const auto& metrics = text_metrics();
    const NSSize visible = self.enclosingScrollView.contentSize;
    const CGFloat height = 2 * inset + static_cast<CGFloat>(_terminal.lines().size()) * metrics.line_height;
    [self setFrameSize:NSMakeSize(std::max(visible.width, console_width()), std::max(visible.height, height))];
    [self setNeedsDisplay:YES];
    [self scrollRectToVisible:NSMakeRect(0, height - metrics.line_height - inset, 1, metrics.line_height + inset)];
}

- (void)drawRect:(NSRect)dirty {
    const auto& metrics = text_metrics();
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    [NSColor.textBackgroundColor setFill];
    NSRectFill(dirty);
    const auto& lines = _terminal.lines();
    const std::size_t first = static_cast<std::size_t>(std::max<CGFloat>(0, (NSMinY(dirty) - inset) / metrics.line_height));
    const std::size_t last = std::min(lines.size(), static_cast<std::size_t>(std::max<CGFloat>(0, (NSMaxY(dirty) - inset) / metrics.line_height)) + 1);
    for (std::size_t line = first; line < last; ++line)
        cedit::mac::draw_plain(context, string_of(lines[line]), NSColor.labelColor,
                             NSMakePoint(inset, inset + static_cast<CGFloat>(line) * metrics.line_height));
    [[NSColor.labelColor colorWithAlphaComponent:0.55] setFill];
    NSRectFill(NSMakeRect(inset + static_cast<CGFloat>(_terminal.cursor_column()) * metrics.advance,
                          inset + static_cast<CGFloat>(_terminal.cursor_line()) * metrics.line_height,
                          std::ceil(metrics.advance), metrics.line_height));
}

- (void)keyDown:(NSEvent*)event {
    NSString* characters = event.characters;
    if (characters.length == 0 || !self.onKeys) return;
    // Keys are told apart without modifiers: Control-C also arrives as 0x03, the code of the keypad Enter key.
    NSString* plain = event.charactersIgnoringModifiers;
    const unichar key = [(plain.length > 0 ? plain : characters) characterAtIndex:0];
    NSString* bytes = characters;
    switch (key) {
    case NSUpArrowFunctionKey: bytes = @"\x1B[A"; break;
    case NSDownArrowFunctionKey: bytes = @"\x1B[B"; break;
    case NSRightArrowFunctionKey: bytes = @"\x1B[C"; break;
    case NSLeftArrowFunctionKey: bytes = @"\x1B[D"; break;
    case NSEnterCharacter:
    case NSCarriageReturnCharacter: bytes = @"\r"; break;
    case NSDeleteCharacter:
    case NSBackspaceCharacter: bytes = @"\x7F"; break;
    default:
        if (key >= 0xF700 && key <= 0xF8FF) return;  // other function keys
        break;
    }
    self.onKeys(bytes);
}

- (void)paste:(id)sender {
    (void)sender;
    NSString* text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    if (text && self.onKeys) self.onKeys([text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"]);
}

- (void)copy:(id)sender {
    (void)sender;
    NSMutableArray<NSString*>* lines = [NSMutableArray array];
    for (const auto& line : _terminal.lines()) [lines addObject:string_of(line)];
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:[lines componentsJoinedByString:@"\n"] forType:NSPasteboardTypeString];
}

@end

@implementation CEConsoleSession {
    NSWindow* _window;
    CEConsoleView* _view;
    std::optional<cedit::PtyChild> _child;
    dispatch_source_t _reader;  // the pty master is open exactly while this exists
    dispatch_source_t _exitWatch;
    std::chrono::steady_clock::time_point _started;
    BOOL _exited;
    void (^_onClose)(CEConsoleSession*);
}

+ (instancetype)runProgram:(NSString*)program
               inDirectory:(NSString*)directory
                   onClose:(void (^)(CEConsoleSession*))onClose {
    CEConsoleSession* session = [[self alloc] init];
    session->_onClose = [onClose copy];
    [session openWindowTitled:program.lastPathComponent];
    [session start:program directory:directory];
    return session;
}

- (void)openWindowTitled:(NSString*)title {
    const NSRect frame = NSMakeRect(0, 0, console_width() + 16, 2 * inset + console_rows * text_metrics().line_height);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.releasedWhenClosed = NO;
    _window.tabbingMode = NSWindowTabbingModeDisallowed;
    _window.title = title;
    _window.delegate = self;
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    _view = [[CEConsoleView alloc] initWithFrame:frame];
    __weak CEConsoleSession* weakSelf = self;
    _view.onKeys = ^(NSString* keys) { [weakSelf typed:keys]; };
    scroll.documentView = _view;
    _window.contentView = scroll;
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_view];
}

- (void)start:(NSString*)program directory:(NSString*)directory {
    _started = std::chrono::steady_clock::now();
    _child = cedit::spawn_in_pty(program.fileSystemRepresentation, directory.fileSystemRepresentation,
                                 cedit::Terminal::columns, console_rows);
    if (!_child) {
        [self finishWith:"Program başlatılamadı."];
        return;
    }
    const int master = _child->master;
    __weak CEConsoleSession* weakSelf = self;
    _reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>(master), 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_reader, ^{ [weakSelf drain]; });
    dispatch_source_set_cancel_handler(_reader, ^{ ::close(master); });
    dispatch_resume(_reader);

    _exitWatch = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, static_cast<uintptr_t>(_child->pid),
                                        DISPATCH_PROC_EXIT, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_exitWatch, ^{ [weakSelf reap]; });
    dispatch_resume(_exitWatch);
    [self reap];  // a program that already finished before the watch was armed
}

// While output flows, reading goes on for a few milliseconds: the program refills the small pty
// buffer as soon as it is emptied, and one buffer per wake-up of the main thread would be slow.
- (void)drain {
    if (!_reader) return;
    std::string output;
    char buffer[16384];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
    while (output.size() < read_limit) {
        const ssize_t got = ::read(_child->master, buffer, sizeof buffer);
        if (got > 0) {
            output.append(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        if (got == 0 || errno != EAGAIN) {  // 0 or EIO: the program closed its terminal
            [self stopReading];
            break;
        }
        pollfd more{_child->master, POLLIN, 0};
        if (output.empty() || std::chrono::steady_clock::now() >= deadline || ::poll(&more, 1, 1) <= 0) break;
    }
    if (!output.empty()) [_view show:output];
}

- (void)reap {
    if (_exited || !_child) return;
    int status = 0;
    const pid_t done = ::waitpid(_child->pid, &status, WNOHANG);
    if (done == 0) return;
    [self drain];
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - _started;
    [self finishWith:done == _child->pid ? cedit::describe_exit(status, elapsed.count()) : "Program sona erdi."];
}

- (void)stopReading {
    if (_reader) dispatch_source_cancel(_reader);
    _reader = nil;
}

- (void)stopWatching {
    [self stopReading];
    if (_exitWatch) dispatch_source_cancel(_exitWatch);
    _exitWatch = nil;
}

- (void)finishWith:(const std::string&)ending {
    _exited = YES;
    [self stopWatching];
    [_view show:"\r\n--------------------------------\r\n" + ending + "\r\nPencereyi kapatmak için bir tuşa basın."];
}

- (void)typed:(NSString*)keys {
    if (_exited) return [self close];
    const char* bytes = keys.UTF8String;
    if (bytes && _reader) (void)::write(_child->master, bytes, std::strlen(bytes));
}

- (void)close { [_window close]; }

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (!_exited && _child) {
        // The whole process group: the program and anything it started, such as a system() shell.
        const pid_t pid = _child->pid;
        if (::kill(-pid, SIGKILL) != 0) ::kill(pid, SIGKILL);
        _exited = YES;
        [self stopWatching];
        // A killed program still flushes its pending output and only finishes exiting once the terminal
        // is closed (by the reader's cancel handler, on this queue), so it is reaped off the main thread.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
            }
        });
    }
    _view.onKeys = nil;
    void (^onClose)(CEConsoleSession*) = _onClose;
    _onClose = nil;
    if (onClose) dispatch_async(dispatch_get_main_queue(), ^{ onClose(self); });
}

@end
