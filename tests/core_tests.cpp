// Core and POSIX checks: `./build.sh test` builds this with sanitizers and runs it.
#include <fcntl.h>
#include <iconv.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "core/diagnostics.hpp"
#include "core/editor.hpp"
#include "core/encoding.hpp"
#include "core/syntax.hpp"
#include "core/terminal.hpp"
#include "core/utf8.hpp"
#include "posix/compile.hpp"
#include "posix/files.hpp"
#include "posix/process.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* what, int line) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
}
#define CHECK(condition) check((condition), #condition, __LINE__)

std::string utf8_of(const std::u32string& line) {
    std::string out;
    for (const char32_t c : line) cedit::utf8::append(out, c);
    return out;
}

void encoding_matches_system_iconv() {
    const iconv_t converter = iconv_open("UTF-8", "WINDOWS-1254");
    CHECK(converter != reinterpret_cast<iconv_t>(-1));
    int defined = 0;
    for (int value = 0x80; value <= 0xFF; ++value) {
        char in[1] = {static_cast<char>(value)};
        char out[8] = {};
        char* in_pointer = in;
        char* out_pointer = out;
        std::size_t in_left = 1;
        std::size_t out_left = sizeof out;
        const bool ok = iconv(converter, &in_pointer, &in_left, &out_pointer, &out_left) != static_cast<std::size_t>(-1);
        const std::string ours = cedit::decode(std::string_view(in, 1), cedit::Encoding::windows1254);
        if (ok) {
            ++defined;
            CHECK(ours == std::string(out, sizeof out - out_left));
            const auto back = cedit::encode(ours, cedit::Encoding::windows1254);
            CHECK(back && *back == std::string(in, 1));
        } else {
            CHECK(ours == "\xEF\xBF\xBD");
        }
    }
    iconv_close(converter);
    CHECK(defined == 121);  // 0x81 0x8D 0x8E 0x8F 0x90 0x9D 0x9E are undefined
}

void file_formats_round_trip() {
    const std::string windows = "printf(\"Ya\xFE\xFDn\xFDz\xFD\");\r\n\xDDkinci \xF0\xFE\r\n";
    const cedit::LoadedText loaded = cedit::load_text(windows);
    CHECK(loaded.format.encoding == cedit::Encoding::windows1254);
    CHECK(loaded.format.crlf);
    CHECK(loaded.text == "printf(\"Yaşınızı\");\nİkinci ğş\n");
    const auto stored = cedit::store_text(loaded.text, loaded.format);
    CHECK(stored && *stored == windows);

    const cedit::LoadedText utf8 = cedit::load_text("\xEF\xBB\xBFint ş;\n");
    CHECK(utf8.format.encoding == cedit::Encoding::utf8 && utf8.format.byte_order_mark && !utf8.format.crlf);
    CHECK(utf8.text == "int ş;\n");
    CHECK(cedit::store_text(utf8.text, utf8.format).value_or("") == "\xEF\xBB\xBFint ş;\n");

    const cedit::LoadedText ascii = cedit::load_text("int a;\n");  // reads the same in both: Windows-1254
    CHECK(ascii.format.encoding == cedit::Encoding::windows1254 && !ascii.format.crlf);
    CHECK(cedit::store_text(ascii.text, ascii.format).value_or("") == "int a;\n");
    CHECK(cedit::load_text("int ş;\n").format.encoding == cedit::Encoding::utf8);
    CHECK(cedit::store_text("printf(\"ş\");\n", cedit::windows_turkish).value_or("") == "printf(\"\xFE\");\r\n");

    const auto unrepresentable = cedit::encode("ab€c→", cedit::Encoding::windows1254);
    CHECK(!unrepresentable && unrepresentable.error() == 6);  // € is in 1254, → is not
}

void editing() {
    cedit::Editor editor("int a;\n    b;");
    CHECK(editor.line_count() == 2 && editor.line(1) == "    b;");
    editor.set_caret(editor.text().size(), false);
    editor.insert_newline();
    CHECK(editor.text() == "int a;\n    b;\n    ");
    for (const char* key : {"ş", "ı", "x"}) editor.insert(key);
    CHECK(editor.text().ends_with("    şıx") && editor.modified());
    CHECK(editor.undo() && editor.text() == "int a;\n    b;\n    ");  // typing undoes as one step
    CHECK(editor.undo() && editor.text() == "int a;\n    b;" && !editor.modified());
    CHECK(editor.redo() && editor.redo() && editor.text().ends_with("şıx"));
    editor.delete_backward();
    CHECK(editor.text().ends_with("şı"));
    editor.move(cedit::Move::word_left, true);
    CHECK(editor.selected_text() == "şı");
    editor.mark_saved();
    editor.insert("y");
    CHECK(editor.text().ends_with("    y") && editor.modified());

    cedit::Editor columns("\tab\nxyşz");
    CHECK(columns.column_of(1) == 4 && columns.offset_at(0, 5) == 2);
    columns.set_caret(columns.offset_at(0, 6), false);  // after "b"
    columns.move(cedit::Move::down, false);
    CHECK(columns.caret() == columns.text().size());  // column 6 clamps to the end of "xyşz"
    columns.move(cedit::Move::up, false);
    CHECK(columns.caret() == 3);  // preferred column restored
    columns.select_word(columns.line_start(1) + 2);
    CHECK(columns.selected_text() == "xyşz");

    cedit::Editor indent("    x");
    indent.set_caret(2, false);
    indent.insert_newline();
    CHECK(indent.text() == "  \n    x");  // only the indentation in front of the caret is repeated

    cedit::Editor block("int main() {");
    block.set_caret(block.text().size(), false);
    block.insert_newline();
    CHECK(block.text() == "int main() {\n    ");  // one level deeper after '{'
    block.insert("if (a) {");
    block.insert_newline();
    block.insert("b;");
    block.insert_newline();
    CHECK(block.text() == "int main() {\n    if (a) {\n        b;\n        ");
    block.insert("}");
    CHECK(block.text() == "int main() {\n    if (a) {\n        b;\n    }");  // lines up with its '{'
    CHECK(block.undo() && block.text() == "int main() {\n    if (a) {\n        b;\n        ");
    block.redo();
    block.insert_newline();
    block.insert("}");
    CHECK(block.text().ends_with("    }\n}"));
    block.insert("}");
    CHECK(block.text().ends_with("    }\n}}"));  // after other text a brace is just typed
    cedit::Editor odd("    if (a) {\n     b;\n     ");  // the class's own five-space indentation
    odd.set_caret(odd.text().size(), false);
    odd.insert("}");
    CHECK(odd.text() == "    if (a) {\n     b;\n    }");
    cedit::Editor lone("     ");
    lone.set_caret(5, false);
    lone.insert("}");
    CHECK(lone.text() == "     }");  // no opening brace: typed as is

    cedit::Editor braceless("    if (sayi % 2 == 0)");
    braceless.set_caret(braceless.text().size(), false);
    braceless.insert_newline();
    braceless.insert("a();");
    braceless.insert_newline();
    braceless.insert("else");
    braceless.insert_newline();
    braceless.insert("b();");
    braceless.insert_newline();
    braceless.insert("return 0;");
    CHECK(braceless.text() == "    if (sayi % 2 == 0)\n        a();\n    else\n        b();\n    return 0;");
    cedit::Editor allman("if (f(a))");
    allman.set_caret(allman.text().size(), false);
    allman.insert_newline();
    allman.insert("{");
    allman.insert_newline();
    allman.insert("x;");
    allman.insert_newline();
    allman.insert("}");
    CHECK(allman.text() == "if (f(a))\n{\n    x;\n}");  // the brace lines up with its "if"
    CHECK(allman.undo() && allman.undo() && allman.text() == "if (f(a))\n{\n    x;");
    cedit::Editor closed("    if (a) {\n        b;\n    } else");
    closed.set_caret(closed.text().size(), false);
    closed.insert_newline();
    closed.insert("c;");
    closed.insert_newline();
    CHECK(closed.text() == "    if (a) {\n        b;\n    } else\n        c;\n    ");
    cedit::Editor narrow("    if (a)\n     b;");  // a one-space statement indentation
    narrow.set_caret(narrow.text().size(), false);
    narrow.insert_newline();
    CHECK(narrow.text() == "    if (a)\n     b;\n    ");
    cedit::Editor plain("    if (a) b();\n    for (i = 0; i < 3; i++)");
    plain.set_caret(plain.line_end(0), false);
    plain.insert_newline();
    CHECK(plain.line(1) == "    ");  // a statement on the same line opens nothing
    plain.set_caret(plain.text().size(), false);
    plain.insert_newline();
    CHECK(plain.text().ends_with("i++)\n        "));

    const cedit::Editor guides("int main() {\n    if (a) {\n        b;\n\n        c;\n    }\n\n    if (d)\n     e;\n\treturn 0;\n}");
    const auto lines = guides.indent_guides();
    using Columns = std::vector<std::size_t>;
    CHECK(lines.size() == 11);
    CHECK(lines[0].empty() && lines[1].empty());  // column 0 is the gutter's edge
    CHECK(lines[2] == Columns{4} && lines[3] == Columns{4} && lines[4] == Columns{4});
    CHECK(lines[5].empty() && lines[6].empty());  // the block is closed above the blank line
    CHECK(lines[8] == Columns{4} && lines[9].empty() && lines[10].empty());  // a tab is 4 columns
    const auto uneven = cedit::Editor("    int i;\n     for (;;) { // loop\n      x;\n     }").indent_guides();
    CHECK(uneven[1].empty() && uneven[2] == Columns{5} && uneven[3].empty());  // only block lines open guides
    cedit::Editor commented("    if (a) // yorum");
    commented.set_caret(commented.text().size(), false);
    commented.insert_newline();
    CHECK(commented.text() == "    if (a) // yorum\n        ");

    cedit::Editor saved;
    saved.insert("a");
    saved.mark_saved();
    saved.insert("b");
    CHECK(saved.undo() && saved.text() == "a" && !saved.modified());  // saving ends the typing step
}

void highlighting() {
    std::vector<cedit::Span> spans;
    cedit::LineState state = cedit::highlight_c("# include <stdio.h>", {}, spans);
    CHECK(spans.size() == 2 && spans[0].token == cedit::Token::preprocessor && spans[1].token == cedit::Token::string);
    state = cedit::highlight_c("int a = 10; /* start", state, spans);
    CHECK(state.in_comment && spans.size() == 3 && spans[0].token == cedit::Token::type &&
          spans[1].token == cedit::Token::number && spans[2].token == cedit::Token::comment);
    state = cedit::highlight_c("end */ return \"x\\\"y\";", state, spans);
    CHECK(!state.in_comment && spans.size() == 5 && spans[0].end == 6 && spans[1].token == cedit::Token::keyword &&
          spans[2].token == cedit::Token::string && spans[3].token == cedit::Token::escape && spans[3].begin == 16 &&
          spans[3].end == 18 && spans[4].token == cedit::Token::string && spans[4].end == 20);
    cedit::highlight_c("uint16_t x;", {}, spans);
    CHECK(spans.size() == 1 && spans[0].token == cedit::Token::type);

    // The escape-sequence lesson: a // comment ending in a backslash takes in the next line, as in C.
    state = cedit::highlight_c("    printf(\"C:\\\\Program Files\\\\\"); // C:\\Program Files\\", {}, spans);
    CHECK(state.in_line_comment && spans.back().token == cedit::Token::comment);
    state = cedit::highlight_c("    x = 1; \\ ", state, spans);
    CHECK(state.in_line_comment && spans.size() == 1 && spans[0].token == cedit::Token::comment && spans[0].end == 13);
    state = cedit::highlight_c("    return 0;", state, spans);
    CHECK(!state.in_line_comment && spans.size() == 1 && spans[0].token == cedit::Token::comment);
    state = cedit::highlight_c("    return 0;", state, spans);
    CHECK(spans.size() == 2 && spans[0].token == cedit::Token::keyword);
    state = cedit::highlight_c("    printf(\"\\\\\");", {}, spans);
    CHECK(!state.in_line_comment);  // a backslash inside a string is not a comment's

    // Each escape is one span: two characters, up to three octal digits, or \x and its hex digits.
    cedit::highlight_c("\"a\\n\\101\\x1Fz\\ş\" '\\0'", {}, spans);
    using T = cedit::Token;
    const std::vector<std::pair<std::size_t, T>> pieces = {{2, T::string},  {4, T::escape},     {8, T::escape},
                                                           {12, T::escape}, {13, T::string},    {16, T::escape},
                                                           {17, T::string}, {19, T::character}, {21, T::escape},
                                                           {22, T::character}};
    CHECK(spans.size() == pieces.size());
    for (std::size_t k = 0; k < std::min(spans.size(), pieces.size()); ++k)
        CHECK(spans[k].end == pieces[k].first && spans[k].token == pieces[k].second);
}

void terminal() {
    cedit::Terminal bell;
    bell.feed("Uyari sesi: \a\a");
    CHECK(bell.bells() == 2 && utf8_of(bell.lines()[0]) == "Uyari sesi: ");  // beeps, prints nothing

    cedit::Terminal screen;
    screen.feed("Birinci say\xC4");  // split code point
    screen.feed("\xB1y\xC4\xB1 girin: 2\r\n\x1B[1mx\x1B[0m\b\bab\rZ");
    CHECK(screen.lines().size() == 2);
    CHECK(utf8_of(screen.lines()[0]) == "Birinci sayıyı girin: 2");
    CHECK(utf8_of(screen.lines()[1]) == "Zb" && screen.cursor_column() == 1);
    screen.feed("\x1B[2J\x1B[Hcleared");
    CHECK(screen.lines().size() == 1 && utf8_of(screen.lines()[0]) == "cleared");

    constexpr std::size_t columns = cedit::Terminal::columns;
    cedit::Terminal wrapped;  // a full line and its newline leave no blank line; a longer one wraps
    wrapped.feed(std::string(columns, 'a') + "\n" + std::string(columns + 5, 'b'));
    CHECK(wrapped.lines().size() == 3 && wrapped.lines()[1].size() == columns && wrapped.lines()[2] == U"bbbbb");

    cedit::Terminal clamped;  // cursor requests stay on the screen and within the scrollback
    clamped.feed("\x1B[1;4000000000Hx\x1B[99999999999C\ty");
    CHECK(clamped.lines().size() == 1 && clamped.lines()[0].size() == columns && clamped.lines()[0].back() == U'y');
    clamped.feed("\x1B[999999999999B");
    CHECK(clamped.lines().size() == cedit::Terminal::max_lines);

    cedit::Terminal endless;  // a program printing forever keeps only the last max_lines lines
    for (int i = 0; i < 12'000; ++i) endless.feed("Merhaba " + std::to_string(i) + "\n");
    CHECK(endless.lines().size() == cedit::Terminal::max_lines && utf8_of(endless.lines().front()) == "Merhaba 2001");
}

void diagnostics() {
    const auto parsed = cedit::parse_diagnostics(
        "/x/a.c:3:5: warning: unused variable 'b' [-Wunused-variable]\n"
        "/x/a.c:9:1: error: expected ';' after expression\n"
        "/x/a.c:9:1: note: something\n/other.h:1:1: error: elsewhere\n",
        "/x/a.c");
    CHECK(parsed.size() == 2 && !parsed[0].error && parsed[0].line == 2 && parsed[0].column == 4);
    CHECK(parsed[1].error && parsed[1].message == "expected ';' after expression");

    const auto fixes = cedit::parse_diagnostics(
        "/x/a.c:4:18: error: use of undeclared identifier 'topalm'; did you mean 'toplam'?\n"
        "    4 |     printf(\"%d\", topalm)\n"
        "fix-it:\"/x/a.c\":{5:18-5:24}:\"toplam\"\n"
        "/x/a.c:3:9: note: 'toplam' declared here\n"
        "/x/a.c:6:11: warning: using the result of an assignment as a condition without parentheses\n"
        "/x/a.c:6:11: note: use '==' to turn this assignment into an equality comparison\n"
        "fix-it:\"/x/a.c\":{7:11-7:12}:\"==\"\n"
        "/x/a.c:8:3: warning: escaped\n"
        "fix-it:\"/x/a.c\":{9:3-9:3}:\" = '\\\\0' \\303\\274\"\n"
        "/x/a.c:10:1: error: two edits\n"
        "fix-it:\"/x/a.c\":{11:1-11:1}:\"(\"\nfix-it:\"/x/a.c\":{11:5-11:5}:\")\"\n"
        "/x/a.c:12:1: error: across lines\n"
        "fix-it:\"/x/a.c\":{13:1-14:2}:\"\"\n",
        "/x/a.c");
    CHECK(fixes.size() == 5);
    CHECK(fixes[0].fix && fixes[0].fix->line == 4 && fixes[0].fix->column == 17 && fixes[0].fix->end_column == 23 &&
          fixes[0].fix->text == "toplam");
    CHECK(!fixes[1].fix);  // a note's suggestion is not a sure fix
    CHECK(fixes[2].fix && fixes[2].fix->text == " = '\\0' ü");
    CHECK(!fixes[3].fix && !fixes[4].fix);  // several edits, or one across lines

    CHECK(cedit::explain("expected ';' after expression", "") == "Noktalı virgül (;) eksik.");
    CHECK(cedit::explain("use of undeclared identifier 'topalm'; did you mean 'toplam'?", "") ==
          "“topalm” tanımlı değil.");
    CHECK(cedit::explain("format specifies type 'int *' but the argument has type 'int'", "    scanf(\"%d\", a);") ==
          "scanf’te değişkenin önüne & koymayı unutmuşsunuz (örneğin &x).");
    CHECK(cedit::explain("format specifies type 'char *' but the argument has type 'int'", "    printf(\"%s\", c);")
              .starts_with("Biçim belirteci"));
    CHECK(cedit::explain("call to undeclared library function 'sqrt' with type 'double (double)'; ISO C99 and later "
                         "do not support implicit function declarations",
                         "") == "“sqrt” için #include <math.h> satırı eksik.");
    CHECK(cedit::explain("variable 'x' is uninitialized when used here", "") ==
          "“x” değişkenine değer verilmeden kullanılıyor; önce bir değer atayın.");
    // Messages as clang on this machine words them for the classic slips.
    CHECK(cedit::explain("result of comparison against a string literal is unspecified (use an explicit string "
                       "comparison function instead)",
                       "")
              .starts_with("Metinler == ile"));
    CHECK(cedit::explain("array type 'char[20]' is not assignable", "").starts_with("Bir diziye = ile metin"));
    CHECK(cedit::explain("array type 'int[5]' is not assignable", "").starts_with("Bir dizi = ile toptan"));
    CHECK(cedit::explain("for loop has empty body", "").starts_with("Döngü satırının"));
    CHECK(cedit::explain("more '%' conversions than data arguments", "").starts_with("Biçim metninde"));
    CHECK(cedit::explain("void function 'g' should not return a value", "").starts_with("void bir fonksiyon"));
    CHECK(cedit::explain("a message no explanation knows", "").empty());
    CHECK(cedit::explain_link_error("Undefined symbols for architecture arm64:\n  \"_main\", referenced from:\n")
              .starts_with("Programda main() fonksiyonu yok"));
    CHECK(cedit::explain_link_error("  \"_getch\", referenced from:\n") == "“getch” için #include <conio.h> satırı eksik.");
}

std::string compat_dir;
std::string scratch_dir;  // build/tmp/core_tests: files there are overwritten on every run

// Saves `bytes` as `name`, loads it as the editor does and compiles it the way Run does; the log is
// printed when the build does not end as expected.
cedit::CompileResult build(const std::string& name, const std::string& bytes, bool should_build = true) {
    const std::string source = scratch_dir + "/" + name;
    CHECK(cedit::write_file_atomic(source, bytes));
    const cedit::LoadedText loaded = cedit::load_text(cedit::read_file(source).value_or(""));
    cedit::CompileResult result = cedit::compile({source, loaded.text, compat_dir, scratch_dir + "/work"});
    if (result.ok != should_build) std::fprintf(stderr, "%s", result.log.c_str());
    return result;
}

// A compiled program on a pty, seen through the console's Terminal; it answers each prompt only
// once it is on screen, as a person at the keyboard would.
struct Console {
    cedit::PtyChild child;
    cedit::Terminal screen;
    std::string shown;

    bool read_until(std::string_view wanted) {
        char buffer[4096];
        while (shown.find(wanted) == std::string::npos) {
            pollfd ready{child.master, POLLIN, 0};
            if (::poll(&ready, 1, 10'000) <= 0) return false;  // a broken program fails the test instead of hanging it
            const ssize_t got = ::read(child.master, buffer, sizeof buffer);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return false;  // EIO once the program has exited
            screen.feed(std::string_view(buffer, static_cast<std::size_t>(got)));
            shown.clear();
            for (const auto& line : screen.lines()) shown += utf8_of(line) + "\n";
        }
        return true;
    }

    void type(std::string_view keys) {
        CHECK(::write(child.master, keys.data(), keys.size()) == static_cast<ssize_t>(keys.size()));
    }

    int finish() {
        read_until("\x01");           // drain to the end
        ::kill(child.pid, SIGKILL);  // no effect on a program that has exited
        int status = 0;
        ::waitpid(child.pid, &status, 0);
        ::close(child.master);
        if (failures) std::fprintf(stderr, "console:\n%s", shown.c_str());
        return cedit::exit_code_of(status);
    }
};

std::optional<Console> start(const cedit::CompileResult& result) {
    if (!result.ok) return std::nullopt;
    const auto child = cedit::spawn_in_pty(result.binary, scratch_dir, cedit::Terminal::columns, 25);
    CHECK(child.has_value());
    if (!child) return std::nullopt;
    ::fcntl(child->master, F_SETFL, 0);  // blocking reads are simpler here
    return Console{*child, {}, {}};
}

// A Turkish program exactly as Windows saves it: Windows-1254 with CRLF.
void compiles_and_runs_windows_program() {
    const cedit::CompileResult result = build(
        "ikisayitoplam.c",
        "# include <stdio.h>\r\n#include <locale.h>\r\n\r\nint main()\r\n{\r\n\r\n  setlocale(LC_ALL, \"Turkish\");\r\n"
        "int a,b;\r\n    printf(\"Birinci say\xFDy\xFD girin: \");\r\n    scanf(\"%d\", &a);\r\n"
        "    printf(\"\xDDkinci say\xFDy\xFD girin: \");\r\n    scanf(\"%d\", &b);\r\n    int toplam=a+b;\r\n"
        "    printf(\"\\nToplam: %d\",toplam);\r\n    system(\"pause\");\r\n    return 0;\r\n}\r\n");
    CHECK(result.ok && result.diagnostics.empty());
    CHECK(result.log.starts_with("Derleme başarılı (") && result.log.ends_with("): hata yok, uyarı yok.\n"));
    std::optional<Console> console = start(result);
    if (!console) return;
    CHECK(console->read_until("Birinci sayıyı girin: "));
    console->type("2\r");
    CHECK(console->read_until("İkinci sayıyı girin: "));
    console->type("99\r");
    CHECK(console->read_until("tuşa basın . . . "));
    console->type("x");
    CHECK(console->finish() == 0);
    CHECK(console->shown.find("Birinci sayıyı girin: 2") != std::string::npos);
    CHECK(console->shown.find("İkinci sayıyı girin: 99") != std::string::npos);
    CHECK(console->shown.find("Toplam: 101Devam etmek için bir tuşa basın . . . ") != std::string::npos);
}

// Habits GCC on Windows accepts: `main()` without int, sqrt without <math.h>, fflush(stdin) dropping the
// rest of a line, "pause" answered with a key that sends several bytes, and an English locale by its Windows name.
void runs_windows_habits() {
    const cedit::CompileResult result = build(
        "aliskanliklar.c",
        "#include <stdio.h>\r\n#include <locale.h>\r\nmain()\r\n{\r\n    setlocale(LC_ALL, \"English\");\r\n    int n; char c;\r\n    printf(\"Say\xFD: \");\r\n    scanf(\"%d\", &n);\r\n"
        "    fflush(stdin);\r\n    printf(\"Harf: \");\r\n    scanf(\"%c\", &c);\r\n    system(\"pause\");\r\n"
        "    printf(\"K\xF6k: %.1f %c\\n\", sqrt(n), c);\r\n    scanf(\"%d\", &n);\r\n    printf(\"Son: %d\\n\", n);\r\n}\r\n");
    CHECK(result.ok && result.diagnostics.size() == 2 && result.log.ends_with("): hata yok, 2 uyarı.\n"));
    for (const cedit::Diagnostic& diagnostic : result.diagnostics) CHECK(!diagnostic.error);
    std::optional<Console> console = start(result);
    if (!console) return;
    CHECK(console->read_until("Sayı: "));
    console->type("16\r");
    CHECK(console->read_until("Harf: "));
    console->type("x\r");
    CHECK(console->read_until("tuşa basın . . . "));
    console->type("\x1B[A");  // the up arrow
    CHECK(console->read_until("Kök: 4.0 x\n"));  // "English" means the English locale, as on Windows (not 4,0)
    console->type("7\r");
    CHECK(console->read_until("Son: 7\n"));
    CHECK(console->finish() == 0);
}

// How builds and programs end, in plain Turkish.
void plain_words() {
    const cedit::CompileResult broken = build("eksik.c", "int main() { return x; }\n", false);
    CHECK(!broken.ok && broken.log.find("\nDerleme başarısız (") != std::string::npos &&
          broken.log.ends_with("): 1 hata, uyarı yok.\n"));
    CHECK(cedit::duration_text(0.234) == "0,23 sn");
    CHECK(cedit::describe_exit(W_EXITCODE(3, 0), 1.25) == "Program 1,25 sn içinde bitti, çıkış kodu 3.");
    CHECK(cedit::describe_exit(W_EXITCODE(0, SIGSEGV), 0.5) ==
          "Program 0,50 sn sonra çöktü: izin verilmeyen bir bellek alanına erişti (segmentation fault).");
    CHECK(cedit::describe_exit(W_EXITCODE(0, SIGINT), 2) == "Program 2,00 sn sonra Control-C ile durduruldu.");
    CHECK(cedit::describe_exit(W_EXITCODE(0, SIGTERM), 1) == "Program 1,00 sn sonra 15 numaralı sinyalle durduruldu.");
}

// Mistakes explained in Turkish, with the compiler's own fixes placed on the user's lines.
void explains_mistakes() {
    const cedit::CompileResult typo = build(
        "hatali.c", "#include <stdio.h>\nint main() {\n    int toplam = 0;\n    printf(\"%d\", topalm)\n    return 0;\n}\n",
        false);
    CHECK(typo.diagnostics.size() == 2 && typo.log.find("fix-it:") == std::string::npos);
    if (typo.diagnostics.size() == 2) {
        const auto& name = typo.diagnostics[0].fix;
        const auto& semicolon = typo.diagnostics[1].fix;
        CHECK(name && name->line == 3 && name->column == 17 && name->end_column == 23 && name->text == "toplam");
        CHECK(semicolon && semicolon->line == 3 && semicolon->column == 24 && semicolon->text == ";");
    }
    CHECK(typo.log.find("  ➜ “topalm” tanımlı değil. “topalm” yerine “toplam” yazılmalı.\n") != std::string::npos);
    CHECK(typo.log.find("  ➜ Noktalı virgül (;) eksik. “;” yazılmalı.\n") != std::string::npos);
    CHECK(build("bos.c", "", false).log.find("  ➜ Programda main() fonksiyonu yok") != std::string::npos);
}

// A program finds its own header beside it, though it is compiled from a scratch copy.
void finds_own_headers() {
    CHECK(cedit::write_file_atomic(scratch_dir + "/kendi.h", "#define SELAM \"Selam\"\n"));
    CHECK(build("baslik.c", "#include <stdio.h>\n#include \"kendi.h\"\nint main() { printf(SELAM); return 0; }\n").ok);
}

// Apple's compiler is found where the Command Line Tools or Xcode put it.
void finds_developer_tools() {
    const auto make_compiler = [](const std::string& path) {
        for (std::size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1))
            ::mkdir(path.substr(0, slash).c_str(), 0700);
        CHECK(cedit::write_file_atomic(path, "#!/bin/sh\n") && ::chmod(path.c_str(), 0700) == 0);
    };
    make_compiler(scratch_dir + "/tools/CommandLineTools/usr/bin/clang");
    make_compiler(scratch_dir + "/tools/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang");
    ::mkdir((scratch_dir + "/tools/Empty").c_str(), 0700);
    CHECK(cedit::compiler_in(scratch_dir + "/tools/CommandLineTools"));
    CHECK(cedit::compiler_in(scratch_dir + "/tools/Developer"));
    CHECK(!cedit::compiler_in(scratch_dir + "/tools/Empty"));
    CHECK(cedit::developer_tools_installed());  // these tests compile with it
}

// In C++ the replacements answer to their std:: names as well.
void compiles_cpp_program() {
    const cedit::CompileResult result = build(
        "merhaba.cpp",
        "#include <bits/stdc++.h>\nusing namespace std;\nint main() {\n    std::setlocale(LC_ALL, \"Turkish\");\n"
        "    cout << \"Merhaba\" << endl;\n    std::fflush(stdin);\n    std::system(\"pause\");\n}\n");
    CHECK(result.ok && result.diagnostics.empty());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: core_tests <compat-dir> <scratch-dir>\n");
        return 2;
    }
    compat_dir = argv[1];
    scratch_dir = std::string(argv[2]) + "/core_tests";
    ::mkdir(scratch_dir.c_str(), 0700);
    encoding_matches_system_iconv();
    file_formats_round_trip();
    editing();
    highlighting();
    terminal();
    diagnostics();
    compiles_and_runs_windows_program();
    runs_windows_habits();
    compiles_cpp_program();
    plain_words();
    explains_mistakes();
    finds_own_headers();
    finds_developer_tools();
    std::printf(failures ? "core_tests: %d FAILED\n" : "core_tests: all passed\n", failures);
    return failures ? 1 : 0;
}
