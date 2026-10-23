#include "syntax.hpp"

#include <algorithm>
#include <array>

namespace cedit {
namespace {

constexpr std::array<std::string_view, 30> keywords = {
    "auto",   "break",    "case",     "const",  "continue", "default", "do",     "else",   "enum",    "extern",
    "for",    "goto",     "if",       "inline", "register", "restrict", "return", "sizeof", "static",  "struct",
    "switch", "typedef",  "union",    "volatile", "while",  "true",    "false",  "NULL",   "_Alignof", "_Static_assert",
};

constexpr std::array<std::string_view, 21> types = {
    "bool",    "char",    "double",  "float",   "int",     "long",     "short",    "signed",   "unsigned", "void", "_Bool",
    "size_t",  "FILE",    "int8_t",  "int16_t", "int32_t", "int64_t",  "uint8_t",  "uint16_t", "uint32_t", "uint64_t",
};

constexpr bool identifier_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
constexpr bool identifier_part(char c) { return identifier_start(c) || (c >= '0' && c <= '9'); }

template <std::size_t N>
constexpr bool contains(const std::array<std::string_view, N>& words, std::string_view word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

}  // namespace

LineState highlight_c(std::string_view line, LineState state, std::vector<Span>& spans) {
    spans.clear();
    const std::size_t size = line.size();
    std::size_t i = 0;
    const auto add = [&](std::size_t begin, std::size_t end, Token token) {
        if (end > begin) spans.push_back({begin, end, token});
    };
    const auto skip_comment = [&](std::size_t begin) {
        const std::size_t close = line.find("*/", i);
        i = close == std::string_view::npos ? size : close + 2;
        state.in_comment = close == std::string_view::npos;
        add(begin, i, Token::comment);
    };
    // A // comment goes on while its lines end with a backslash (spaces after it still count).
    const std::size_t last = line.find_last_not_of(" \t");
    const bool spliced = last != std::string_view::npos && line[last] == '\\';
    if (state.in_line_comment) {
        add(0, size, Token::comment);
        state.in_line_comment = spliced;
        return state;
    }
    if (state.in_comment) skip_comment(0);

    std::size_t first = line.find_first_not_of(" \t");
    const bool directive = first != std::string_view::npos && line[first] == '#' && i <= first;
    if (directive) {
        // "#include" and the directive name; the rest of the line is tokenized normally.
        std::size_t end = first + 1;
        while (end < size && (line[end] == ' ' || line[end] == '\t')) ++end;
        while (end < size && identifier_part(line[end])) ++end;
        add(first, end, Token::preprocessor);
        i = end;
        const std::size_t header = line.find_first_not_of(" \t", i);
        if (header != std::string_view::npos && line[header] == '<') {
            const std::size_t close = line.find('>', header);
            i = close == std::string_view::npos ? size : close + 1;
            add(header, i, Token::string);
        }
    }

    while (i < size) {
        const char c = line[i];
        const std::size_t begin = i;
        if (c == '/' && i + 1 < size && line[i + 1] == '/') {
            add(begin, size, Token::comment);
            state.in_line_comment = spliced;
            break;
        }
        if (c == '/' && i + 1 < size && line[i + 1] == '*') {
            i += 2;
            skip_comment(begin);
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < size && line[i] != c) i += line[i] == '\\' && i + 1 < size ? 2 : 1;
            i = std::min(i + 1, size);
            add(begin, i, c == '"' ? Token::string : Token::character);
            continue;
        }
        if (c >= '0' && c <= '9') {
            while (i < size && (identifier_part(line[i]) || line[i] == '.')) ++i;
            add(begin, i, Token::number);
            continue;
        }
        if (identifier_start(c)) {
            while (i < size && identifier_part(line[i])) ++i;
            const std::string_view word = line.substr(begin, i - begin);
            if (contains(keywords, word))
                add(begin, i, Token::keyword);
            else if (contains(types, word))
                add(begin, i, Token::type);
            continue;
        }
        ++i;
    }
    return state;
}

}  // namespace cedit
