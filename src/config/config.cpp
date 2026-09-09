#include "config/config.h"

#include "core/log.h"

#include <algorithm>

namespace omni::config {
namespace {

// std::to_wstring su un float da' sempre sei decimali: "deve stare fra
// 0.200000 e 1.000000" e' scritto da un programma, non per una persona. Qui i
// decimali inutili si tolgono.
std::wstring Num(double v) {
    std::wstring out = std::to_wstring(v);
    if (out.find(L'.') == std::wstring::npos) return out;
    while (!out.empty() && out.back() == L'0') out.pop_back();
    if (!out.empty() && out.back() == L'.') out.pop_back();
    return out;
}

// Un valore fuori intervallo non fa fallire il file: si riporta dentro e si
// dice all'utente cosa gli e' stato fatto e perche'. Rifiutare tutto per un
// numero sbagliato vorrebbe dire perdere anche le venti impostazioni giuste
// che ci sono intorno.
float Number(const toml::Value& v, std::string_view key, float fallback,
             float lo, float hi, std::vector<toml::Error>& problems) {
    if (!v.valid()) return fallback;
    if (v.type != toml::Type::Integer && v.type != toml::Type::Float) {
        problems.push_back({v.line, L"« " + FromUtf8(key) + L" » vuole un numero"});
        return fallback;
    }
    const double raw = v.asNumber(fallback);
    const double cl  = std::clamp(raw, static_cast<double>(lo), static_cast<double>(hi));
    if (cl != raw) {
        problems.push_back({v.line, L"« " + FromUtf8(key) + L" » deve stare fra " + Num(lo) +
                                        L" e " + Num(hi) + L": uso " + Num(cl)});
    }
    return static_cast<float>(cl);
}

int Integer(const toml::Value& v, std::string_view key, int fallback,
            int lo, int hi, std::vector<toml::Error>& problems) {
    return static_cast<int>(Number(v, key, static_cast<float>(fallback),
                                   static_cast<float>(lo), static_cast<float>(hi), problems));
}

bool Boolean(const toml::Value& v, std::string_view key, bool fallback,
             std::vector<toml::Error>& problems) {
    if (!v.valid()) return fallback;
    if (v.type != toml::Type::Boolean) {
        problems.push_back({v.line, L"« " + FromUtf8(key) + L" » vuole true oppure false"});
        return fallback;
    }
    return v.boolean;
}

// Una scelta fra parole. Il messaggio elenca quelle ammesse: dire soltanto
// "valore non valido" costringe chi legge ad andare a cercare la
// documentazione, e la documentazione non ce l'ha davanti.
int Choice(const toml::Value& v, std::string_view key, int fallback,
           std::initializer_list<const wchar_t*> names,
           std::vector<toml::Error>& problems) {
    if (!v.valid()) return fallback;
    if (v.type != toml::Type::String) {
        problems.push_back({v.line, L"« " + FromUtf8(key) + L" » vuole una parola fra virgolette"});
        return fallback;
    }
    int index = 0;
    for (const wchar_t* n : names) {
        if (EqualsNoCase(v.str, n)) return index;
        ++index;
    }
    std::wstring elenco;
    for (const wchar_t* n : names) {
        if (!elenco.empty()) elenco += L", ";
        elenco += n;
    }
    problems.push_back({v.line, L"« " + FromUtf8(key) + L" »: « " + v.str +
                                    L" » non è fra i valori possibili (" + elenco + L")"});
    return fallback;
}

// #RRGGBB o #RGB. Un colore scritto male e' un errore che si vede subito, ma
// vale la pena dire *come* si scrive invece di dire solo che e' sbagliato.
bool ParseHexColor(const std::wstring& text, float& r, float& g, float& b) {
    std::wstring h = text;
    if (!h.empty() && h[0] == L'#') h.erase(0, 1);
    if (h.size() == 3) {
        std::wstring wide;
        for (wchar_t c : h) { wide += c; wide += c; }
        h = wide;
    }
    if (h.size() != 6) return false;

    unsigned v = 0;
    for (wchar_t c : h) {
        int d;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    r = static_cast<float>((v >> 16) & 0xFF) / 255.f;
    g = static_cast<float>((v >> 8) & 0xFF) / 255.f;
    b = static_cast<float>(v & 0xFF) / 255.f;
    return true;
}

// Segnala le chiavi che non conosciamo dentro una sezione che conosciamo.
//
// Senza questo, `opacty = 0.5` non fa niente e non dice niente: l'utente
// riavvia, non vede cambiare nulla, e conclude che l'impostazione non
// funziona. E' il modo peggiore di fallire, perche' non lascia nemmeno una
// traccia da cercare.
//
// Le sezioni SCONOSCIUTE invece restano zitte: nel file di esempio ci sono
// sezioni di funzioni non ancora scritte, e un avviso per ognuna a ogni avvio
// insegnerebbe solo a ignorare gli avvisi.
void CheckKeys(const toml::Value& section, std::string_view name,
               std::initializer_list<const char*> known,
               std::vector<toml::Error>& problems) {
    if (section.type != toml::Type::Table) return;

    for (const auto& kv : section.table) {
        bool trovata = false;
        for (const char* k : known)
            if (kv.first == k) { trovata = true; break; }
        if (trovata) continue;

        problems.push_back({kv.second.line,
                            L"« " + FromUtf8(name) + L"." + FromUtf8(kv.first) +
                                L" » non è un'impostazione che conosco: controlla come si scrive"});
    }
}

}  // namespace

Config Load(const std::wstring& path, std::vector<toml::Error>& problems) {
    Config c;

    toml::Document doc = toml::ParseFile(path);
    problems = std::move(doc.errors);
    const toml::Value& root = doc.root;

    // ── [bar] ──
    const toml::Value& bar = root["bar"];
    c.edge = static_cast<Edge>(
        Choice(bar["edge"], "bar.edge", 3, {L"bottom", L"top", L"left", L"right"}, problems));

    // `extent` accetta la parola "auto" oppure una percentuale: due tipi per la
    // stessa impostazione, perche' la domanda che l'utente si pone e' una sola
    // — quanto dev'essere lunga — e non deve diventare due chiavi.
    const toml::Value& extent = bar["extent"];
    if (extent.type == toml::Type::String) {
        if (EqualsNoCase(extent.str, L"auto")) c.extentPct = 0.f;
        else problems.push_back({extent.line,
                                 L"« bar.extent » vuole \"auto\" oppure una percentuale"});
    } else if (extent.valid()) {
        c.extentPct = Number(extent, "bar.extent", 0.f, 10.f, 100.f, problems);
    }

    c.maxExtentPct = Number(bar["max_extent"], "bar.max_extent", c.maxExtentPct, 20.f, 100.f, problems);
    c.thicknessDip = Number(bar["thickness"], "bar.thickness", c.thicknessDip, 24.f, 120.f, problems);
    c.cornerRadius = Number(bar["corner_radius"], "bar.corner_radius", c.cornerRadius, 0.f, 60.f, problems);
    c.opacity      = Number(bar["opacity"], "bar.opacity", c.opacity, 0.2f, 1.f, problems);
    c.theme        = static_cast<ThemeMode>(
        Choice(bar["theme"], "bar.theme", 0, {L"auto", L"dark", L"light"}, problems));

    CheckKeys(bar, "bar", {"edge", "extent", "max_extent", "align", "monitor", "thickness",
                           "corner_radius", "opacity", "theme"}, problems);

    // ── [reveal] ──
    const toml::Value& rev = root["reveal"];
    c.lineDip     = Number(rev["line"], "reveal.line", c.lineDip, 1.f, 12.f, problems);
    c.nubThickDip = Number(rev["nub_thickness"], "reveal.nub_thickness", c.nubThickDip, 2.f, 30.f, problems);
    c.nubLenDip   = Number(rev["nub_length"], "reveal.nub_length", c.nubLenDip, 20.f, 400.f, problems);
    c.triggerPx   = Integer(rev["trigger_px"], "reveal.trigger_px", c.triggerPx, 1, 24, problems);
    c.delayMs     = Integer(rev["delay_ms"], "reveal.delay_ms", c.delayMs, 0, 2000, problems);
    c.travelPx    = Integer(rev["travel_px"], "reveal.travel_px", c.travelPx, 4, 400, problems);
    c.unhoverMs   = Integer(rev["unhover_ms"], "reveal.unhover_ms", c.unhoverMs, 0, 5000, problems);
    c.onHover     = Boolean(rev["on_hover"], "reveal.on_hover", c.onHover, problems);

    CheckKeys(rev, "reveal", {"line", "nub_thickness", "nub_length", "peek", "peek_px",
                              "trigger_px", "delay_ms", "travel_px", "unhover_ms", "on_hover"},
              problems);

    // ── [colors] ──
    const toml::Value& col = root["colors"];
    const toml::Value& accent = col["accent"];
    if (accent.type == toml::Type::String) {
        if (EqualsNoCase(accent.str, L"system")) {
            c.accentFromSystem = true;
        } else if (ParseHexColor(accent.str, c.accentR, c.accentG, c.accentB)) {
            c.accentFromSystem = false;
        } else {
            problems.push_back({accent.line,
                                L"« colors.accent » vuole \"system\" oppure un colore "
                                L"come \"#0078D4\""});
        }
    } else if (accent.valid()) {
        problems.push_back({accent.line, L"« colors.accent » vuole del testo fra virgolette"});
    }
    c.soften = Number(col["soften"], "colors.soften", c.soften, 0.f, 1.f, problems);

    CheckKeys(col, "colors", {"accent", "soften", "avatar_top", "avatar_bottom"}, problems);
    CheckKeys(root["log"], "log", {"level"}, problems);

    // ── [log] ──
    c.logLevel = Choice(root["log"]["level"], "log.level", c.logLevel,
                        {L"trace", L"debug", L"info", L"warn", L"error"}, problems);

    // In ordine di riga. Gli errori di sintassi arrivano dal parser e quelli di
    // valore dalla validazione, quindi nascono in due momenti diversi e
    // finirebbero mescolati: chi apre il file per correggerlo lo legge
    // dall'alto in basso, e la lista deve seguirlo.
    std::stable_sort(problems.begin(), problems.end(),
                     [](const toml::Error& a, const toml::Error& b) { return a.line < b.line; });

    return c;
}

void Report(const std::wstring& path, const std::vector<toml::Error>& problems) {
    if (problems.empty()) return;

    log::Warn(L"Nella configurazione ci sono " + std::to_wstring(problems.size()) +
              L" cose da sistemare — " + path);
    for (const toml::Error& e : problems) {
        if (e.line > 0)
            log::Warn(L"  riga " + std::to_wstring(e.line) + L": " + e.message);
        else
            log::Warn(L"  " + e.message);
    }
    log::Warn(L"  (il resto del file è stato applicato: dove c'era un problema valgono i default)");
}

}  // namespace omni::config
