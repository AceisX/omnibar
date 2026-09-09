#include "config/toml.h"

#include <algorithm>
#include <cstdlib>

namespace omni::toml {
namespace {

const Value kNone;

bool IsBare(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// Il parser vero. Tiene la posizione nel testo e il numero di riga, e riporta
// gli errori invece di lanciare: un file di configurazione sbagliato non e'
// un'eccezione, e' la normalita' del primo tentativo.
class Parser {
public:
    Parser(std::string_view text, Document& doc) : s_(text), doc_(doc) {}

    void Run() {
        current_ = &doc_.root;
        doc_.root.type = Type::Table;

        while (!Eof()) {
            SkipBlank();
            if (Eof()) break;

            if (Peek() == '[') {
                ParseHeader();
            } else {
                ParseKeyValue(*current_);
            }
            FinishLine();
        }
    }

private:
    // ── Lettura di base ────────────────────────────────────────────────────
    bool Eof() const { return i_ >= s_.size(); }
    char Peek(size_t off = 0) const { return i_ + off < s_.size() ? s_[i_ + off] : '\0'; }
    char Take() { return s_[i_++]; }

    void Error(std::wstring msg) {
        // Un solo errore per riga: dopo il primo, il resto della riga e'
        // probabilmente rumore causato da quello, e riportarlo confonderebbe.
        if (!doc_.errors.empty() && doc_.errors.back().line == line_) return;
        doc_.errors.push_back({line_, std::move(msg)});
    }

    void SkipSpaces() {
        while (!Eof() && (Peek() == ' ' || Peek() == '\t')) ++i_;
    }

    void SkipToEol() {
        while (!Eof() && Peek() != '\n') ++i_;
    }

    // Salta spazi, commenti e righe vuote, contando le righe.
    void SkipBlank() {
        for (;;) {
            SkipSpaces();
            if (Eof()) return;
            if (Peek() == '#') { SkipToEol(); continue; }
            if (Peek() == '\r') { ++i_; continue; }
            if (Peek() == '\n') { ++i_; ++line_; continue; }
            return;
        }
    }

    // Chiude la riga corrente, segnalando cio' che avanza.
    void FinishLine() {
        SkipSpaces();
        if (!Eof() && Peek() == '#') SkipToEol();
        SkipSpaces();
        if (!Eof() && Peek() != '\n' && Peek() != '\r') {
            Error(L"c'è dell'altro dopo la fine della riga: ogni impostazione va su una riga sua");
            SkipToEol();
        }
        if (!Eof() && Peek() == '\r') ++i_;
        if (!Eof() && Peek() == '\n') { ++i_; ++line_; }
    }

    // ── Chiavi ─────────────────────────────────────────────────────────────
    std::string ParseKeyPiece() {
        if (Peek() == '"' || Peek() == '\'') {
            const std::wstring w = ParseString();
            return ToUtf8(w);
        }
        std::string out;
        while (!Eof() && IsBare(Peek())) out += Take();
        if (out.empty()) Error(L"qui ci vuole il nome di un'impostazione");
        return out;
    }

    // `a.b.c` -> la tabella `a.b`, e in `last` il nome `c`.
    Value* ResolvePath(Value& from, std::string& last, bool createArrays) {
        Value* node = &from;
        for (;;) {
            SkipSpaces();
            std::string piece = ParseKeyPiece();
            SkipSpaces();
            if (Peek() != '.') { last = std::move(piece); return node; }
            ++i_;
            node = &Descend(*node, piece, createArrays);
        }
    }

    Value& Descend(Value& parent, const std::string& key, bool) {
        if (parent.type != Type::Table) {
            parent.type = Type::Table;
            parent.table.clear();
        }
        for (auto& kv : parent.table)
            if (kv.first == key) {
                // Un array di tabelle si estende sempre nell'ultimo elemento.
                if (kv.second.type == Type::Array && !kv.second.array.empty())
                    return kv.second.array.back();
                return kv.second;
            }

        Value fresh;
        fresh.type = Type::Table;
        fresh.line = line_;
        parent.table.emplace_back(key, std::move(fresh));
        return parent.table.back().second;
    }

    // ── Intestazioni di tabella ────────────────────────────────────────────
    void ParseHeader() {
        ++i_;                        // '['
        const bool isArray = (Peek() == '[');
        if (isArray) ++i_;

        Value* node = &doc_.root;
        std::string last;
        node = ResolvePath(*node, last, true);

        SkipSpaces();
        if (Peek() != ']') {
            Error(L"manca la parentesi quadra di chiusura del nome della sezione");
            SkipToEol();
            return;
        }
        ++i_;
        if (isArray) {
            if (Peek() != ']') {
                Error(L"una sezione aperta con [[ va chiusa con ]]");
                SkipToEol();
                return;
            }
            ++i_;
        }

        if (isArray) {
            Value* slot = nullptr;
            for (auto& kv : node->table)
                if (kv.first == last) { slot = &kv.second; break; }
            if (!slot) {
                Value arr;
                arr.type = Type::Array;
                arr.line = line_;
                node->table.emplace_back(last, std::move(arr));
                slot = &node->table.back().second;
            }
            if (slot->type != Type::Array) {
                Error(L"« " + FromUtf8(last) + L" » è già usato come sezione singola");
                return;
            }
            Value item;
            item.type = Type::Table;
            item.line = line_;
            slot->array.push_back(std::move(item));
            current_ = &slot->array.back();
        } else {
            current_ = &Descend(*node, last, false);
            current_->line = line_;
        }
    }

    // ── Coppie chiave/valore ───────────────────────────────────────────────
    void ParseKeyValue(Value& into) {
        std::string last;
        Value* node = ResolvePath(into, last, false);
        if (last.empty()) { SkipToEol(); return; }

        SkipSpaces();
        if (Peek() != '=') {
            Error(L"dopo « " + FromUtf8(last) + L" » ci vuole un = e poi il valore");
            SkipToEol();
            return;
        }
        ++i_;
        SkipSpaces();

        Value v = ParseValue();
        if (!v.valid()) { SkipToEol(); return; }

        if (node->type != Type::Table) { node->type = Type::Table; node->table.clear(); }
        for (auto& kv : node->table)
            if (kv.first == last) {
                Error(L"« " + FromUtf8(last) + L" » è impostata due volte");
                kv.second = std::move(v);
                return;
            }
        node->table.emplace_back(last, std::move(v));
    }

    // ── Valori ─────────────────────────────────────────────────────────────
    Value ParseValue() {
        Value v;
        v.line = line_;

        if (Eof()) { Error(L"manca il valore"); return v; }

        const char c = Peek();
        if (c == '"' || c == '\'') {
            v.type = Type::String;
            v.str  = ParseString();
            return v;
        }
        if (c == '[') return ParseArray();
        if (c == '{') return ParseInlineTable();

        if (s_.compare(i_, 4, "true") == 0)  { i_ += 4; v.type = Type::Boolean; v.boolean = true;  return v; }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; v.type = Type::Boolean; v.boolean = false; return v; }

        return ParseNumber();
    }

    std::wstring ParseString() {
        const char quote = Take();
        std::string out;

        // Le apici singole sono letterali: dentro non si interpreta nulla. E'
        // cio' che rende scrivibile un percorso di Windows senza raddoppiare
        // ogni backslash.
        const bool literal = (quote == '\'');

        while (!Eof()) {
            const char c = Peek();
            if (c == '\n') break;
            ++i_;
            if (c == quote) return FromUtf8(out);

            if (!literal && c == '\\' && !Eof()) {
                const char e = Take();
                switch (e) {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        // \uXXXX: serve alle icone e ai caratteri speciali.
                        unsigned code = 0;
                        int digits = 0;
                        while (digits < 4 && !Eof() && isxdigit(static_cast<unsigned char>(Peek()))) {
                            const char h = Take();
                            code = code * 16 + static_cast<unsigned>(
                                       h <= '9' ? h - '0' : (tolower(h) - 'a' + 10));
                            ++digits;
                        }
                        if (digits == 4) {
                            const wchar_t wc = static_cast<wchar_t>(code);
                            out += ToUtf8(std::wstring(1, wc));
                        } else {
                            Error(L"dopo \\u ci vogliono quattro cifre esadecimali");
                        }
                        break;
                    }
                    default:
                        Error(L"sequenza di escape sconosciuta dopo la barra rovesciata");
                        out += e;
                        break;
                }
                continue;
            }
            out += c;
        }

        Error(L"la stringa non è stata chiusa prima della fine della riga");
        return FromUtf8(out);
    }

    Value ParseNumber() {
        Value v;
        v.line = line_;

        const size_t start = i_;
        if (!Eof() && (Peek() == '+' || Peek() == '-')) ++i_;
        bool digits = false, dot = false, expo = false;
        while (!Eof()) {
            const char c = Peek();
            if (c >= '0' && c <= '9') { digits = true; ++i_; continue; }
            if (c == '_') { ++i_; continue; }           // separatore di migliaia
            if (c == '.' && !dot && !expo) { dot = true; ++i_; continue; }
            if ((c == 'e' || c == 'E') && digits && !expo) {
                expo = true; ++i_;
                if (!Eof() && (Peek() == '+' || Peek() == '-')) ++i_;
                continue;
            }
            break;
        }

        if (!digits) {
            Error(L"valore non riconosciuto: ci vuole un numero, true/false, "
                  L"del testo fra virgolette, oppure un elenco fra parentesi quadre");
            SkipToEol();
            return v;
        }

        std::string raw;
        for (size_t k = start; k < i_; ++k)
            if (s_[k] != '_') raw += s_[k];

        if (dot || expo) {
            v.type   = Type::Float;
            v.number = std::strtod(raw.c_str(), nullptr);
        } else {
            v.type    = Type::Integer;
            v.integer = std::strtoll(raw.c_str(), nullptr, 10);
        }
        return v;
    }

    Value ParseArray() {
        Value v;
        v.type = Type::Array;
        v.line = line_;
        ++i_;   // '['

        for (;;) {
            SkipBlank();
            if (Eof()) { Error(L"l'elenco non è stato chiuso con ]"); return v; }
            if (Peek() == ']') { ++i_; return v; }

            Value item = ParseValue();
            if (!item.valid()) return v;
            v.array.push_back(std::move(item));

            SkipBlank();
            if (!Eof() && Peek() == ',') { ++i_; continue; }
            SkipBlank();
            if (!Eof() && Peek() == ']') { ++i_; return v; }

            Error(L"fra un elemento e l'altro dell'elenco ci vuole una virgola");
            SkipToEol();
            return v;
        }
    }

    Value ParseInlineTable() {
        Value v;
        v.type = Type::Table;
        v.line = line_;
        ++i_;   // '{'

        for (;;) {
            SkipSpaces();
            if (Eof()) { Error(L"la tabella in linea non è stata chiusa con }"); return v; }
            if (Peek() == '}') { ++i_; return v; }

            ParseKeyValue(v);

            SkipSpaces();
            if (!Eof() && Peek() == ',') { ++i_; continue; }
            SkipSpaces();
            if (!Eof() && Peek() == '}') { ++i_; return v; }

            Error(L"fra un campo e l'altro ci vuole una virgola");
            SkipToEol();
            return v;
        }
    }

    std::string_view s_;
    Document&        doc_;
    size_t           i_    = 0;
    int              line_ = 1;
    Value*           current_ = nullptr;
};

}  // namespace

// ── Accesso ai valori ────────────────────────────────────────────────────────

const Value& Value::operator[](std::string_view key) const {
    if (type != Type::Table) return kNone;
    for (const auto& kv : table)
        if (kv.first == key) return kv.second;
    return kNone;
}

const Value& Value::find(std::string_view path) const {
    const Value* node = this;
    size_t start = 0;
    for (;;) {
        const size_t dot = path.find('.', start);
        const std::string_view piece =
            path.substr(start, dot == std::string_view::npos ? path.size() - start : dot - start);
        node = &(*node)[piece];
        if (!node->valid() || dot == std::string_view::npos) return *node;
        start = dot + 1;
    }
}

std::wstring Value::asString(std::wstring_view fallback) const {
    return type == Type::String ? str : std::wstring(fallback);
}

double Value::asNumber(double fallback) const {
    if (type == Type::Float)   return number;
    if (type == Type::Integer) return static_cast<double>(integer);
    return fallback;
}

int64_t Value::asInt(int64_t fallback) const {
    if (type == Type::Integer) return integer;
    if (type == Type::Float)   return static_cast<int64_t>(number);
    return fallback;
}

bool Value::asBool(bool fallback) const {
    return type == Type::Boolean ? boolean : fallback;
}

// ── Ingressi ─────────────────────────────────────────────────────────────────

Document Parse(std::string_view utf8) {
    Document doc;

    // Il BOM lo mettono quasi tutti gli editor di Windows: saltarlo in
    // silenzio, invece di far fallire la prima riga con un errore
    // incomprensibile.
    if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB &&
        static_cast<unsigned char>(utf8[2]) == 0xBF)
        utf8.remove_prefix(3);

    Parser(utf8, doc).Run();
    return doc;
}

Document ParseFile(const std::wstring& path) {
    Document doc;
    doc.root.type = Type::Table;

    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return doc;   // niente file: valgono i default

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(f);
        if (size.QuadPart > 4 * 1024 * 1024)
            doc.errors.push_back({0, L"il file è troppo grande per essere una configurazione"});
        return doc;
    }

    std::string text(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = ReadFile(f, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(f);
    if (!ok) {
        doc.errors.push_back({0, L"il file non si è potuto leggere"});
        return doc;
    }
    text.resize(read);
    return Parse(text);
}

}  // namespace omni::toml
