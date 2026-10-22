#pragma once

#import <AppKit/AppKit.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "core/syntax.hpp"

namespace cedit::mac {

// One monospaced font for every text surface; all layout is cell based.
struct TextMetrics {
    NSFont* font;
    CGFloat advance;      // width of one cell
    CGFloat line_height;
    CGFloat ascent;
};

const TextMetrics& text_metrics();

// Posted after the font size changes so every text surface can re-lay out.
extern NSNotificationName const text_metrics_changed;

// Grows (positive delta) or shrinks (negative delta) the monospaced font, clamped to a readable range.
void change_text_size(CGFloat delta);

NSColor* token_color(Token token);

// A source line laid out for display: tabs expanded to spaces, with the UTF-16 index of every byte offset.
struct DisplayLine {
    NSString* text;
    std::vector<NSUInteger> utf16_at_byte;  // size = line bytes + 1
};

DisplayLine display_line(std::string_view line);

// Draws `text` with its top-left at `origin` in a flipped context; spans are in byte offsets of the source line.
void draw_line(CGContextRef context, const DisplayLine& line, std::span<const Span> spans, NSPoint origin);

void draw_plain(CGContextRef context, NSString* text, NSColor* color, NSPoint origin);

}  // namespace cedit::mac
