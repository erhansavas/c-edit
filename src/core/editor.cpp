#include "editor.hpp"

#include <algorithm>

#include "utf8.hpp"

namespace cedit {
namespace {

bool is_word(std::string_view text, std::size_t offset) {
    const auto byte = static_cast<unsigned char>(text[offset]);
    if (byte >= 0x80u) return true;  // non-ASCII letters
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") + 1 - first);
}

bool starts_with_word(std::string_view text, std::string_view word) {
    return text.starts_with(word) && (text.size() == word.size() || !is_word(text, word.size()));
}

// Visits the offset of each byte of `line` that is code: outside "..." and '...' and before a //
// comment. Returns where that comment starts, or the line's size.
template <typename Visit>
std::size_t scan_code(std::string_view line, Visit visit) {
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (quote && line[i] == '\\') ++i;
        else if (quote) quote = line[i] == quote ? 0 : quote;
        else if (line[i] == '"' || line[i] == '\'') quote = line[i];
        else if (line.substr(i, 2) == "//") return i;
        else visit(i);
    }
    return line.size();
}

// The line without a trailing // comment (outside quotes) and without surrounding blanks.
std::string_view code_of(std::string_view line) { return trimmed(line.substr(0, scan_code(line, [](std::size_t) {}))); }

// "if (...)", "else if (...)", "for (...)", "while (...)", "else" or "do" with nothing after it,
// also after a closing "}": a control line whose single statement goes on the next line.
bool opens_statement(std::string_view line) {
    line = code_of(line);
    if (line.starts_with('}')) line = trimmed(line.substr(1));
    if (line == "else" || line == "do") return true;
    if (starts_with_word(line, "else")) line = trimmed(line.substr(4));
    std::size_t keyword = 0;
    for (const std::string_view word : {"if", "for", "while"})
        if (starts_with_word(line, word)) keyword = word.size();
    if (keyword == 0 || !line.ends_with(')') || !trimmed(line.substr(keyword)).starts_with('(')) return false;
    // The condition's ')' must end the line; parentheses in quotes, as in ')', do not count.
    std::size_t depth = 0;
    std::size_t close = std::string_view::npos;
    scan_code(line, [&](std::size_t i) {
        if (i < keyword || close != std::string_view::npos) return;
        if (line[i] == '(') ++depth;
        if (line[i] == ')' && --depth == 0) close = i;
    });
    return close != std::string_view::npos && close + 1 == line.size();
}

// Whether the line's code ends with '{'; one in quotes, as in printf("{, does not count.
bool ends_with_brace(std::string_view line) {
    std::size_t last = std::string_view::npos;
    scan_code(line, [&](std::size_t i) {
        if (line[i] != ' ' && line[i] != '\t') last = i;
    });
    return last != std::string_view::npos && line[last] == '{';
}

// Whether the byte at `offset` of `line` is code, not inside quotes or a // comment.
bool is_code(std::string_view line, std::size_t offset) {
    bool code = false;
    scan_code(line, [&](std::size_t i) { code = code || i == offset; });
    return code;
}

bool opens_block(std::string_view line) { return ends_with_brace(line) || opens_statement(line); }

}  // namespace

Editor::Editor(std::string text) : text_(std::move(text)) { rebuild_lines(); }

void Editor::rebuild_lines() {
    starts_.clear();
    starts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i)
        if (text_[i] == '\n') starts_.push_back(i + 1);
}

std::size_t Editor::line_end(std::size_t line) const {
    return line + 1 < starts_.size() ? starts_[line + 1] - 1 : text_.size();
}

std::string_view Editor::line(std::size_t line) const {
    return std::string_view(text_).substr(starts_[line], line_end(line) - starts_[line]);
}

std::size_t Editor::line_of(std::size_t offset) const {
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    return static_cast<std::size_t>(it - starts_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> Editor::selection() const { return std::minmax(caret_, anchor_); }

std::string_view Editor::selected_text() const {
    const auto [from, to] = selection();
    return std::string_view(text_).substr(from, to - from);
}

std::size_t Editor::clamp_boundary(std::size_t offset) const {
    offset = std::min(offset, text_.size());
    while (offset > 0 && offset < text_.size() && utf8::is_continuation(text_[offset])) --offset;
    return offset;
}

void Editor::apply(std::size_t position, std::size_t remove_length, std::string_view with) {
    text_.replace(position, remove_length, with);
    rebuild_lines();
    ++revision_;
}

void Editor::replace(std::size_t from, std::size_t to, std::string_view with, bool typing) {
    if (from == to && with.empty()) return;
    redo_.clear();
    const bool extend = typing && typing_open_ && !undo_.empty() && from == to &&
                        undo_.back().position + undo_.back().inserted.size() == from && undo_.back().removed.empty();
    if (extend) {
        undo_.back().inserted.append(with);
        apply(from, 0, with);
        undo_.back().state_after = state_ = next_state_++;
    } else {
        Edit edit{from, text_.substr(from, to - from), std::string(with), caret_, anchor_, state_, next_state_++};
        apply(from, to - from, with);
        state_ = edit.state_after;
        undo_.push_back(std::move(edit));
    }
    typing_open_ = typing && with.find('\n') == std::string_view::npos;
    caret_ = anchor_ = from + with.size();
    has_preferred_column_ = false;
}

std::string_view Editor::indentation(std::size_t line) const {
    const std::string_view text = this->line(line);
    return text.substr(0, std::min(text.find_first_not_of(" \t"), text.size()));
}

std::size_t Editor::statement_header(std::size_t line) const {
    while (line-- > 0)
        if (!trimmed(this->line(line)).empty()) return opens_statement(this->line(line)) ? line : npos;
    return npos;
}

std::size_t Editor::opening_line(std::size_t offset) const {
    std::size_t depth = 0;
    std::string braces;
    for (std::size_t above = line_of(offset) + 1; above-- > 0;) {
        const std::string_view text = this->line(above).substr(0, offset - starts_[above]);
        braces.clear();
        scan_code(text, [&](std::size_t i) {
            if (text[i] == '{' || text[i] == '}') braces.push_back(text[i]);
        });
        for (auto brace = braces.rbegin(); brace != braces.rend(); ++brace)
            if (*brace == '}') ++depth;
            else if (depth-- == 0) return above;
    }
    return npos;
}

void Editor::insert(std::string_view text) {
    const auto [from, to] = selection();
    const std::size_t line = line_of(from);
    const std::size_t start = starts_[line];
    const bool blank_before = from == to && start + indentation(line).size() >= from;
    if (text == "}" && blank_before) {
        // A closing brace typed on a blank line lines up with the line of its opening brace.
        const std::size_t opening = opening_line(from);
        if (opening != npos && indentation(opening) != std::string_view(text_).substr(start, from - start))
            return replace(start, from, std::string(indentation(opening)) + "}", false);
    }
    if (text == "{" && blank_before) {
        // An opening brace on its own line under "if (...)" or "else" lines up with that line.
        const std::size_t header = statement_header(line);
        if (header != npos && indentation(header) != std::string_view(text_).substr(start, from - start))
            return replace(start, from, std::string(indentation(header)) + "{", false);
    }
    replace(from, to, text, text.size() <= 4 && from == to);
}

void Editor::insert_newline() {
    const auto [from, to] = selection();
    const std::size_t line = line_of(from);
    const std::size_t start = starts_[line];
    const std::string_view before = std::string_view(text_).substr(start, from - start);
    const std::string_view code = code_of(before);
    const bool brace = ends_with_brace(before);
    std::string text = "\n";
    text += before.substr(0, std::min(before.find_first_not_of(" \t"), before.size()));
    // One level deeper after '{' or a control line; after the single statement under a control
    // line, back to the level of that line.
    if (brace || opens_statement(code)) {
        text.append(tab_width, ' ');
    } else if (code.ends_with(';')) {
        const std::size_t header = statement_header(line);
        if (header != npos && column_of(starts_[header] + indentation(header).size()) < column_of(start + text.size() - 1))
            text = "\n" + std::string(indentation(header));
    }
    // A '}' after the caret goes to a line of its own, lined up with its '{'; between "{" and "}"
    // the caret gets the indented line in the middle.
    const std::size_t to_line = line_of(to);
    std::size_t after = to;
    while (after < line_end(to_line) && (text_[after] == ' ' || text_[after] == '\t')) ++after;
    if (after < line_end(to_line) && text_[after] == '}' && is_code(this->line(to_line), after - starts_[to_line])) {
        const std::size_t opening = opening_line(after);
        if (opening != npos) {
            const std::string closing = "\n" + std::string(indentation(opening));
            if (!brace) return replace(from, after, closing, false);
            replace(from, after, text + closing, false);
            caret_ = anchor_ = from + text.size();
            return;
        }
    }
    replace(from, to, text, false);
}

std::vector<std::vector<std::size_t>> Editor::indent_guides() const {
    std::vector<std::vector<std::size_t>> guides(starts_.size());
    std::vector<std::size_t> own(starts_.size(), npos);  // indentation column, npos on a blank line
    std::vector<std::size_t> open;                       // indentation of the enclosing block lines
    for (std::size_t line = 0; line < starts_.size(); ++line) {
        if (trimmed(this->line(line)).empty()) continue;
        own[line] = column_of(starts_[line] + indentation(line).size());
        while (!open.empty() && open.back() >= own[line]) open.pop_back();
        for (const std::size_t column : open)
            if (column > 0) guides[line].push_back(column);
        if (opens_block(this->line(line))) open.push_back(own[line]);
    }
    // Blank lines, bottom up: each takes from the first non-blank line below it, up to the lesser
    // indentation of the lines around it.
    std::vector<std::size_t> above(starts_.size(), 0);
    for (std::size_t line = 1; line < starts_.size(); ++line)
        above[line] = own[line - 1] != npos ? own[line - 1] : above[line - 1];
    std::size_t below = npos;
    for (std::size_t line = starts_.size(); line-- > 0;) {
        if (own[line] != npos) {
            below = line;
            continue;
        }
        if (below == npos) continue;
        for (const std::size_t column : guides[below])
            if (column < std::min(above[line], own[below])) guides[line].push_back(column);
    }
    return guides;
}

void Editor::insert_tab() {
    const std::size_t column = column_of(selection().first);
    insert(std::string(tab_width - column % tab_width, ' '));
}

void Editor::delete_backward() {
    if (has_selection()) {
        const auto [from, to] = selection();
        replace(from, to, {}, false);
    } else if (caret_ > 0) {
        const std::size_t line = line_of(caret_);
        const std::size_t start = starts_[line];
        std::size_t from = utf8::prev(text_, caret_);
        if (caret_ == start && trimmed(this->line(line - 1)).empty()) {
            from = starts_[line - 1];  // a blank line goes with its spaces, so this line keeps its own indentation
        } else if (start + indentation(line).size() >= caret_ && text_[caret_ - 1] == ' ') {
            // In the indentation, spaces go back one level at a time: to the previous multiple of tab_width.
            from = caret_;
            for (std::size_t count = (column_of(caret_) - 1) % tab_width + 1; count > 0 && text_[from - 1] == ' '; --count) --from;
        }
        replace(from, caret_, {}, false);
    }
}

void Editor::delete_forward() {
    if (has_selection()) {
        const auto [from, to] = selection();
        replace(from, to, {}, false);
    } else if (caret_ < text_.size()) {
        // At the end of a blank line the line goes with its spaces, so the next line keeps its own indentation.
        const std::size_t line = line_of(caret_);
        if (caret_ == line_end(line) && trimmed(this->line(line)).empty())
            return replace(starts_[line], caret_ + 1, {}, false);
        replace(caret_, utf8::next(text_, caret_), {}, false);
    }
}

void Editor::delete_to_line_start() {
    const std::size_t start = starts_[line_of(caret_)];
    if (has_selection() || start == caret_) return delete_backward();
    replace(start, caret_, {}, false);
}

bool Editor::undo() {
    if (undo_.empty()) return false;
    Edit edit = std::move(undo_.back());
    undo_.pop_back();
    apply(edit.position, edit.inserted.size(), edit.removed);
    caret_ = edit.caret_before;
    anchor_ = edit.anchor_before;
    state_ = edit.state_before;
    redo_.push_back(std::move(edit));
    typing_open_ = has_preferred_column_ = false;
    return true;
}

bool Editor::redo() {
    if (redo_.empty()) return false;
    Edit edit = std::move(redo_.back());
    redo_.pop_back();
    apply(edit.position, edit.removed.size(), edit.inserted);
    caret_ = anchor_ = edit.position + edit.inserted.size();
    state_ = edit.state_after;
    undo_.push_back(std::move(edit));
    typing_open_ = has_preferred_column_ = false;
    return true;
}

std::size_t Editor::column_of(std::size_t offset) const {
    const std::string_view text(text_);
    std::size_t column = 0;
    for (std::size_t i = starts_[line_of(offset)]; i < offset; i = utf8::next(text, i))
        column = text[i] == '\t' ? column + tab_width - column % tab_width : column + 1;
    return column;
}

std::size_t Editor::offset_at(std::size_t line, std::size_t column) const {
    line = std::min(line, starts_.size() - 1);
    const std::string_view text(text_);
    const std::size_t end = line_end(line);
    std::size_t current = 0;
    std::size_t offset = starts_[line];
    while (offset < end) {
        const std::size_t width = text[offset] == '\t' ? tab_width - current % tab_width : 1;
        if (current + (width + 1) / 2 > column) break;  // nearer to the left edge of this cell
        current += width;
        offset = utf8::next(text, offset);
    }
    return offset;
}

std::size_t Editor::word_left(std::size_t offset) const {
    while (offset > 0 && !is_word(text_, utf8::prev(text_, offset))) offset = utf8::prev(text_, offset);
    while (offset > 0 && is_word(text_, utf8::prev(text_, offset))) offset = utf8::prev(text_, offset);
    return offset;
}

std::size_t Editor::word_right(std::size_t offset) const {
    while (offset < text_.size() && !is_word(text_, offset)) offset = utf8::next(text_, offset);
    while (offset < text_.size() && is_word(text_, offset)) offset = utf8::next(text_, offset);
    return offset;
}

void Editor::move(Move move, bool extend, std::size_t page_lines) {
    typing_open_ = false;
    const auto [from, to] = selection();
    const bool vertical = move == Move::up || move == Move::down || move == Move::page_up || move == Move::page_down;
    if (vertical && !has_preferred_column_) {
        preferred_column_ = column_of(caret_);
        has_preferred_column_ = true;
    }
    const std::size_t line = line_of(caret_);
    std::size_t target = caret_;
    switch (move) {
    case Move::left: target = has_selection() && !extend ? from : utf8::prev(text_, caret_); break;
    case Move::right: target = has_selection() && !extend ? to : utf8::next(text_, caret_); break;
    case Move::word_left: target = word_left(caret_); break;
    case Move::word_right: target = word_right(caret_); break;
    case Move::line_start: {
        // First press goes to the first non-blank character, the next to column 0.
        std::size_t first = starts_[line];
        while (first < line_end(line) && (text_[first] == ' ' || text_[first] == '\t')) ++first;
        target = caret_ == first ? starts_[line] : first;
        break;
    }
    case Move::line_end: target = line_end(line); break;
    case Move::document_start: target = 0; break;
    case Move::document_end: target = text_.size(); break;
    case Move::up: target = line == 0 ? 0 : offset_at(line - 1, preferred_column_); break;
    case Move::down: target = line + 1 >= starts_.size() ? text_.size() : offset_at(line + 1, preferred_column_); break;
    case Move::page_up: target = offset_at(line > page_lines ? line - page_lines : 0, preferred_column_); break;
    case Move::page_down: target = offset_at(line + page_lines, preferred_column_); break;
    }
    caret_ = target;
    if (!extend) anchor_ = caret_;
    if (!vertical) has_preferred_column_ = false;
}

void Editor::set_caret(std::size_t offset, bool extend) {
    typing_open_ = has_preferred_column_ = false;
    caret_ = clamp_boundary(offset);
    if (!extend) anchor_ = caret_;
}

void Editor::select_all() {
    typing_open_ = has_preferred_column_ = false;
    anchor_ = 0;
    caret_ = text_.size();
}

void Editor::select_word(std::size_t offset) {
    offset = clamp_boundary(offset);
    typing_open_ = has_preferred_column_ = false;
    if (offset < text_.size() && is_word(text_, offset)) {
        anchor_ = word_left(utf8::next(text_, offset));
        caret_ = word_right(offset);
    } else {
        anchor_ = offset;
        caret_ = utf8::next(text_, offset);
    }
}

void Editor::select_line(std::size_t line) {
    line = std::min(line, starts_.size() - 1);
    typing_open_ = has_preferred_column_ = false;
    anchor_ = starts_[line];
    caret_ = line + 1 < starts_.size() ? starts_[line + 1] : text_.size();
}

}  // namespace cedit
