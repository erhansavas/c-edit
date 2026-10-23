#import "EditorView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "TextStyle.h"
#include "core/syntax.hpp"
#include "core/utf8.hpp"

using cedit::mac::text_metrics;
using cedit::mac::text_metrics_changed;

namespace {

constexpr CGFloat text_inset = 8;     // between gutter and text
constexpr CGFloat vertical_inset = 6;
constexpr CGFloat gutter_padding = 8;  // fixed in points, so zooming never widens the margin

NSString* string_of(id text) {
    return [text isKindOfClass:NSAttributedString.class] ? ((NSAttributedString*)text).string : (NSString*)text;
}

std::string utf8_of(NSString* text) { return text.UTF8String ? std::string(text.UTF8String) : std::string(); }

// NSTextInputClient ranges count UTF-16 units over the whole text; the editor counts UTF-8 bytes.
NSUInteger utf16_length(std::string_view text) {
    NSUInteger units = 0;
    for (const char c : text)
        if (!cedit::utf8::is_continuation(c)) units += static_cast<unsigned char>(c) >= 0xF0u ? 2 : 1;
    return units;
}

// Bytes taken by the first `units` UTF-16 units of `text`, or all of it if it is shorter.
std::size_t utf8_length(std::string_view text, NSUInteger units) {
    std::size_t offset = 0;
    for (NSUInteger counted = 0; offset < text.size() && counted < units; offset = cedit::utf8::next(text, offset))
        counted += static_cast<unsigned char>(text[offset]) >= 0xF0u ? 2 : 1;
    return offset;
}

}  // namespace

@implementation CEEditorView {
    cedit::Editor _editor;
    std::vector<cedit::LineState> _states;  // highlight state at the start of each line
    std::vector<std::vector<std::size_t>> _guides;  // indentation guide columns of each line
    std::uint64_t _statesRevision;
    std::uint64_t _seenRevision;  // text revision the size, diagnostics and owner last saw
    std::vector<cedit::Diagnostic> _diagnostics;
    std::size_t _longestLine;  // in columns
    NSString* _marked;          // uncommitted input-method text, drawn at the caret
    NSRange _markedSelection;   // the input method's selection within _marked
    NSTimer* _blink;
    BOOL _caretVisible;
}

- (instancetype)initWithText:(std::string)text {
    if (!(self = [super initWithFrame:NSMakeRect(0, 0, 400, 400)])) return nil;
    _editor = cedit::Editor(std::move(text));
    _statesRevision = ~std::uint64_t{0};
    _seenRevision = _editor.revision();
    _marked = @"";
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(textMetricsChanged)
                                               name:text_metrics_changed
                                             object:nil];
    [self measure];
    return self;
}

- (void)dealloc { [_blink invalidate]; }

- (cedit::Editor*)editor { return &_editor; }

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)textMetricsChanged {
    [self.window invalidateCursorRectsForView:self];
    [self fitFrame];
    [self revealCaret];
    [self setNeedsDisplay:YES];
}

- (void)magnifyWithEvent:(NSEvent*)event {
    cedit::mac::change_text_size(text_metrics().font.pointSize * event.magnification);
}

#pragma mark - Focus

- (BOOL)isFocused { return self.window.isKeyWindow && self.window.firstResponder == self; }

- (void)viewDidMoveToWindow {
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    [center removeObserver:self name:NSWindowDidBecomeKeyNotification object:nil];
    [center removeObserver:self name:NSWindowDidResignKeyNotification object:nil];
    if (self.window) {
        [center addObserver:self selector:@selector(keyWindowChanged) name:NSWindowDidBecomeKeyNotification object:self.window];
        [center addObserver:self selector:@selector(keyWindowChanged) name:NSWindowDidResignKeyNotification object:self.window];
    }
    [self keyWindowChanged];
}

// The caret blinks, and the selection is emphasized, only in the key window.
- (void)keyWindowChanged {
    [self blink:self.isFocused];
    [self setNeedsDisplay:YES];
}

- (BOOL)becomeFirstResponder {
    [self blink:self.window.isKeyWindow];  // the window's firstResponder is not yet this view
    [self setNeedsDisplay:YES];
    return YES;
}

- (BOOL)resignFirstResponder {
    [self blink:NO];
    [self setNeedsDisplay:YES];
    return YES;
}

// Starts the blink over with the caret shown, or stops it.
- (void)blink:(BOOL)on {
    [_blink invalidate];
    _blink = nil;
    _caretVisible = on;
    if (!on) return;
    __weak CEEditorView* weakSelf = self;
    _blink = [NSTimer scheduledTimerWithTimeInterval:0.53
                                             repeats:YES
                                               block:^(NSTimer*) {
                                                   CEEditorView* view = weakSelf;
                                                   if (!view) return;
                                                   view->_caretVisible = !view->_caretVisible;
                                                   [view setNeedsDisplayInRect:[view caretRect]];
                                               }];
}

#pragma mark - Geometry

- (CGFloat)gutterWidth {
    const std::size_t digits = std::to_string(_editor.line_count()).size();
    return std::ceil(static_cast<CGFloat>(digits) * text_metrics().advance) + 2 * gutter_padding;
}

- (CGFloat)textLeft { return [self gutterWidth] + text_inset; }

- (NSPoint)pointForOffset:(std::size_t)offset {
    const std::size_t line = _editor.line_of(offset);
    return NSMakePoint([self textLeft] + static_cast<CGFloat>(_editor.column_of(offset)) * text_metrics().advance,
                       vertical_inset + static_cast<CGFloat>(line) * text_metrics().line_height);
}

- (NSRect)caretRect {
    const NSPoint point = [self pointForOffset:_editor.caret()];
    const CGFloat marked = static_cast<CGFloat>(_marked.length) * text_metrics().advance;
    return NSMakeRect(point.x - 1 + marked, point.y, 3, text_metrics().line_height);
}

- (std::size_t)offsetForPoint:(NSPoint)point {
    const CGFloat row = std::floor((point.y - vertical_inset) / text_metrics().line_height);
    const std::size_t line = row < 0 ? 0 : static_cast<std::size_t>(row);
    if (line >= _editor.line_count()) return _editor.text().size();
    const CGFloat column = std::round((point.x - [self textLeft]) / text_metrics().advance);
    return _editor.offset_at(line, column < 0 ? 0 : static_cast<std::size_t>(column));
}

- (void)measure {
    _longestLine = 0;
    for (std::size_t line = 0; line < _editor.line_count(); ++line)
        _longestLine = std::max(_longestLine, _editor.column_of(_editor.line_end(line)));
    [self.window invalidateCursorRectsForView:self];  // the gutter widens with the line count
    [self fitFrame];
}

// The size of the text, but never smaller than the visible area, which follows window resizes.
- (void)fitFrame {
    NSScrollView* scroll = self.enclosingScrollView;
    const NSSize visible = scroll ? scroll.contentSize : self.frame.size;
    const CGFloat width = [self textLeft] + static_cast<CGFloat>(_longestLine + 4) * text_metrics().advance;
    const CGFloat height = 2 * vertical_inset + static_cast<CGFloat>(_editor.line_count()) * text_metrics().line_height;
    const NSSize size = NSMakeSize(std::max(width, visible.width), std::max(height, visible.height));
    if (!NSEqualSizes(size, self.frame.size)) [self setFrameSize:size];
}

- (void)viewWillDraw {
    [super viewWillDraw];
    [self fitFrame];
}

- (void)revealCaret {
    [self scrollRectToVisible:NSInsetRect([self caretRect], -4 * text_metrics().advance, -text_metrics().line_height)];
}

#pragma mark - Drawing

- (void)ensureStates {
    if (_statesRevision == _editor.revision()) return;
    _states.assign(_editor.line_count(), {});
    std::vector<cedit::Span> scratch;
    cedit::LineState state;
    for (std::size_t line = 0; line < _editor.line_count(); ++line) {
        _states[line] = state;
        state = cedit::highlight_c(_editor.line(line), state, scratch);
    }
    _guides = _editor.indent_guides();
    _statesRevision = _editor.revision();
}

- (void)drawRect:(NSRect)dirty {
    [self ensureStates];
    const auto& metrics = text_metrics();
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    [NSColor.textBackgroundColor setFill];
    NSRectFill(dirty);

    const CGFloat gutter = [self gutterWidth];
    const CGFloat left = [self textLeft];
    const std::size_t first = static_cast<std::size_t>(std::max<CGFloat>(0, (NSMinY(dirty) - vertical_inset) / metrics.line_height));
    const std::size_t last = std::min(_editor.line_count(),
                                      static_cast<std::size_t>(std::max<CGFloat>(0, (NSMaxY(dirty) - vertical_inset) / metrics.line_height)) + 1);
    const auto [selection_from, selection_to] = _editor.selection();
    const std::size_t caret_line = _editor.line_of(_editor.caret());
    const BOOL focused = self.isFocused;

    [NSColor.windowBackgroundColor setFill];
    NSRectFill(NSMakeRect(NSMinX(dirty), NSMinY(dirty), std::max<CGFloat>(0, gutter - NSMinX(dirty)), NSHeight(dirty)));

    const CGFloat hairline = [self convertSizeFromBacking:NSMakeSize(1, 1)].width;
    std::vector<cedit::Span> spans;
    for (std::size_t line = first; line < last; ++line) {
        const CGFloat y = vertical_inset + static_cast<CGFloat>(line) * metrics.line_height;
        const NSRect row = NSMakeRect(gutter, y, NSWidth(self.bounds) - gutter, metrics.line_height);

        NSColor* rowColor = nil;
        for (const cedit::Diagnostic& diagnostic : _diagnostics)
            if (diagnostic.line == line && (!rowColor || diagnostic.error))
                rowColor = diagnostic.error ? [NSColor.systemRedColor colorWithAlphaComponent:0.18]
                                            : [NSColor.systemYellowColor colorWithAlphaComponent:0.22];
        if (!rowColor && line == caret_line && !_editor.has_selection())
            rowColor = [NSColor.labelColor colorWithAlphaComponent:0.05];
        if (rowColor) {
            [rowColor setFill];
            NSRectFillUsingOperation(row, NSCompositingOperationSourceOver);
        }
        [[NSColor.labelColor colorWithAlphaComponent:0.14] setFill];
        for (const std::size_t column : _guides[line])
            NSRectFillUsingOperation(NSMakeRect(left + static_cast<CGFloat>(column) * metrics.advance, y, hairline, metrics.line_height),
                                     NSCompositingOperationSourceOver);

        const std::size_t start = _editor.line_start(line);
        const std::size_t end = _editor.line_end(line);
        if (selection_from < selection_to && selection_from <= end && selection_to >= start) {
            const CGFloat x0 = left + static_cast<CGFloat>(_editor.column_of(std::max(selection_from, start))) * metrics.advance;
            CGFloat x1 = left + static_cast<CGFloat>(_editor.column_of(std::min(selection_to, end))) * metrics.advance;
            if (selection_to > end) x1 += metrics.advance;  // the selected line break
            [(focused ? NSColor.selectedTextBackgroundColor : NSColor.unemphasizedSelectedTextBackgroundColor) setFill];
            NSRectFill(NSMakeRect(x0, y, x1 - x0, metrics.line_height));
        }

        NSString* number = [NSString stringWithFormat:@"%zu", line + 1];
        cedit::mac::draw_plain(context, number, line == caret_line ? NSColor.labelColor : NSColor.tertiaryLabelColor,
                             NSMakePoint(gutter - gutter_padding - static_cast<CGFloat>(number.length) * metrics.advance, y));

        const std::string_view text = _editor.line(line);
        cedit::highlight_c(text, _states[line], spans);
        cedit::mac::draw_line(context, cedit::mac::display_line(text), spans, NSMakePoint(left, y));
    }

    if (_marked.length > 0) {
        const NSPoint point = [self pointForOffset:_editor.caret()];
        const NSRect box = NSMakeRect(point.x, point.y, static_cast<CGFloat>(_marked.length) * metrics.advance, metrics.line_height);
        [NSColor.textBackgroundColor setFill];
        NSRectFill(box);
        cedit::mac::draw_plain(context, _marked, NSColor.labelColor, point);
        [NSColor.secondaryLabelColor setFill];
        NSRectFill(NSMakeRect(box.origin.x, NSMaxY(box) - 2, box.size.width, 1));
    }
    if (_caretVisible && focused) {
        [NSColor.controlAccentColor setFill];
        NSRect caret = [self caretRect];
        caret.origin.x += 1;
        caret.size.width = 2;
        NSRectFill(caret);
    }
}

#pragma mark - Updates

// After every edit or caret move. What depends on the text is redone only when it really changed.
- (void)update {
    if (_seenRevision != _editor.revision()) {
        _seenRevision = _editor.revision();
        [self measure];
        _diagnostics.clear();
        if (self.onChange) self.onChange();
    }
    if (self.onCaretMove) self.onCaretMove();
    [self blink:self.isFocused];
    [self revealCaret];
    [self setNeedsDisplay:YES];
}

- (void)setDiagnostics:(std::vector<cedit::Diagnostic>)diagnostics {
    _diagnostics = std::move(diagnostics);
    [self setNeedsDisplay:YES];
}

- (void)goToLine:(std::size_t)line column:(std::size_t)column {
    line = std::min(line, _editor.line_count() - 1);
    _editor.set_caret(std::min(_editor.line_start(line) + column, _editor.line_end(line)), false);
    [self.window makeFirstResponder:self];
    [self update];
}

#pragma mark - Mouse

- (void)mouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const std::size_t offset = [self offsetForPoint:point];
    if (event.clickCount == 2)
        _editor.select_word(offset);
    else if (event.clickCount >= 3)
        _editor.select_line(_editor.line_of(offset));
    else
        _editor.set_caret(offset, (event.modifierFlags & NSEventModifierFlagShift) != 0);
    [self update];
}

- (void)mouseDragged:(NSEvent*)event {
    [self autoscroll:event];
    _editor.set_caret([self offsetForPoint:[self convertPoint:event.locationInWindow fromView:nil]], true);
    [self update];
}

- (void)resetCursorRects {
    [self addCursorRect:NSMakeRect([self gutterWidth], 0, NSWidth(self.bounds), NSHeight(self.bounds))
                 cursor:NSCursor.IBeamCursor];
}

#pragma mark - Keyboard

- (void)keyDown:(NSEvent*)event { [self interpretKeyEvents:@[ event ]]; }

- (void)moveBy:(cedit::Move)move extend:(bool)extend {
    const std::size_t page = static_cast<std::size_t>(std::max<CGFloat>(
        1, self.enclosingScrollView.contentSize.height / text_metrics().line_height - 1));
    _editor.move(move, extend, page);
    [self update];
}

// The standard macOS key bindings (StandardKeyBinding.dict) that make sense in a code editor.
- (void)doCommandBySelector:(SEL)selector {
    using cedit::Move;
    struct Binding {
        SEL selector;
        Move move;
        bool extend;
    };
    static const Binding moves[] = {
        {@selector(moveLeft:), Move::left, false},
        {@selector(moveBackward:), Move::left, false},
        {@selector(moveRight:), Move::right, false},
        {@selector(moveForward:), Move::right, false},
        {@selector(moveUp:), Move::up, false},
        {@selector(moveDown:), Move::down, false},
        {@selector(moveWordLeft:), Move::word_left, false},
        {@selector(moveWordRight:), Move::word_right, false},
        {@selector(moveToBeginningOfLine:), Move::line_start, false},
        {@selector(moveToLeftEndOfLine:), Move::line_start, false},
        {@selector(moveToBeginningOfParagraph:), Move::line_start, false},
        {@selector(moveToEndOfLine:), Move::line_end, false},
        {@selector(moveToRightEndOfLine:), Move::line_end, false},
        {@selector(moveToEndOfParagraph:), Move::line_end, false},
        {@selector(moveToBeginningOfDocument:), Move::document_start, false},
        {@selector(moveToEndOfDocument:), Move::document_end, false},
        {@selector(pageUp:), Move::page_up, false},
        {@selector(pageDown:), Move::page_down, false},
        {@selector(scrollPageUp:), Move::page_up, false},
        {@selector(scrollPageDown:), Move::page_down, false},
        {@selector(moveLeftAndModifySelection:), Move::left, true},
        {@selector(moveBackwardAndModifySelection:), Move::left, true},
        {@selector(moveRightAndModifySelection:), Move::right, true},
        {@selector(moveForwardAndModifySelection:), Move::right, true},
        {@selector(moveUpAndModifySelection:), Move::up, true},
        {@selector(moveDownAndModifySelection:), Move::down, true},
        {@selector(moveWordLeftAndModifySelection:), Move::word_left, true},
        {@selector(moveWordRightAndModifySelection:), Move::word_right, true},
        {@selector(moveToBeginningOfLineAndModifySelection:), Move::line_start, true},
        {@selector(moveToLeftEndOfLineAndModifySelection:), Move::line_start, true},
        {@selector(moveToBeginningOfParagraphAndModifySelection:), Move::line_start, true},
        {@selector(moveParagraphBackwardAndModifySelection:), Move::line_start, true},
        {@selector(moveToEndOfLineAndModifySelection:), Move::line_end, true},
        {@selector(moveToRightEndOfLineAndModifySelection:), Move::line_end, true},
        {@selector(moveToEndOfParagraphAndModifySelection:), Move::line_end, true},
        {@selector(moveParagraphForwardAndModifySelection:), Move::line_end, true},
        {@selector(moveToBeginningOfDocumentAndModifySelection:), Move::document_start, true},
        {@selector(moveToEndOfDocumentAndModifySelection:), Move::document_end, true},
        {@selector(pageUpAndModifySelection:), Move::page_up, true},
        {@selector(pageDownAndModifySelection:), Move::page_down, true},
    };
    for (const Binding& binding : moves) {
        if (binding.selector == selector) return [self moveBy:binding.move extend:binding.extend];
    }
    if (selector == @selector(scrollToBeginningOfDocument:) || selector == @selector(scrollToEndOfDocument:)) {
        // Home and End scroll without moving the caret, as everywhere on macOS.
        [self scrollPoint:NSMakePoint(0, selector == @selector(scrollToBeginningOfDocument:) ? 0 : NSHeight(self.bounds))];
        return;
    }
    if (selector == @selector(insertNewline:) || selector == @selector(insertNewlineIgnoringFieldEditor:)) {
        _editor.insert_newline();
    } else if (selector == @selector(insertTab:) || selector == @selector(insertTabIgnoringFieldEditor:)) {
        _editor.insert_tab();
    } else if (selector == @selector(deleteBackward:) || selector == @selector(deleteBackwardByDecomposingPreviousCharacter:)) {
        _editor.delete_backward();
    } else if (selector == @selector(deleteForward:)) {
        _editor.delete_forward();
    } else if (selector == @selector(deleteWordBackward:)) {
        if (!_editor.has_selection()) _editor.move(Move::word_left, true);
        _editor.delete_backward();
    } else if (selector == @selector(deleteWordForward:)) {
        if (!_editor.has_selection()) _editor.move(Move::word_right, true);
        _editor.delete_forward();
    } else if (selector == @selector(deleteToBeginningOfLine:)) {
        _editor.delete_to_line_start();
    } else if (selector == @selector(deleteToEndOfParagraph:)) {
        if (!_editor.has_selection()) _editor.move(Move::line_end, true);
        _editor.delete_forward();
    } else {
        return;  // unbound keys do nothing (no beep for Escape and friends)
    }
    [self update];
}

#pragma mark - NSTextInputClient

// Marked text is drawn at the caret and is not part of the text the ranges below describe.

- (NSUInteger)utf16Offset:(std::size_t)offset { return utf16_length(std::string_view(_editor.text()).substr(0, offset)); }

- (void)selectUTF16Range:(NSRange)range {
    if (NSEqualRanges(range, self.selectedRange)) return;  // keeps consecutive typing one undo step
    const std::string_view text = _editor.text();
    const std::size_t from = utf8_length(text, range.location);
    _editor.set_caret(from, false);
    _editor.set_caret(from + utf8_length(text.substr(from), range.length), true);
}

- (void)insertText:(id)text replacementRange:(NSRange)range {
    // Without marked text, a replacement range is text to replace, e.g. the letter the accent menu changes.
    if (_marked.length == 0 && range.location != NSNotFound) [self selectUTF16Range:range];
    _marked = @"";
    const std::string bytes = utf8_of(string_of(text));
    if (!bytes.empty()) _editor.insert(bytes);
    [self update];
}

- (void)setMarkedText:(id)text selectedRange:(NSRange)selected replacementRange:(NSRange)range {
    if (_marked.length == 0 && range.location != NSNotFound) [self selectUTF16Range:range];
    _marked = [string_of(text) copy];
    _markedSelection = selected;
    [self setNeedsDisplay:YES];
}

- (void)unmarkText {
    if (_marked.length == 0) return;
    NSString* text = _marked;
    _marked = @"";
    [self insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (BOOL)hasMarkedText { return _marked.length > 0; }

- (NSRange)markedRange {
    return _marked.length > 0 ? NSMakeRange([self utf16Offset:_editor.caret()], _marked.length) : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
    if (_marked.length > 0)
        return NSMakeRange([self utf16Offset:_editor.caret()] + _markedSelection.location, _markedSelection.length);
    const auto [from, to] = _editor.selection();
    const NSUInteger start = [self utf16Offset:from];
    return NSMakeRange(start, [self utf16Offset:to] - start);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    const std::string_view text = _editor.text();
    if (range.location == NSNotFound || range.location > utf16_length(text)) return nil;
    const std::size_t from = utf8_length(text, range.location);
    const std::string_view part = text.substr(from, utf8_length(text.substr(from), range.length));
    if (actual) *actual = NSMakeRange(range.location, utf16_length(part));
    NSString* string = [[NSString alloc] initWithBytes:part.data() length:part.size() encoding:NSUTF8StringEncoding];
    return [[NSAttributedString alloc] initWithString:string attributes:@{NSFontAttributeName : text_metrics().font}];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    if (!self.window) return NSNotFound;
    const NSPoint local = [self convertPoint:[self.window convertPointFromScreen:point] fromView:nil];
    return [self utf16Offset:[self offsetForPoint:local]];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual) *actual = range;
    NSRect rect = [self caretRect];
    if (_marked.length == 0 && range.location != NSNotFound) {
        // The range on its first line, e.g. the letter the accent menu offers to replace.
        const std::string_view text = _editor.text();
        const std::size_t from = utf8_length(text, range.location);
        const std::size_t to = std::min(from + utf8_length(text.substr(from), range.length), _editor.line_end(_editor.line_of(from)));
        const NSPoint start = [self pointForOffset:from];
        rect = NSMakeRect(start.x, start.y, [self pointForOffset:to].x - start.x, text_metrics().line_height);
    }
    return self.window ? [self.window convertRectToScreen:[self convertRect:rect toView:nil]] : NSZeroRect;
}

#pragma mark - Edit menu

- (void)undo:(id)sender {
    (void)sender;
    if (_editor.undo()) [self update];
}

- (void)redo:(id)sender {
    (void)sender;
    if (_editor.redo()) [self update];
}

- (void)copy:(id)sender {
    (void)sender;
    if (!_editor.has_selection()) return;
    const std::string_view selected = _editor.selected_text();
    NSString* text = [[NSString alloc] initWithBytes:selected.data() length:selected.size() encoding:NSUTF8StringEncoding];
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];
}

- (void)cut:(id)sender {
    if (!_editor.has_selection()) return;
    [self copy:sender];
    _editor.delete_backward();
    [self update];
}

- (void)paste:(id)sender {
    (void)sender;
    NSString* text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    if (!text) return;
    // Pasted Windows text arrives with CRLF; the editor stores '\n' only.
    text = [[text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"]
        stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
    _editor.insert(utf8_of(text));
    [self update];
}

- (void)selectAll:(id)sender {
    (void)sender;
    _editor.select_all();
    [self update];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    const SEL action = item.action;
    if (action == @selector(copy:) || action == @selector(cut:)) return _editor.has_selection();
    if (action == @selector(paste:)) return [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] != nil;
    return YES;
}

@end
