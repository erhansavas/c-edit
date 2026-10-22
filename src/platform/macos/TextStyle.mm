#import "TextStyle.h"

#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>

#include "core/editor.hpp"
#include "core/utf8.hpp"

namespace cedit::mac {

NSNotificationName const text_metrics_changed = @"CETextMetricsChanged";

namespace {

constexpr CGFloat default_text_size = 13;
constexpr CGFloat min_text_size = 9;
constexpr CGFloat max_text_size = 36;

TextMetrics make_metrics(CGFloat size) {
    NSFont* font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    const CGFloat advance = [@"0" sizeWithAttributes:@{NSFontAttributeName : font}].width;
    const CGFloat ascent = std::ceil(font.ascender);
    const CGFloat height = ascent + std::ceil(-font.descender) + std::ceil(font.leading) + 3;
    return TextMetrics{font, advance, height, ascent + 1};
}

TextMetrics& metrics_slot() {
    static TextMetrics metrics = make_metrics(default_text_size);
    return metrics;
}

}  // namespace

const TextMetrics& text_metrics() { return metrics_slot(); }

void change_text_size(CGFloat delta) {
    const CGFloat size = std::clamp(metrics_slot().font.pointSize + delta, min_text_size, max_text_size);
    if (size == metrics_slot().font.pointSize) return;
    metrics_slot() = make_metrics(size);
    [NSNotificationCenter.defaultCenter postNotificationName:text_metrics_changed object:nil];
}

NSColor* token_color(Token token) {
    switch (token) {
    case Token::plain: return NSColor.labelColor;
    case Token::keyword: return NSColor.systemPinkColor;
    case Token::type: return NSColor.systemPurpleColor;
    case Token::preprocessor: return NSColor.systemOrangeColor;
    case Token::string: return NSColor.systemRedColor;
    case Token::character: return NSColor.systemRedColor;
    case Token::comment: return NSColor.systemGreenColor;
    case Token::number: return NSColor.systemBlueColor;
    }
    return NSColor.labelColor;
}

DisplayLine display_line(std::string_view line) {
    std::vector<unichar> units;
    units.reserve(line.size());
    std::vector<NSUInteger> index(line.size() + 1, 0);
    std::size_t column = 0;
    for (std::size_t offset = 0; offset < line.size();) {
        const utf8::Decoded decoded = utf8::decode(line, offset);
        for (std::size_t i = 0; i < decoded.length; ++i) index[offset + i] = units.size();
        if (decoded.code_point == U'\t') {
            const std::size_t spaces = tab_width - column % tab_width;
            units.insert(units.end(), spaces, u' ');
            column += spaces;
        } else {
            if (decoded.code_point >= 0x10000) {
                const char32_t value = decoded.code_point - 0x10000;
                units.push_back(static_cast<unichar>(0xD800 + (value >> 10)));
                units.push_back(static_cast<unichar>(0xDC00 + (value & 0x3FF)));
            } else {
                units.push_back(static_cast<unichar>(decoded.code_point));
            }
            ++column;
        }
        offset += decoded.length;
    }
    index[line.size()] = units.size();
    return {[NSString stringWithCharacters:units.data() length:units.size()], std::move(index)};
}

namespace {

void draw_attributed(CGContextRef context, NSAttributedString* text, NSPoint origin) {
    const CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)text);
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformMakeScale(1, -1));
    CGContextSetTextPosition(context, origin.x, origin.y + text_metrics().ascent);
    CTLineDraw(line, context);
    CGContextRestoreGState(context);
    CFRelease(line);
}

}  // namespace

void draw_line(CGContextRef context, const DisplayLine& line, std::span<const Span> spans, NSPoint origin) {
    if (line.text.length == 0) return;
    NSMutableAttributedString* text = [[NSMutableAttributedString alloc]
        initWithString:line.text
            attributes:@{
                (id)kCTFontAttributeName : text_metrics().font,
                (id)kCTForegroundColorAttributeName : (id)token_color(Token::plain).CGColor,
            }];
    for (const Span& span : spans) {
        const NSUInteger begin = line.utf16_at_byte[span.begin];
        const NSUInteger end = line.utf16_at_byte[span.end];
        [text addAttribute:(id)kCTForegroundColorAttributeName
                     value:(id)token_color(span.token).CGColor
                     range:NSMakeRange(begin, end - begin)];
    }
    draw_attributed(context, text, origin);
}

void draw_plain(CGContextRef context, NSString* text, NSColor* color, NSPoint origin) {
    if (text.length == 0) return;
    draw_attributed(context,
                    [[NSAttributedString alloc] initWithString:text
                                                    attributes:@{
                                                        (id)kCTFontAttributeName : text_metrics().font,
                                                        (id)kCTForegroundColorAttributeName : (id)color.CGColor,
                                                    }],
                    origin);
}

}  // namespace cedit::mac
