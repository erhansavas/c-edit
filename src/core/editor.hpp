#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cedit {

constexpr std::size_t tab_width = 4;

enum class Move : std::uint8_t {
    left, right, up, down, word_left, word_right, line_start, line_end, document_start, document_end, page_up, page_down,
};

// UTF-8 text with '\n' line ends, a caret/anchor selection and linear undo. Offsets are bytes
// on code point boundaries; columns are display cells (one per code point, tabs to tab_width).
class Editor {
public:
    explicit Editor(std::string text = {});

    const std::string& text() const { return text_; }
    std::size_t line_count() const { return starts_.size(); }
    std::size_t line_start(std::size_t line) const { return starts_[line]; }
    std::size_t line_end(std::size_t line) const;
    std::string_view line(std::size_t line) const;
    std::size_t line_of(std::size_t offset) const;

    std::size_t caret() const { return caret_; }
    bool has_selection() const { return caret_ != anchor_; }
    std::pair<std::size_t, std::size_t> selection() const;
    std::string_view selected_text() const;

    bool modified() const { return state_ != saved_state_; }
    void mark_saved() {  // also ends the typing step, so undo returns exactly to the saved text
        saved_state_ = state_;
        typing_open_ = false;
    }
    std::uint64_t revision() const { return revision_; }

    // Replaces the selection; consecutive typing undoes as one step. A "}" typed on a blank line
    // lines up with its opening brace, a "{" with the "if (...)" or "else" above it.
    void insert(std::string_view text);
    // Keeps the indentation in front of the caret: one level deeper after '{' or a control line
    // ("if (...)", "else", ...), back to that line's level after its single statement.
    void insert_newline();
    void insert_tab();
    void delete_backward();
    void delete_forward();
    void delete_to_line_start();
    bool undo();
    bool redo();

    void move(Move move, bool extend, std::size_t page_lines = 0);
    void set_caret(std::size_t offset, bool extend);
    void select_all();
    void select_word(std::size_t offset);
    void select_line(std::size_t line);

    std::size_t column_of(std::size_t offset) const;
    std::size_t offset_at(std::size_t line, std::size_t column) const;

    // Per line, the columns of its indentation guides: the indentation of each enclosing line that
    // opens a block ('{', "if (...)", "else", ...), column 0 left out. A blank line carries the guides
    // its neighbours share.
    std::vector<std::vector<std::size_t>> indent_guides() const;

private:
    static constexpr std::size_t npos = std::string_view::npos;

    struct Edit {
        std::size_t position;
        std::string removed;
        std::string inserted;
        std::size_t caret_before, anchor_before;
        std::uint64_t state_before, state_after;
    };

    void replace(std::size_t from, std::size_t to, std::string_view with, bool typing);
    void apply(std::size_t position, std::size_t remove_length, std::string_view with);
    void rebuild_lines();
    std::string_view indentation(std::size_t line) const;
    std::size_t statement_header(std::size_t line) const;  // the control line above, if any
    // The line of the '{' a '}' at the start of `line` closes, if any; braces in quotes or // comments
    // (printf("{")) do not count.
    std::size_t opening_line(std::size_t line) const;
    std::size_t clamp_boundary(std::size_t offset) const;
    std::size_t word_left(std::size_t offset) const;
    std::size_t word_right(std::size_t offset) const;

    std::string text_;
    std::vector<std::size_t> starts_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    std::size_t preferred_column_ = 0;
    bool has_preferred_column_ = false;
    std::vector<Edit> undo_;
    std::vector<Edit> redo_;
    bool typing_open_ = false;
    std::uint64_t state_ = 0;
    std::uint64_t saved_state_ = 0;
    std::uint64_t next_state_ = 1;
    std::uint64_t revision_ = 0;
};

}  // namespace cedit
