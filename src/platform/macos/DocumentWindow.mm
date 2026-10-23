#import "DocumentWindow.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#import "ConsolePane.h"
#import "EditorView.h"
#include "TextStyle.h"
#include "core/editor.hpp"
#include "core/encoding.hpp"
#include "posix/compile.hpp"
#include "posix/files.hpp"

using cedit::mac::text_metrics;
using cedit::mac::text_metrics_changed;

namespace {

constexpr CGFloat status_height = 26;
constexpr CGFloat log_padding = 8;          // above and below the build messages
constexpr std::size_t log_min_lines = 2;
constexpr CGFloat log_max_share = 0.4;      // of what the console leaves, so the code always stays in view
constexpr CGFloat console_max_share = 0.5;  // of the window

NSString* ns(std::string_view text) {
    NSString* string = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return string ? string : @"";
}

NSArray<UTType*>* source_types() {
    return @[ UTTypeCSource, UTTypeCHeader, UTTypeCPlusPlusSource, UTTypeCPlusPlusHeader ];
}

void alert(NSString* message, NSString* detail) {
    NSAlert* box = [[NSAlert alloc] init];
    box.messageText = message;
    box.informativeText = detail;
    [box runModal];
}

}  // namespace

NSString* const CEDocumentTabs = @"CEDocument";

// Compiler output; clicking a diagnostic line jumps to it in the editor.
@interface CELogView : NSView
@property(nonatomic, copy) void (^onClickLine)(NSString* line, NSString* above);
@property(nonatomic, readonly) std::size_t lineCount;
- (void)setText:(const std::string&)text;
- (void)layoutLines;  // after the text or the visible size changed
@end

@implementation CELogView {
    std::vector<std::string> _lines;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }

- (instancetype)initWithFrame:(NSRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(textMetricsChanged)
                                               name:text_metrics_changed
                                             object:nil];
    return self;
}

- (std::size_t)lineCount { return _lines.size(); }

- (void)setText:(const std::string&)text {
    _lines.clear();
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        if (end == text.size() && start == end) break;
        _lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    [self layoutLines];
    [self scrollPoint:NSZeroPoint];
}

- (void)layoutLines {
    const auto& metrics = text_metrics();
    std::size_t widest = 0;
    for (const std::string& line : _lines) widest = std::max(widest, line.size());
    const NSSize visible = self.enclosingScrollView.contentSize;
    [self setFrameSize:NSMakeSize(std::max(visible.width, 12 + static_cast<CGFloat>(widest) * metrics.advance),
                                  std::max(visible.height, log_padding + static_cast<CGFloat>(_lines.size()) * metrics.line_height))];
    [self setNeedsDisplay:YES];
}

- (void)textMetricsChanged { [self layoutLines]; }

- (void)drawRect:(NSRect)dirty {
    const auto& metrics = text_metrics();
    [NSColor.controlBackgroundColor setFill];
    NSRectFill(dirty);
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    for (std::size_t i = 0; i < _lines.size(); ++i) {
        const CGFloat y = 4 + static_cast<CGFloat>(i) * metrics.line_height;
        if (y > NSMaxY(dirty) || y + metrics.line_height < NSMinY(dirty)) continue;
        const std::string& line = _lines[i];
        NSColor* color = line.starts_with(cedit::hint_prefix) ? NSColor.systemBlueColor
                         : line.find(": error: ") != std::string::npos || line.find(": fatal error: ") != std::string::npos
                             ? NSColor.systemRedColor
                         : line.find(": warning: ") != std::string::npos ? NSColor.systemOrangeColor
                                                                         : NSColor.labelColor;
        cedit::mac::draw_plain(context, ns(line), color, NSMakePoint(6, y));
    }
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat row = std::floor((point.y - 4) / text_metrics().line_height);
    if (row < 0 || static_cast<std::size_t>(row) >= _lines.size() || !self.onClickLine) return;
    const auto index = static_cast<std::size_t>(row);
    self.onClickLine(ns(_lines[index]), index > 0 ? ns(_lines[index - 1]) : @"");
}

@end

@implementation CEDocumentWindow {
    NSWindow* _window;
    CEEditorView* _editorView;
    cedit::Editor* _editor;  // owned by _editorView
    cedit::FileFormat _format;
    cedit::FileFormat _savedFormat;  // as on disk: any other _format is an unsaved change
    NSString* _path;
    NSScrollView* _editorScroll;
    NSScrollView* _logScroll;
    CELogView* _log;
    NSTextField* _position;
    NSPopUpButton* _encodingMenu;
    NSPopUpButton* _lineEndMenu;
    NSButton* _runButton;
    CEConsoleSession* _console;  // its pane sits under the build messages while it exists
    NSBox* _logDivider;
    NSBox* _consoleDivider;
    NSString* _workDir;  // compiler scratch folder of this document
    BOOL _compiling;
    BOOL _closed;
}

+ (instancetype)untitled {
    CEDocumentWindow* document = [[self alloc] init];
    document->_format = document->_savedFormat = cedit::windows_turkish;
    [document buildWindowWithText:{}];
    return document;
}

+ (instancetype)openPath:(NSString*)path {
    const std::optional<std::string> bytes = cedit::read_file(path.fileSystemRepresentation);
    if (!bytes) {
        alert([NSString stringWithFormat:@"“%@” açılamadı.", path.lastPathComponent], path);
        return nil;
    }
    CEDocumentWindow* document = [[self alloc] init];
    cedit::LoadedText loaded = cedit::load_text(*bytes);
    document->_format = document->_savedFormat = loaded.format;
    document->_path = [path copy];
    [document buildWindowWithText:std::move(loaded.text)];
    return document;
}

- (NSWindow*)window { return _window; }
- (NSString*)path { return _path; }
- (BOOL)modified { return _editor->modified() || _format != _savedFormat; }
- (BOOL)isPristine { return !_path && !self.modified && _editor->text().empty(); }

#pragma mark - Window

- (void)buildWindowWithText:(std::string)text {
    const auto& metrics = text_metrics();
    const NSRect frame = NSMakeRect(0, 0, std::round(100 * metrics.advance), std::round(36 * metrics.line_height));
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.releasedWhenClosed = NO;
    _window.delegate = self;
    _window.minSize = NSMakeSize(420, 300);
    _window.tabbingMode = NSWindowTabbingModePreferred;
    _window.tabbingIdentifier = CEDocumentTabs;

    // Vector SF Symbol: stays sharp at every size and display scale.
    NSImage* playImage = [[NSImage imageWithSystemSymbolName:@"play.fill" accessibilityDescription:@"Derle ve Çalıştır"]
        imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithScale:NSImageSymbolScaleMedium]];
    _runButton = [NSButton buttonWithImage:playImage target:self action:@selector(run:)];
    _runButton.bezelStyle = NSBezelStyleTexturedRounded;
    _runButton.toolTip = @"Derle ve Çalıştır (⌘R veya F11)";
    NSTitlebarAccessoryViewController* accessory = [[NSTitlebarAccessoryViewController alloc] init];
    NSView* holder = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(_runButton.frame) + 16, 28)];
    [_runButton setFrameOrigin:NSMakePoint(8, std::floor((28 - NSHeight(_runButton.frame)) / 2))];
    [holder addSubview:_runButton];
    accessory.view = holder;
    accessory.layoutAttribute = NSLayoutAttributeRight;
    [_window addTitlebarAccessoryViewController:accessory];

    NSView* content = _window.contentView;
    _editorScroll = [[NSScrollView alloc] initWithFrame:content.bounds];
    _editorScroll.hasVerticalScroller = YES;
    _editorScroll.hasHorizontalScroller = YES;
    _editorScroll.autohidesScrollers = YES;
    _editorScroll.borderType = NSNoBorder;
    _editorView = [[CEEditorView alloc] initWithText:std::move(text)];
    _editor = _editorView.editor;
    _editorScroll.documentView = _editorView;
    __weak CEDocumentWindow* weakSelf = self;
    _editorView.onChange = ^{ [weakSelf updateEdited]; };
    _editorView.onCaretMove = ^{ [weakSelf updatePosition]; };

    _logScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _logScroll.hasVerticalScroller = YES;
    _logScroll.hasHorizontalScroller = YES;
    _logScroll.autohidesScrollers = YES;
    _logScroll.borderType = NSNoBorder;
    _log = [[CELogView alloc] initWithFrame:NSZeroRect];
    _log.onClickLine = ^(NSString* line, NSString* above) { [weakSelf clickedLogLine:line above:above]; };
    _logScroll.documentView = _log;
    _logScroll.hidden = YES;

    _position = [NSTextField labelWithString:@""];
    _position.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
    _position.textColor = NSColor.secondaryLabelColor;
    _encodingMenu = [self statusMenu:@[ @"UTF-8", @"Windows-1254 (Türkçe)" ] action:@selector(encodingPicked:)];
    _lineEndMenu = [self statusMenu:@[ @"LF (macOS)", @"CRLF (Windows)" ] action:@selector(lineEndPicked:)];

    _logDivider = [self divider];
    _consoleDivider = [self divider];
    for (NSView* view in @[ _editorScroll, _logScroll, _position, _encodingMenu, _lineEndMenu, _logDivider, _consoleDivider ])
        [content addSubview:view];
    content.postsFrameChangedNotifications = YES;
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(layoutPanes)
                                               name:NSViewFrameDidChangeNotification
                                             object:content];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(layoutPanes)
                                               name:text_metrics_changed
                                             object:nil];
    [self updateTitle];
    [self updatePosition];
    [self updateFormat];
    [_window center];
    [_window makeFirstResponder:_editorView];
}

- (NSBox*)divider {
    NSBox* line = [[NSBox alloc] initWithFrame:NSZeroRect];
    line.boxType = NSBoxSeparator;
    line.hidden = YES;
    return line;
}

- (NSPopUpButton*)statusMenu:(NSArray<NSString*>*)titles action:(SEL)action {
    NSPopUpButton* menu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [menu addItemsWithTitles:titles];
    menu.bordered = NO;
    menu.font = [NSFont systemFontOfSize:11];
    menu.controlSize = NSControlSizeSmall;
    menu.target = self;
    menu.action = action;
    [menu sizeToFit];
    return menu;
}

// The build messages take the height of their lines, from two up to a share of the window.
- (CGFloat)logHeightIn:(CGFloat)available {
    if (_logScroll.hidden) return 0;
    const CGFloat line = text_metrics().line_height;
    const std::size_t lines = std::max(log_min_lines, _log.lineCount);
    const NSSize text = NSMakeSize(0, log_padding + static_cast<CGFloat>(lines) * line);
    const CGFloat wanted = [NSScrollView frameSizeForContentSize:text
                                         horizontalScrollerClass:NSScroller.class
                                           verticalScrollerClass:nil
                                                      borderType:NSNoBorder
                                                     controlSize:NSControlSizeRegular
                                                   scrollerStyle:NSScroller.preferredScrollerStyle]
                               .height;
    const CGFloat smallest = log_padding + static_cast<CGFloat>(log_min_lines) * line;
    return std::round(std::min(wanted, std::max(smallest, std::floor(log_max_share * available))));
}

// From the bottom: status bar, program console, build messages, code.
- (void)layoutPanes {
    const NSRect bounds = _window.contentView.bounds;
    const CGFloat width = NSWidth(bounds);
    const CGFloat available = NSHeight(bounds) - status_height;
    const CGFloat console = _console ? [_console heightWithin:std::floor(console_max_share * available)] : 0;
    const CGFloat log = [self logHeightIn:available - console];
    const CGFloat code = std::max<CGFloat>(0, available - console - log);
    _consoleDivider.hidden = !_console;
    _logDivider.hidden = _logScroll.hidden;
    if (_console) {
        _console.pane.frame = NSMakeRect(0, status_height, width, console);
        _consoleDivider.frame = NSMakeRect(0, status_height + console - 1, width, 1);
        [_console layoutPane];
    }
    _logScroll.frame = NSMakeRect(0, status_height + console, width, log);
    _logDivider.frame = NSMakeRect(0, status_height + console + log - 1, width, 1);
    if (!_logScroll.hidden) [_log layoutLines];
    _editorScroll.frame = NSMakeRect(0, status_height + console + log, width, code);
    _position.frame = NSMakeRect(10, 5, 160, 16);
    CGFloat x = width - 8;
    for (NSPopUpButton* menu : @[ _lineEndMenu, _encodingMenu ]) {
        x -= NSWidth(menu.frame);
        [menu setFrameOrigin:NSMakePoint(x, std::floor((status_height - NSHeight(menu.frame)) / 2))];
        x -= 6;
    }
}

// After the path changed.
- (void)updateTitle {
    _window.title = _path ? _path.lastPathComponent : @"Adsız";
    _window.representedURL = _path ? [NSURL fileURLWithPath:_path] : nil;
    [_window standardWindowButton:NSWindowDocumentIconButton].image = _path ? [NSImage imageNamed:@"CSource"] : nil;
    [self updateEdited];
}

- (void)updateEdited { _window.documentEdited = self.modified; }

- (void)updatePosition {
    const std::size_t caret = _editor->caret();
    _position.stringValue = [NSString stringWithFormat:@"Sütun %zu", _editor->column_of(caret) + 1];  // the gutter shows the line
}

// After the encoding or the line ends changed.
- (void)updateFormat {
    [_encodingMenu selectItemAtIndex:_format.encoding == cedit::Encoding::utf8 ? 0 : 1];
    [_lineEndMenu selectItemAtIndex:_format.crlf ? 1 : 0];
    [_encodingMenu sizeToFit];
    [_lineEndMenu sizeToFit];
    [self layoutPanes];
    [self updateEdited];
}

#pragma mark - Format

- (void)setEncoding:(cedit::Encoding)encoding {
    if (_format.encoding == encoding) return;
    _format.encoding = encoding;
    _format.byte_order_mark = encoding == cedit::Encoding::utf8 && _savedFormat.byte_order_mark;  // the file's own, if it had one
    [self updateFormat];
}

- (void)setCRLF:(bool)crlf {
    if (_format.crlf == crlf) return;
    _format.crlf = crlf;
    [self updateFormat];
}

- (void)encodingPicked:(NSPopUpButton*)menu {
    [self setEncoding:menu.indexOfSelectedItem == 0 ? cedit::Encoding::utf8 : cedit::Encoding::windows1254];
}
- (void)lineEndPicked:(NSPopUpButton*)menu { [self setCRLF:menu.indexOfSelectedItem == 1]; }
- (void)useUTF8:(id)sender { (void)sender, [self setEncoding:cedit::Encoding::utf8]; }
- (void)useWindows1254:(id)sender { (void)sender, [self setEncoding:cedit::Encoding::windows1254]; }
- (void)useLF:(id)sender { (void)sender, [self setCRLF:false]; }
- (void)useCRLF:(id)sender { (void)sender, [self setCRLF:true]; }

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    const SEL action = item.action;
    if (action == @selector(useUTF8:)) item.state = _format.encoding == cedit::Encoding::utf8;
    if (action == @selector(useWindows1254:)) item.state = _format.encoding == cedit::Encoding::windows1254;
    if (action == @selector(useLF:)) item.state = !_format.crlf;
    if (action == @selector(useCRLF:)) item.state = _format.crlf;
    if (action == @selector(run:)) return !_compiling;
    return YES;
}

#pragma mark - Saving

- (BOOL)writeTo:(NSString*)path {
    const auto bytes = cedit::store_text(_editor->text(), _format);
    if (!bytes) {
        const std::size_t offset = bytes.error();
        const std::size_t line = _editor->line_of(offset);
        NSString* rest = ns(std::string_view(_editor->text()).substr(offset));
        NSString* character = [rest substringWithRange:[rest rangeOfComposedCharacterSequenceAtIndex:0]];
        alert([NSString stringWithFormat:@"%zu. satırdaki “%@” %s olarak kaydedilemiyor.", line + 1, character,
                                         std::string(cedit::display_name(_format.encoding)).c_str()],
              @"Bu karakteri silin ya da durum çubuğundan UTF-8’i seçin.");
        [_editorView goToLine:line column:offset - _editor->line_start(line)];
        return NO;
    }
    if (!cedit::write_file_atomic(path.fileSystemRepresentation, *bytes)) {
        alert([NSString stringWithFormat:@"“%@” kaydedilemedi.", path.lastPathComponent], path);
        return NO;
    }
    _path = [path copy];
    _editor->mark_saved();
    _savedFormat = _format;
    [self updateTitle];
    return YES;
}

- (BOOL)saveAs {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = source_types();
    panel.allowsOtherFileTypes = YES;
    panel.nameFieldStringValue = _path ? _path.lastPathComponent : @"adsız.c";
    if (_path) panel.directoryURL = [NSURL fileURLWithPath:_path.stringByDeletingLastPathComponent];
    if ([panel runModal] != NSModalResponseOK) return NO;
    return [self writeTo:panel.URL.path];
}

- (BOOL)save { return _path ? [self writeTo:_path] : [self saveAs]; }
- (void)saveDocument:(id)sender { (void)sender, [self save]; }
- (void)saveDocumentAs:(id)sender { (void)sender, [self saveAs]; }

- (BOOL)confirmClose {
    if (!self.modified || (!_path && _editor->text().empty())) return YES;
    [_window makeKeyAndOrderFront:nil];
    NSAlert* box = [[NSAlert alloc] init];
    box.messageText = [NSString stringWithFormat:@"“%@” dosyasında yaptığınız değişiklikleri kaydetmek istiyor musunuz?",
                                                   _window.title];
    box.informativeText = @"Kaydetmezseniz değişiklikleriniz kaybolur.";
    [box addButtonWithTitle:@"Kaydet"];
    [box addButtonWithTitle:@"Vazgeç"].keyEquivalent = @"\033";  // Escape: AppKit gives it only to a button titled "Cancel"
    NSButton* discard = [box addButtonWithTitle:@"Kaydetme"];
    discard.keyEquivalent = @"d";
    discard.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    const NSModalResponse answer = [box runModal];
    if (answer == NSAlertFirstButtonReturn) return [self save];
    return answer == NSAlertThirdButtonReturn;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    return [self confirmClose];
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    _closed = YES;
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [_console close];
    _console = nil;
    if (self.onClose) self.onClose(self);
}

#pragma mark - Compile and run

- (void)showLog:(const std::string&)text {
    _logScroll.hidden = NO;
    [_log setText:text];
    [self layoutPanes];
}

- (void)run:(id)sender {
    (void)sender;
    if (_compiling || ![self save]) return;
    [self closeConsole];
    _compiling = YES;
    _runButton.enabled = NO;
    [self showLog:std::string(_path.lastPathComponent.UTF8String) + " derleniyor…"];
    if (!_workDir) {
        // One per document, so two open files with the same name never overwrite each other's program.
        static unsigned long documents = 0;
        _workDir = [NSTemporaryDirectory()
            stringByAppendingPathComponent:[NSString stringWithFormat:@"c-editor-%d-%lu", ::getpid(), ++documents]];
    }
    NSString* compat = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"compat"];
    const cedit::CompileRequest request{_path.fileSystemRepresentation, _editor->text(), compat.fileSystemRepresentation,
                                        _workDir.fileSystemRepresentation};
    __weak CEDocumentWindow* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (!cedit::developer_tools_installed()) {
            dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf offerDeveloperTools]; });
            return;
        }
        const cedit::CompileResult result = cedit::compile(request);
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf compiled:result]; });
    });
}

// Without Apple's compiler nothing can be built; its installer is one click away.
- (void)offerDeveloperTools {
    _compiling = NO;
    _runButton.enabled = YES;
    if (_closed) return;
    [self showLog:"Programları derlemek için Apple’ın Komut Satırı Araçları gerekiyor."];
    NSAlert* box = [[NSAlert alloc] init];
    box.messageText = @"Apple’ın geliştirici araçları gerekiyor";
    box.informativeText = @"C programlarını derlemek için Apple’ın ücretsiz Komut Satırı Araçları bir kez kurulmalı. "
                          @"“Kur”a tıklayınca Apple’ın kurulum penceresi açılır; kurulum bitince ⌘R’ye yeniden basın.";
    [box addButtonWithTitle:@"Kur"];
    [box addButtonWithTitle:@"Vazgeç"].keyEquivalent = @"\033";
    if ([box runModal] != NSAlertFirstButtonReturn) return;
    [self showLog:"Apple’ın kurulum penceresi açılıyor. Kurulum bitince ⌘R’ye yeniden basın."];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ cedit::request_developer_tools(); });
}

- (void)compiled:(const cedit::CompileResult&)result {
    _compiling = NO;
    _runButton.enabled = YES;
    if (_closed) return;
    [self showLog:result.log];
    [_editorView setDiagnostics:result.diagnostics];
    if (!result.ok) {
        for (const cedit::Diagnostic& diagnostic : result.diagnostics)
            if (diagnostic.error) return [_editorView goToLine:diagnostic.line column:diagnostic.column];
        return;
    }
    // Without messages to read, the build summary heads the console instead of a pane of its own.
    std::string_view summary = result.log;
    while (summary.ends_with('\n')) summary.remove_suffix(1);
    summary.remove_prefix(std::min(summary.size(), summary.rfind('\n') + 1));
    if (result.diagnostics.empty()) _logScroll.hidden = YES;
    _console = [CEConsoleSession runProgram:[NSString stringWithUTF8String:result.binary.c_str()]
                                inDirectory:_path.stringByDeletingLastPathComponent
                                     header:result.diagnostics.empty() ? summary : std::string_view()];
    __weak CEDocumentWindow* weakSelf = self;
    _console.onResize = ^{ [weakSelf layoutPanes]; };
    _console.onDismiss = ^{ [weakSelf closeConsole]; };
    [_window.contentView addSubview:_console.pane positioned:NSWindowBelow relativeTo:_consoleDivider];
    [self layoutPanes];
    [_window makeFirstResponder:_console.keyView];
}

// Ends the program, if it still runs, and gives its room back to the code.
- (void)closeConsole {
    if (!_console) return;
    const BOOL focused = _window.firstResponder == _console.keyView;
    [_console close];
    [_console.pane removeFromSuperview];
    _console = nil;
    [self layoutPanes];
    if (focused) [_window makeFirstResponder:_editorView];
}

// A message line, or the explanation under it, jumps to its place; the text is left to the writer.
- (void)clickedLogLine:(NSString*)line above:(NSString*)above {
    const bool hint = std::string_view(line.UTF8String).starts_with(cedit::hint_prefix);
    const auto parsed = cedit::parse_diagnostics((hint ? above : line).UTF8String, _path.lastPathComponent.fileSystemRepresentation);
    if (!parsed.empty()) [_editorView goToLine:parsed.front().line column:parsed.front().column];
}

@end
