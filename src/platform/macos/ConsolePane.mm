#import "ConsolePane.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
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
constexpr std::size_t first_rows = 8;  // enough for a short program and the closing lines
constexpr CGFloat inset = 6;
constexpr std::size_t read_limit = 64 * 1024;  // per wake-up, so endless output cannot starve the event loop

// "\a" (alert): a quarter-second 880 Hz tone at full scale, about 10 dB louder than the
// loudest system alert sound, made once in memory as a 16-bit mono WAV.
NSSound* bell_sound() {
    static NSSound* const sound = [] {
        constexpr std::uint32_t rate = 44100;
        constexpr std::uint32_t frames = rate / 4;
        constexpr std::uint32_t fade = rate / 200;  // 5 ms in and out, so the tone does not click
        std::string wav = "RIFF....WAVEfmt ";
        const auto put = [&](std::uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) wav.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        };
        put(16, 4), put(1, 2), put(1, 2), put(rate, 4), put(rate * 2, 4), put(2, 2), put(16, 2);
        wav += "data";
        put(frames * 2, 4);
        for (std::uint32_t i = 0; i < frames; ++i) {
            const double envelope = std::min({1.0, i / double(fade), (frames - i) / double(fade)});
            const double sample = 0.95 * 32767 * envelope * std::sin(2 * M_PI * 880 * i / rate);
            put(static_cast<std::uint32_t>(static_cast<std::int16_t>(sample)) & 0xFFFF, 2);
        }
        const auto size = static_cast<std::uint32_t>(wav.size() - 8);
        for (int i = 0; i < 4; ++i) wav[static_cast<std::size_t>(4 + i)] = static_cast<char>((size >> (8 * i)) & 0xFF);
        return [[NSSound alloc] initWithData:[NSData dataWithBytes:wav.data() length:wav.size()]];
    }();
    return sound;
}

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
@property(nonatomic, readonly) std::size_t lineCount;
- (void)show:(std::string_view)output;
- (void)terminalChanged;  // after new output or a new size: re-lays out and shows the cursor line
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

- (std::size_t)lineCount { return _terminal.lines().size(); }

- (void)show:(std::string_view)output {
    const std::size_t bells = _terminal.bells();
    _terminal.feed(output);
    if (_terminal.bells() != bells) {  // "\a" beeps, as in a console
        [bell_sound() stop];
        [bell_sound() play];
    }
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
    NSScrollView* _pane;
    CEConsoleView* _view;
    std::optional<cedit::PtyChild> _child;
    dispatch_source_t _reader;  // the pty master is open exactly while this exists
    dispatch_source_t _exitWatch;
    std::chrono::steady_clock::time_point _started;
    std::size_t _rows;  // rows the pane shows; it only grows, so the code above never jumps back
    BOOL _exited;
}

+ (instancetype)runProgram:(NSString*)program inDirectory:(NSString*)directory header:(std::string_view)header {
    CEConsoleSession* session = [[self alloc] init];
    [session makePane];
    if (!header.empty()) [session->_view show:std::string(header) + "\r\n"];
    [session start:program directory:directory];
    return session;
}

- (NSScrollView*)pane { return _pane; }
- (NSView*)keyView { return _view; }

- (void)makePane {
    _rows = first_rows;
    _pane = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
    _pane.hasVerticalScroller = YES;
    _pane.hasHorizontalScroller = YES;
    _pane.autohidesScrollers = YES;
    _pane.borderType = NSNoBorder;
    _view = [[CEConsoleView alloc] initWithFrame:_pane.bounds];
    __weak CEConsoleSession* weakSelf = self;
    _view.onKeys = ^(NSString* keys) { [weakSelf typed:keys]; };
    _pane.documentView = _view;
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(textMetricsChanged)
                                               name:text_metrics_changed
                                             object:nil];
}

- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }

- (CGFloat)heightWithin:(CGFloat)limit {
    const NSSize text = NSMakeSize(console_width(), 2 * inset + static_cast<CGFloat>(_rows) * text_metrics().line_height);
    const CGFloat wanted = [NSScrollView frameSizeForContentSize:text
                                         horizontalScrollerClass:NSScroller.class
                                           verticalScrollerClass:nil
                                                      borderType:NSNoBorder
                                                     controlSize:NSControlSizeRegular
                                                   scrollerStyle:NSScroller.preferredScrollerStyle]
                               .height;
    return std::round(std::min(wanted, limit));
}

- (void)layoutPane { [_view terminalChanged]; }

- (void)textMetricsChanged {
    if (self.onResize) self.onResize();
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
    if (!output.empty()) [self show:output];
}

// Grows the pane with the lines written so far.
- (void)show:(std::string_view)output {
    [_view show:output];
    const std::size_t rows = std::min<std::size_t>(console_rows, _view.lineCount);
    if (rows <= _rows) return;
    _rows = rows;
    if (self.onResize) self.onResize();
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
    [self show:"\r\n--------------------------------\r\n" + ending + "\r\nPaneli kapatmak için bir tuşa basın."];
}

- (void)typed:(NSString*)keys {
    if (_exited) {
        if (self.onDismiss) self.onDismiss();
        return;
    }
    const char* bytes = keys.UTF8String;
    if (bytes && _reader) (void)::write(_child->master, bytes, std::strlen(bytes));
}

- (void)close {
    _view.onKeys = nil;
    self.onResize = nil;
    self.onDismiss = nil;
    if (_exited || !_child) return;
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

@end
