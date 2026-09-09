#include "config/widgets.h"

namespace omni::config {
namespace {

void Problema(std::vector<toml::Error>& problems, int line, std::wstring msg) {
    problems.push_back({line, std::move(msg)});
}

// Elenca i valori ammessi invece di dire solo "non valido": chi sta scrivendo
// il file non ha la documentazione davanti.
std::wstring Elenco(std::initializer_list<const wchar_t*> voci) {
    std::wstring out;
    for (const wchar_t* v : voci) {
        if (!out.empty()) out += L", ";
        out += v;
    }
    return out;
}

}  // namespace

ui::Action BuildAction(const toml::Value& v, std::vector<toml::Error>& problems) {
    ui::Action a;
    if (!v.valid()) return a;

    if (v.type != toml::Type::Table) {
        Problema(problems, v.line,
                 L"« action » vuole una tabella, per esempio "
                 L"{ kind = \"keystroke\", keys = \"Ctrl+S\" }");
        return a;
    }

    const std::wstring kind = v["kind"].asString();
    if (kind.empty()) {
        Problema(problems, v.line, L"all'azione manca « kind »: " +
                                       Elenco({L"keystroke", L"shell", L"url", L"internal", L"macro"}));
        return a;
    }

    if (EqualsNoCase(kind, L"keystroke")) {
        a.kind = ui::ActionKind::Keystroke;
        a.keys = v["keys"].asString();
        if (a.keys.empty()) {
            Problema(problems, v.line, L"a un'azione « keystroke » serve « keys », "
                                       L"per esempio keys = \"Ctrl+Shift+S\"");
            a.kind = ui::ActionKind::None;
        }
        return a;
    }

    if (EqualsNoCase(kind, L"shell")) {
        a.kind = ui::ActionKind::Shell;
        a.exe  = v["exe"].asString();
        if (a.exe.empty()) {
            Problema(problems, v.line, L"a un'azione « shell » serve « exe »");
            a.kind = ui::ActionKind::None;
            return a;
        }

        // Gli argomenti sono un elenco, mai una riga sola da spezzare noi.
        // E' la regola di sicurezza §2.2, e qui si vede da fuori: non esiste un
        // campo in cui scrivere una riga di comando.
        const toml::Value& args = v["args"];
        if (args.valid() && args.type != toml::Type::Array) {
            Problema(problems, args.line,
                     L"« args » vuole un elenco fra parentesi quadre, un argomento per elemento: "
                     L"gli argomenti non si scrivono tutti in una stringa");
        } else if (args.valid()) {
            for (const toml::Value& item : args.array) {
                if (item.type != toml::Type::String) {
                    Problema(problems, item.line, L"ogni argomento va fra virgolette");
                    continue;
                }
                a.args.push_back(item.str);
            }
        }
        return a;
    }

    if (EqualsNoCase(kind, L"url")) {
        a.kind = ui::ActionKind::Url;
        a.url  = v["url"].asString();
        if (a.url.empty()) {
            Problema(problems, v.line, L"a un'azione « url » serve « url »");
            a.kind = ui::ActionKind::None;
        }
        return a;
    }

    if (EqualsNoCase(kind, L"internal")) {
        a.kind = ui::ActionKind::Internal;
        a.name = v["name"].asString();
        if (a.name.empty()) {
            Problema(problems, v.line, L"a un'azione « internal » serve « name »");
            a.kind = ui::ActionKind::None;
        }
        return a;
    }

    if (EqualsNoCase(kind, L"macro")) {
        a.kind = ui::ActionKind::Macro;
        const toml::Value& steps = v["steps"];
        if (steps.type != toml::Type::Array) {
            Problema(problems, v.line, L"a una « macro » serve « steps », un elenco di azioni");
            a.kind = ui::ActionKind::None;
            return a;
        }
        for (const toml::Value& s : steps.array) {
            ui::Action passo = BuildAction(s, problems);
            if (passo.valid()) a.steps.push_back(std::move(passo));
        }
        if (a.steps.empty()) a.kind = ui::ActionKind::None;
        return a;
    }

    Problema(problems, v.line, L"« kind = " + kind + L" » non esiste: " +
                                   Elenco({L"keystroke", L"shell", L"url", L"internal", L"macro"}));
    return a;
}

std::vector<ui::Widget> BuildWidgets(const toml::Value& list,
                                     std::vector<toml::Error>& problems) {
    std::vector<ui::Widget> out;
    if (list.type != toml::Type::Array) return out;

    for (const toml::Value& v : list.array) {
        if (v.type != toml::Type::Table) {
            Problema(problems, v.line, L"ogni widget va scritto come una sezione [[widget]]");
            continue;
        }

        const std::wstring tipo = v["type"].asString();
        if (tipo.empty()) {
            Problema(problems, v.line,
                     L"al widget manca « type »: " +
                         Elenco({L"button", L"toggle", L"label", L"separator", L"spacer", L"avatar"}));
            continue;
        }

        ui::Widget w;
        w.id      = ToUtf8(v["id"].asString());
        w.icon    = v["icon"].asString();
        w.label   = v["label"].asString();
        w.tooltip = v["tooltip"].asString();
        w.a11y    = v["a11y"].asString();
        w.enabled = v["enabled"].asBool(true);
        w.on      = v["on"].asBool(false);
        w.badge   = static_cast<int>(v["badge"].asInt(0));

        if (EqualsNoCase(tipo, L"button"))         w.type = ui::WidgetType::Button;
        else if (EqualsNoCase(tipo, L"toggle"))    w.type = ui::WidgetType::Toggle;
        else if (EqualsNoCase(tipo, L"label"))     w.type = ui::WidgetType::Label;
        else if (EqualsNoCase(tipo, L"separator")) w.type = ui::WidgetType::Separator;
        else if (EqualsNoCase(tipo, L"spacer"))    w.type = ui::WidgetType::Spacer;
        else if (EqualsNoCase(tipo, L"avatar"))    w.type = ui::WidgetType::Avatar;
        else {
            Problema(problems, v.line,
                     L"« type = " + tipo + L" » non esiste: " +
                         Elenco({L"button", L"toggle", L"label", L"separator", L"spacer", L"avatar"}));
            continue;
        }

        if (w.type == ui::WidgetType::Spacer)
            w.size = static_cast<float>(v["size"].asNumber(0.0));

        // L'agente ha un nome che non cambia: non ha senso chiederlo a chi
        // scrive il file, e senza sarebbe l'unico widget a fallire la regola
        // sul nome accessibile pur avendo un significato ovvio.
        if (w.type == ui::WidgetType::Avatar && w.tooltip.empty() && w.a11y.empty())
            w.tooltip = L"Assistente";

        const toml::Value& azione = v["action"];
        if (azione.valid()) w.action = BuildAction(azione, problems);

        // Un widget che si puo' premere deve avere un nome leggibile. Non e'
        // pignoleria: senza, gli screen reader annunciano "pulsante" e basta, e
        // l'utente non sa cosa sta per premere. Il tooltip vale come nome —
        // quindi basta scriverne uno.
        if (w.interactive() && w.accessibleName().empty()) {
            Problema(problems, v.line,
                     L"a questo widget serve « tooltip » (o « label », o « a11y »): "
                     L"senza, chi usa uno screen reader sente solo « pulsante »");
        }

        // Un bottone senza icona e senza testo e' un rettangolo vuoto: si vede
        // che qualcosa non va, ma non si capisce cosa. Meglio dirlo.
        if ((w.type == ui::WidgetType::Button || w.type == ui::WidgetType::Toggle) &&
            w.icon.empty() && w.label.empty()) {
            Problema(problems, v.line, L"a questo bottone serve « icon » oppure « label »");
        }

        // Un id serve a distinguerlo: senza, hover e pressione non lo trovano.
        // Se manca se ne inventa uno, cosi' un file scritto in fretta funziona
        // lo stesso — ma i doppioni vanno segnalati, perche' due widget con lo
        // stesso id si illuminano insieme.
        if (w.id.empty()) w.id = "w" + std::to_string(out.size());
        for (const ui::Widget& altro : out) {
            if (!altro.id.empty() && altro.id == w.id) {
                Problema(problems, v.line, L"l'id « " + FromUtf8(w.id) +
                                               L" » è già usato da un altro widget");
                break;
            }
        }

        out.push_back(std::move(w));
    }

    return out;
}

}  // namespace omni::config
