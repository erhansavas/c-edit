#include "diagnostics.hpp"

#include <array>
#include <charconv>
#include <utility>

namespace cedit {
namespace {

constexpr std::size_t npos = std::string_view::npos;

bool take_number(std::string_view& rest, std::size_t& value, char separator) {
    const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (error != std::errc{} || end == rest.data() || end == rest.data() + rest.size() || *end != separator) return false;
    rest.remove_prefix(static_cast<std::size_t>(end - rest.data()) + 1);
    return true;
}

struct Header {
    std::size_t line;
    std::size_t column;
    std::string_view kind;  // "error", "fatal error", "warning" or "note"
    std::string_view message;
};

// "path:line:col: kind: message" about `path`.
std::optional<Header> header_for(std::string_view line, std::string_view path) {
    if (!line.starts_with(path) || line.size() <= path.size() || line[path.size()] != ':') return std::nullopt;
    line.remove_prefix(path.size() + 1);
    std::size_t number = 0;
    std::size_t column = 0;
    if (!take_number(line, number, ':') || !take_number(line, column, ':') || number == 0) return std::nullopt;
    if (line.starts_with(' ')) line.remove_prefix(1);
    for (const std::string_view kind : {"error", "fatal error", "warning", "note"})
        if (line.starts_with(kind) && line.substr(kind.size()).starts_with(": "))
            return Header{number - 1, column > 0 ? column - 1 : 0, kind, line.substr(kind.size() + 2)};
    return std::nullopt;
}

// Undoes llvm::raw_ostream::write_escaped, which clang uses for fix-it paths and text.
std::string unescape(std::string_view text) {
    const auto octal = [](char c) { return c >= '0' && c <= '7'; };
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const char next = text[++i];
        if (next == 'n') {
            out.push_back('\n');
        } else if (next == 't') {
            out.push_back('\t');
        } else if (octal(next) && i + 2 < text.size() && octal(text[i + 1]) && octal(text[i + 2])) {
            out.push_back(static_cast<char>((next - '0') << 6 | (text[i + 1] - '0') << 3 | (text[i + 2] - '0')));
            i += 2;
        } else {
            out.push_back(next);  // \\ and \"
        }
    }
    return out;
}

// fix-it:"path":{line:column-line:column}:"text", one-line edits of `path` only.
std::optional<Fix> parse_fix(std::string_view line, std::string_view path) {
    constexpr std::string_view prefix = "fix-it:\"";
    if (!line.starts_with(prefix)) return std::nullopt;
    line.remove_prefix(prefix.size());
    const std::size_t range = line.find("\":{");
    if (range == npos || unescape(line.substr(0, range)) != path) return std::nullopt;
    line.remove_prefix(range + 3);
    std::size_t first = 0;
    std::size_t column = 0;
    std::size_t last = 0;
    std::size_t end_column = 0;
    if (!take_number(line, first, ':') || !take_number(line, column, '-') || !take_number(line, last, ':') ||
        !take_number(line, end_column, '}') || first == 0 || first != last || column == 0 || end_column < column ||
        !line.starts_with(":\"") || line.size() < 3 || !line.ends_with('"'))
        return std::nullopt;
    return Fix{first - 1, column - 1, end_column - 1, unescape(line.substr(2, line.size() - 3))};
}

// The text inside the first pair of single quotes at or after `from`.
std::string quoted(std::string_view text, std::size_t from = 0) {
    const std::size_t open = from == npos ? npos : text.find('\'', from);
    const std::size_t close = open == npos ? npos : text.find('\'', open + 1);
    return close == npos ? std::string() : std::string(text.substr(open + 1, close - open - 1));
}

// The header a course program most often forgets for a library function.
std::string_view header_of(std::string_view function) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 25> headers = {{
        {"sqrt", "math.h"},     {"pow", "math.h"},      {"fabs", "math.h"},    {"floor", "math.h"},
        {"ceil", "math.h"},     {"round", "math.h"},    {"sin", "math.h"},     {"cos", "math.h"},
        {"tan", "math.h"},      {"log", "math.h"},      {"log10", "math.h"},   {"exp", "math.h"},
        {"isdigit", "ctype.h"}, {"isalpha", "ctype.h"}, {"isalnum", "ctype.h"}, {"isspace", "ctype.h"},
        {"isupper", "ctype.h"}, {"islower", "ctype.h"}, {"toupper", "ctype.h"}, {"tolower", "ctype.h"},
        {"time", "time.h"},     {"clock", "time.h"},    {"getch", "conio.h"},  {"getche", "conio.h"},
        {"Sleep", "windows.h"},
    }};
    for (const auto& [name, header] : headers)
        if (name == function) return header;
    return {};
}

std::string missing_function(const std::string& name) {
    const std::string_view header = header_of(name);
    if (!header.empty()) return "“" + name + "” için #include <" + std::string(header) + "> satırı eksik.";
    return "“" + name + "” adında bir fonksiyon tanımlı değil: adını kontrol edin ya da gereken #include satırını ekleyin.";
}

}  // namespace

std::vector<Diagnostic> parse_diagnostics(std::string_view output, std::string_view source_path) {
    std::vector<Diagnostic> diagnostics;
    bool collecting = false;  // fix-it lines now belong to the last diagnostic
    std::size_t fixits = 0;
    while (!output.empty()) {
        const std::size_t newline = output.find('\n');
        const std::string_view line = output.substr(0, newline);
        output.remove_prefix(newline == npos ? output.size() : newline + 1);
        if (line.starts_with("fix-it:")) {
            if (!collecting) continue;
            if (++fixits == 1)
                diagnostics.back().fix = parse_fix(line, source_path);
            else
                diagnostics.back().fix.reset();  // several edits are not one sure correction
        } else if (const std::optional<Header> header = header_for(line, source_path)) {
            collecting = header->kind != "note";  // a note's fix-its are only suggestions
            if (!collecting) continue;
            diagnostics.push_back(
                {header->line, header->column, header->kind != "warning", std::string(header->message), std::nullopt});
            fixits = 0;
        } else if (line.find(": error: ") != npos || line.find(": warning: ") != npos || line.find(": note: ") != npos) {
            collecting = false;  // about another file
        }
    }
    return diagnostics;
}

std::string explain(std::string_view message, std::string_view source_line) {
    const auto starts = [&](std::string_view text) { return message.starts_with(text); };
    const auto has = [&](std::string_view text) { return message.find(text) != npos; };
    const std::string name = quoted(message);
    if (starts("expected ';'")) return "Noktalı virgül (;) eksik.";
    if (starts("use of undeclared identifier '")) {
        if (has("did you mean '"))
            return "“" + name + "” tanımlı değil. Bunu mu demek istediniz: “" + quoted(message, message.find("did you mean")) + "”?";
        return "“" + name + "” tanımlı değil: kullanmadan önce tanımlayın (örneğin int " + name + ";) ya da yazımını kontrol edin.";
    }
    if (starts("call to undeclared function '") || starts("call to undeclared library function '"))
        return missing_function(name);
    if (starts("format specifies type '")) {
        const std::string given = quoted(message, message.find("argument has type"));
        if (name.ends_with(" *") && given.find_first_of("*[") == std::string::npos &&
            source_line.find("scanf") != std::string_view::npos)
            return "scanf’te değişkenin önüne & koymayı unutmuşsunuz (örneğin &x).";
        return "Biçim belirteci (%d, %f, %c, %s…) ile verilen değerin türü uyuşmuyor.";
    }
    if (starts("using the result of an assignment as a condition"))
        return "Koşulda = (atama) kullanılmış; karşılaştırmak için == yazın.";
    if (starts("if statement has empty body"))
        return "if satırının sonunda fazladan noktalı virgül (;) var; if’in gövdesi boş kalıyor.";
    if (starts("for loop has empty body") || starts("while loop has empty body"))
        return "Döngü satırının sonunda fazladan noktalı virgül (;) var; döngünün gövdesi boş kalıyor.";
    if (starts("more '%' conversions than data arguments"))
        return "Biçim metninde verilen değerlerden fazla %d, %f… var: her biri için bir değer verin.";
    if (starts("data argument not used by format string"))
        return "Fazladan bir değer verilmiş: biçim metninde onun için bir %d, %f… yok.";
    if (starts("format string is not a string literal"))
        return "İlk değer tırnak içinde bir biçim metni olmalı, örneğin printf(\"%d\", x);";
    if (starts("result of comparison against a string literal"))
        return "Metinler == ile karşılaştırılamaz: strcmp(s, \"evet\") == 0 yazın (#include <string.h>).";
    if (starts("equality comparison result unused")) return "== karşılaştırır, değer vermez; atamak için = yazın.";
    if (starts("array type 'char[")) return "Bir diziye = ile metin atanamaz: strcpy(s, \"metin\") yazın (#include <string.h>).";
    if (starts("array type '")) return "Bir dizi = ile toptan atanamaz; elemanları tek tek (örneğin döngüyle) atayın.";
    if (starts("variable-sized object may not be initialized"))
        return "Boyutu bir değişken olan dizi = {…} ile başlatılamaz: boyutu sabit yazın ya da elemanları döngüyle atayın.";
    if (starts("void function '") && has("should not return a value"))
        return "void bir fonksiyon değer döndürmez: yalnızca return; yazın ya da dönüş türünü değiştirin.";
    if (starts("expected '}'")) return "Bir süslü parantez (}) eksik: her { için bir } olmalı.";
    if (starts("extraneous closing brace")) return "Fazladan bir süslü parantez (}) var.";
    if (starts("expected ')'")) return "Kapanış parantezi ) eksik.";
    if (starts("missing terminating '\"' character")) return "Tırnak işareti kapatılmamış: metnin sonuna \" ekleyin.";
    if (starts("missing terminating ' character")) return "Tek tırnak kapatılmamış: karakterden sonra ' ekleyin.";
    if (has("is uninitialized when used here"))
        return "“" + name + "” değişkenine değer verilmeden kullanılıyor; önce bir değer atayın.";
    if (has("is used uninitialized whenever"))
        return "“" + name + "” bazı durumlarda değer verilmeden kullanılıyor; tanımlarken bir değer atayın.";
    if (starts("unused variable '")) return "“" + name + "” tanımlanmış ama hiç kullanılmamış.";
    if (starts("non-void function does not return a value"))
        return "Fonksiyon her durumda bir değer döndürmüyor; sonuna bir return satırı ekleyin.";
    if (starts("return type of 'main' is not 'int'")) return "main’in dönüş türü int olmalı: int main() yazın.";
    if (starts("type specifier missing")) return "Tür yazılmamış: örneğin main() yerine int main() yazın.";
    if (has("initializing 'char' with an expression of type 'char["))
        return "Tek bir karakter için tek tırnak kullanın: 'a'. Çift tırnak (\"a\") bir metin oluşturur.";
    if (starts("expected expression"))
        return "Burada bir değer ya da ifade bekleniyordu; eksik ya da fazladan bir işaret olabilir.";
    if (starts("expected identifier or '('"))
        return "Burada beklenmeyen bir işaret var; süslü parantezlerin eşleştiğine bakın.";
    if (starts("division by zero")) return "Sıfıra bölme var.";
    if (starts("array index ") && has("past the end of the array"))
        return "Dizinin sınırı aşılıyor: n elemanlı bir dizinin indisleri 0’dan n-1’e kadardır.";
    if (starts("redefinition of '")) return "“" + name + "” iki kez tanımlanmış.";
    if (starts("too few arguments to function call")) return "Fonksiyona gereğinden az değer verilmiş.";
    if (starts("too many arguments to function call")) return "Fonksiyona gereğinden fazla değer verilmiş.";
    return {};
}

std::string explain_link_error(std::string_view output) {
    const std::size_t referenced = output.find("\", referenced from:");
    const std::size_t open = referenced == npos ? npos : output.rfind("\"_", referenced);
    if (open == npos) return {};
    const std::string name(output.substr(open + 2, referenced - open - 2));
    if (name == "main") return "Programda main() fonksiyonu yok: her C programı bir main fonksiyonuyla başlar.";
    return missing_function(name);
}

}  // namespace cedit
