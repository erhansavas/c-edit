#include "terminal.hpp"

#include <algorithm>
#include <charconv>

#include "utf8.hpp"

namespace cedit {

void Terminal::clear() {
    lines_.assign(1, {});
    line_ = column_ = 0;
}

void Terminal::line_feed() {
    if (++line_ == lines_.size()) lines_.emplace_back();
    if (lines_.size() > max_lines) {
        lines_.pop_front();
        --line_;
    }
}

void Terminal::put(char32_t code_point) {
    if (column_ >= columns) {
        line_feed();
        column_ = 0;
    }
    std::u32string& line = lines_[line_];
    if (line.size() < column_) line.resize(column_, U' ');
    if (column_ == line.size())
        line.push_back(code_point);
    else
        line[column_] = code_point;
    ++column_;
}

std::size_t Terminal::parameter(std::size_t index, std::size_t fallback) const {
    std::string_view rest(parameters_);
    for (std::size_t i = 0; i < index; ++i) {
        const std::size_t separator = rest.find(';');
        if (separator == std::string_view::npos) return fallback;
        rest.remove_prefix(separator + 1);
    }
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    return error != std::errc{} || end == rest.data() || value == 0 ? fallback : value;
}

// Cursor positions stay on the screen width and within the scrollback, whatever a program asks for.
void Terminal::control_sequence(char final_byte) {
    switch (final_byte) {
    case 'J':
        if (parameter(0, 0) >= 2) clear();
        break;
    case 'H':
    case 'f':
        line_ = std::min(parameter(0, 1), max_lines) - 1;
        column_ = std::min(parameter(1, 1), columns) - 1;
        while (lines_.size() <= line_) lines_.emplace_back();
        break;
    case 'K':
        if (lines_[line_].size() > column_) lines_[line_].resize(column_);
        break;
    case 'A': line_ -= std::min(line_, parameter(0, 1)); break;
    case 'B':
        for (std::size_t n = std::min(parameter(0, 1), max_lines); n > 0; --n) line_feed();
        break;
    case 'C': column_ = std::min(column_ + std::min(parameter(0, 1), columns), columns - 1); break;
    case 'D': column_ -= std::min(column_, parameter(0, 1)); break;
    default: break;  // colours and modes
    }
}

void Terminal::feed(std::string_view bytes) {
    std::string_view text = bytes;
    if (!pending_utf8_.empty()) {
        pending_utf8_.append(bytes);
        text = pending_utf8_;
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto byte = static_cast<unsigned char>(text[offset]);
        if (state_ == State::escape) {
            state_ = byte == '[' ? State::csi : State::ground;
            if (state_ == State::csi) parameters_.clear();
            ++offset;
            continue;
        }
        if (state_ == State::csi) {
            if (byte >= 0x40 && byte <= 0x7E) {
                state_ = State::ground;
                control_sequence(static_cast<char>(byte));
            } else {
                parameters_.push_back(static_cast<char>(byte));
            }
            ++offset;
            continue;
        }
        if (byte < 0x80) {
            switch (byte) {
            case '\n': line_feed(); column_ = 0; break;
            case '\r': column_ = 0; break;
            case '\b': column_ -= column_ > 0 ? 1 : 0; break;
            case '\t':
                if (column_ < columns) column_ = std::min(column_ + 8 - column_ % 8, columns - 1);
                break;
            case 0x1B: state_ = State::escape; break;
            default:
                if (byte >= 0x20 && byte != 0x7F) put(byte);
                break;
            }
            ++offset;
            continue;
        }
        const std::size_t needed = (byte & 0xE0u) == 0xC0u ? 2 : (byte & 0xF0u) == 0xE0u ? 3 : (byte & 0xF8u) == 0xF0u ? 4 : 1;
        if (offset + needed > text.size()) break;  // wait for the rest of this code point
        const utf8::Decoded decoded = utf8::decode(text, offset);
        put(decoded.code_point);
        offset += decoded.length;
    }
    pending_utf8_ = std::string(text.substr(offset));
}

}  // namespace cedit
