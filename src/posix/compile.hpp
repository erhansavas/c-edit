#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/diagnostics.hpp"

namespace cedit {

struct CompileRequest {
    std::string source_path;  // the user's file; diagnostics name it by its file name
    std::string text;         // current UTF-8 editor text
    std::string compat_dir;   // resources/compat inside the app bundle
    std::string work_dir;     // per-document scratch directory, created if missing
};

struct CompileResult {
    bool ok = false;
    std::string binary;
    std::string log;  // compiler output with Turkish explanations, followed by a summary
    std::vector<Diagnostic> diagnostics;
};

// Starts the log lines that explain the compiler message above them.
constexpr std::string_view hint_prefix = "  ➜ ";

// Compiles one C (or, by extension, C++) file the way code written on Windows expects: Windows console
// idioms from the compat headers, gnu dialects, GCC's leniency, no optimisation.
CompileResult compile(const CompileRequest& request);

// Whether `developer_dir`, as xcode-select -p prints it, holds a compiler: the Command Line Tools' or Xcode's.
bool compiler_in(const std::string& developer_dir);

// Whether Apple's compiler is installed; without it /usr/bin/clang can only ask for the Command Line Tools.
bool developer_tools_installed();

// Opens Apple's installer for the Command Line Tools.
void request_developer_tools();

}  // namespace cedit
