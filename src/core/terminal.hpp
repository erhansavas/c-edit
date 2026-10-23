#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace cedit {

// The console screen of a running program: UTF-8 output plus the few control sequences programs
// code produces (CR, LF, BS, TAB, clear screen, cursor home/position, erase line). Other
// escape sequences are consumed and ignored. Lines wrap at `columns`, as in a real console,
// so a program printing in an endless loop only ever fills the bounded scrollback.
class Terminal {
public:
    static constexpr std::size_t columns = 80;
    static constexpr std::size_t max_lines = 10'000;

    Terminal() : lines_(1) {}

    void feed(std::string_view bytes);

    const std::deque<std::u32string>& lines() const { return lines_; }
    std::size_t cursor_line() const { return line_; }
    std::size_t cursor_column() const { return column_; }
    std::size_t bells() const { return bells_; }  // '\a' received so far; the console beeps for each new one

private:
    enum class State : std::uint8_t { ground, escape, csi };

    void clear();
    void put(char32_t code_point);
    void line_feed();
    void control_sequence(char final_byte);
    std::size_t parameter(std::size_t index, std::size_t fallback) const;

    std::deque<std::u32string> lines_;
    std::size_t line_ = 0;
    std::size_t bells_ = 0;
    std::size_t column_ = 0;  // == columns after the last cell of a line was written: the next character wraps
    State state_ = State::ground;
    std::string parameters_;
    std::string pending_utf8_;
};

}  // namespace cedit
