#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cedit {

std::optional<std::string> read_file(const std::string& path);

// Writes a sibling temporary file and renames it over `path`, so a failed save never truncates the original.
bool write_file_atomic(const std::string& path, std::string_view bytes);

}  // namespace cedit
