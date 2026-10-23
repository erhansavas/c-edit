#include "compile.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <string_view>

#include "posix/files.hpp"
#include "posix/process.hpp"

namespace cedit {
namespace {

std::string_view file_name(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool is_cpp(std::string_view path) {
    for (const std::string_view extension : {".cpp", ".cc", ".cxx", ".C"})
        if (path.ends_with(extension)) return true;
    return false;
}

std::string line_directive(std::string_view path) {
    std::string directive = "#line 1 \"";
    for (const char c : path) {
        if (c == '"' || c == '\\') directive.push_back('\\');
        directive.push_back(c);
    }
    directive += "\"\n";
    return directive;
}

// "hata yok", "2 hata".
std::string count(std::size_t n, std::string_view noun) {
    return n == 0 ? std::string(noun) + " yok" : std::to_string(n) + " " + std::string(noun);
}

// Line `index` (0-based) of `text`.
std::string_view line_of(std::string_view text, std::size_t index) {
    std::size_t start = 0;
    for (; index > 0; --index) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) return {};
        start = newline + 1;
    }
    return text.substr(start, text.find('\n', start) - start);
}

// The compiler's sure correction, in words: what the line should say. The writer makes the change.
std::string describe(const Fix& fix, std::string_view text) {
    const std::string_view line = line_of(text, fix.line);
    const std::string before(line.substr(std::min(fix.column, line.size()), fix.end_column - fix.column));
    if (before.empty()) return "“" + fix.text + "” yazılmalı.";
    if (fix.text.empty()) return "“" + before + "” silinmeli.";
    return "“" + before + "” yerine “" + fix.text + "” yazılmalı.";
}

// The compiler's output with a Turkish explanation under each message it recognises, without the
// fix-it lines that only the editor reads.
std::string annotated(const std::string_view output, const CompileRequest& request,
                      const std::vector<Diagnostic>& diagnostics) {
    std::string log;
    std::string_view rest = output;
    std::size_t next = 0;  // diagnostics[next] belongs to the next message line about the source
    while (!rest.empty()) {
        const std::size_t newline = rest.find('\n');
        const std::string_view line = rest.substr(0, newline);
        rest.remove_prefix(newline == std::string_view::npos ? rest.size() : newline + 1);
        if (line.starts_with("fix-it:")) continue;
        log.append(line).push_back('\n');
        if (next == diagnostics.size() || parse_diagnostics(line, file_name(request.source_path)).empty()) continue;
        const Diagnostic& diagnostic = diagnostics[next++];
        std::string hint = explain(diagnostic.message, line_of(request.text, diagnostic.line));
        if (diagnostic.fix) hint += (hint.empty() ? "" : " ") + describe(*diagnostic.fix, request.text);
        if (!hint.empty()) log.append(hint_prefix).append(hint).push_back('\n');
    }
    if (const std::string link = explain_link_error(output); !link.empty())
        log.append(hint_prefix).append(link).push_back('\n');
    return log;
}

}  // namespace

CompileResult compile(const CompileRequest& request) {
    CompileResult result;
    ::mkdir(request.work_dir.c_str(), 0700);
    const std::string_view name = file_name(request.source_path);
    const bool cpp = is_cpp(request.source_path);
    const std::string stem(name.substr(0, name.rfind('.')));
    const std::string work_source = request.work_dir + "/" + std::string(name);
    result.binary = request.work_dir + "/" + stem;

    // The compiler always sees UTF-8; #line names the user's file in diagnostics, briefly, by its file name.
    if (!write_file_atomic(work_source, line_directive(name) + request.text)) {
        result.log = work_source + " yazılamadı.\n";
        return result;
    }
    const std::vector<std::string> argv = {
        cpp ? "/usr/bin/clang++" : "/usr/bin/clang",
        "-x", cpp ? "c++" : "c",
        cpp ? "-std=gnu++14" : "-std=gnu11",
        "-O0", "-Wall", "-Wno-deprecated-declarations", "-fno-color-diagnostics", "-fdiagnostics-parseable-fixits",
        // A // comment ending in a backslash, common in escape-sequence lessons; the editor shows the joined line as comment.
        "-Wno-comment",
        // GCC on Windows only warns about these old habits, e.g. `main()` or a missing #include.
        "-Wno-error=implicit-int", "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
        "-Wno-error=incompatible-function-pointer-types", "-Wno-error=return-mismatch",
        "-iquote", request.source_path.substr(0, request.source_path.rfind('/')),  // #include "own.h" beside it
        "-I", request.compat_dir,
        "-include", request.compat_dir + "/cedit_compat.h",
        work_source, "-o", result.binary, "-lm",
    };
    const auto started = std::chrono::steady_clock::now();
    const std::optional<Captured> captured = run_capture(argv);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
    if (!captured) {
        result.log = "Derleyici başlatılamadı: " + argv[0] + "\n";
        return result;
    }
    result.ok = captured->exit_code == 0;
    result.diagnostics = parse_diagnostics(captured->output, name);
    // Fix-its count physical lines, and the #line directive above the user's text is one of them.
    for (Diagnostic& diagnostic : result.diagnostics) {
        if (!diagnostic.fix) continue;
        if (diagnostic.fix->line == 0)
            diagnostic.fix.reset();
        else
            --diagnostic.fix->line;
    }
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const Diagnostic& diagnostic : result.diagnostics) (diagnostic.error ? errors : warnings) += 1;
    if (!result.ok && errors == 0) errors = 1;  // e.g. a linker error with no source location

    result.log = annotated(captured->output, request, result.diagnostics);
    if (!result.log.empty()) result.log.push_back('\n');
    result.log += std::string(result.ok ? "Derleme başarılı (" : "Derleme başarısız (") + duration_text(elapsed.count()) +
                  "): " + count(errors, "hata") + ", " + count(warnings, "uyarı") + ".\n";
    return result;
}

bool compiler_in(const std::string& developer_dir) {
    for (const char* clang : {"/usr/bin/clang", "/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"})
        if (::access((developer_dir + clang).c_str(), X_OK) == 0) return true;
    return false;
}

bool developer_tools_installed() {
    const std::optional<Captured> selected = run_capture({"/usr/bin/xcode-select", "-p"});
    if (!selected || selected->exit_code != 0) return false;
    std::string directory = selected->output;
    while (!directory.empty() && (directory.back() == '\n' || directory.back() == ' ')) directory.pop_back();
    return compiler_in(directory);
}

void request_developer_tools() { (void)run_capture({"/usr/bin/xcode-select", "--install"}); }

}  // namespace cedit
