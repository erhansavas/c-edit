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

void Editor::insert(std::string_view text) {
    const auto [from, to] = selection();
    replace(from, to, text, text.size() <= 4 && from == to);
}

void Editor::insert_newline() {
    const auto [from, to] = selection();
    std::string text = "\n";
    for (std::size_t i = starts_[line_of(from)]; i < from && (text_[i] == ' ' || text_[i] == '\t'); ++i)
        text.push_back(text_[i]);
    replace(from, to, text, false);
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
        replace(utf8::prev(text_, caret_), caret_, {}, false);
    }
}

void Editor::delete_forward() {
    if (has_selection()) {
        const auto [from, to] = selection();
        replace(from, to, {}, false);
    } else if (caret_ < text_.size()) {
        replace(caret_, utf8::next(text_, caret_), {}, false);
    }
}

void Editor::delete_to_line_start() {
    if (has_selection()) return delete_backward();
    const std::size_t start = starts_[line_of(caret_)];
    replace(start == caret_ && caret_ > 0 ? caret_ - 1 : start, caret_, {}, false);
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
