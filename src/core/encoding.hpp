#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace cedit {

enum class Encoding : std::uint8_t { utf8, windows1254 };

// How a file is stored on disk. The editor always works on UTF-8 text with '\n' line ends.
struct FileFormat {
    Encoding encoding = Encoding::utf8;
    bool byte_order_mark = false;
    bool crlf = false;
    friend bool operator==(const FileFormat&, const FileFormat&) = default;
};

// How Turkish Windows saves text files; new files use it, so they open unchanged on Windows PCs.
constexpr FileFormat windows_turkish{.encoding = Encoding::windows1254, .crlf = true};

struct LoadedText {
    std::string text;
    FileFormat format;
};

// UTF-8 with a BOM or with non-ASCII text loads as UTF-8; any other bytes are Windows-1254, the code
// page of Turkish Windows. Plain ASCII reads the same in both, so it loads as Windows-1254 too.
LoadedText load_text(std::string_view bytes);

// Error value: byte offset in `text` of the first character the encoding cannot represent.
std::expected<std::string, std::size_t> store_text(std::string_view text, const FileFormat& format);

std::string decode(std::string_view bytes, Encoding encoding);
std::expected<std::string, std::size_t> encode(std::string_view text, Encoding encoding);

constexpr std::string_view display_name(Encoding encoding) {
    return encoding == Encoding::utf8 ? "UTF-8" : "Windows-1254";
}

}  // namespace cedit
