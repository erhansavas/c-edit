#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace cedit::utf8 {

constexpr char32_t replacement = U'�';

constexpr bool is_continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
}

// Byte offset of the code point boundary after the one starting at `offset`.
constexpr std::size_t next(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) return text.size();
    ++offset;
    while (offset < text.size() && is_continuation(text[offset])) ++offset;
    return offset;
}

// Byte offset of the code point boundary before `offset`.
constexpr std::size_t prev(std::string_view text, std::size_t offset) {
    if (offset == 0) return 0;
    --offset;
    while (offset > 0 && is_continuation(text[offset])) --offset;
    return offset;
}

struct Decoded {
    char32_t code_point;
    std::size_t length;  // bytes consumed; 1 for an invalid byte
};

// Strict decode of the code point at `offset`; invalid or truncated input yields U+FFFD over one byte.
constexpr Decoded decode(std::string_view text, std::size_t offset) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[offset + i]); };
    const std::size_t available = text.size() - offset;
    const unsigned char lead = byte(0);
    if (lead < 0x80u) return {lead, 1};
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2, value = lead & 0x1Fu, minimum = 0x80;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3, value = lead & 0x0Fu, minimum = 0x800;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4, value = lead & 0x07u, minimum = 0x10000;
    } else {
        return {replacement, 1};
    }
    if (available < length) return {replacement, 1};
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(i) & 0xC0u) != 0x80u) return {replacement, 1};
        value = (value << 6) | (byte(i) & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return {replacement, 1};
    return {value, length};
}

constexpr bool valid(std::string_view text) {
    for (std::size_t offset = 0; offset < text.size();) {
        const Decoded decoded = decode(text, offset);
        if (decoded.code_point == replacement && decoded.length == 1 &&
            static_cast<unsigned char>(text[offset]) >= 0x80u)
            return false;
        offset += decoded.length;
    }
    return true;
}

inline void append(std::string& out, char32_t code_point) {
    const auto put = [&](char32_t bits) { out.push_back(static_cast<char>(static_cast<unsigned char>(bits))); };
    if (code_point < 0x80) {
        put(code_point);
    } else if (code_point < 0x800) {
        put(0xC0 | (code_point >> 6));
        put(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        put(0xE0 | (code_point >> 12));
        put(0x80 | ((code_point >> 6) & 0x3F));
        put(0x80 | (code_point & 0x3F));
    } else {
        put(0xF0 | (code_point >> 18));
        put(0x80 | ((code_point >> 12) & 0x3F));
        put(0x80 | ((code_point >> 6) & 0x3F));
        put(0x80 | (code_point & 0x3F));
    }
}

}  // namespace cedit::utf8
