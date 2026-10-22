#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cedit {

enum class Token : std::uint8_t { plain, keyword, type, preprocessor, string, character, comment, number };

struct Span {
    std::size_t begin;  // byte offsets within the line
    std::size_t end;
    Token token;
};

// Carried from one line to the next: whether the line starts inside a /* comment */.
struct LineState {
    bool in_comment = false;
    friend bool operator==(LineState, LineState) = default;
};

// C highlighting for one line; spans cover only non-plain tokens, in order. Returns the next line's state.
LineState highlight_c(std::string_view line, LineState state, std::vector<Span>& spans);

}  // namespace cedit
