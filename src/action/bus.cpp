#include "action/bus.h"

#include "core/log.h"

#include <shellapi.h>

#include <algorithm>
#include <array>

namespace omni::action {
namespace {

struct NamedKey {
    const wchar_t* name;
    WORD           vk;
};

constexpr std::array kNamedKeys = std::to_array<NamedKey>({
    {L"enter", VK_RETURN},   {L"return", VK_RETURN}, {L"tab", VK_TAB},
    {L"esc", VK_ESCAPE},     {L"escape", VK_ESCAPE}, {L"space", VK_SPACE},
    {L"back", VK_BACK},      {L"backspace", VK_BACK},
    {L"del", VK_DELETE},     {L"delete", VK_DELETE}, {L"ins", VK_INSERT},
    {L"home", VK_HOME},      {L"end", VK_END},
    {L"pgup", VK_PRIOR},     {L"pgdn", VK_NEXT},
    {L"up", VK_UP},          {L"down", VK_DOWN},
    {L"left", VK_LEFT},      {L"right", VK_RIGHT},
    {L"plus", VK_OEM_PLUS},  {L"minus", VK_OEM_MINUS},
});

std::wstring Lower(std::wstring_view s) {
    std::wstring out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) { return towlower(c); });
    return out;
}

std::wstring Trim(std::wstring_view s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return std::wstring(s.substr(a, b - a));
}

void Press(std::vector<INPUT>& seq, WORD vk, bool up) {
    INPUT in{};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    seq.push_back(in);
}

}  // namespace

uint32_t Required(const ui::Action& action) {
    switch (action.kind) {
        case ui::ActionKind::None:
        case ui::ActionKind::Internal:
        case ui::ActionKind::Rpc:
            return static_cast<uint32_t>(Capability::None);
        case ui::ActionKind::Keystroke: return static_cast<uint32_t>(Capability::Input);
        case ui::ActionKind::Shell:     return static_cast<uint32_t>(Capability::Exec);
        case ui::ActionKind::Url:       return static_cast<uint32_t>(Capability::Open);
        case ui::ActionKind::Macro: {
            uint32_t all = 0;
            for (const auto& step : action.steps) all |= Required(step);
            return all;
        }
    }
    return static_cast<uint32_t>(Capability::None);
}

KeyCombo ParseCombo(std::wstring_view text) {
    KeyCombo combo;

    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'+') {
            // "Ctrl++" e' il tasto '+': un pezzo vuoto fra due '+' non e' un
            // separatore di troppo, e' il tasto stesso.
            std::wstring piece = Trim(text.substr(start, i - start));
            if (piece.empty() && i < text.size() && i + 1 < text.size() && text[i + 1] == L'+') {
                piece = L"+";
                ++i;
            }
            if (!piece.empty()) parts.push_back(piece);
            start = i + 1;
        }
    }
    if (parts.empty()) return combo;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::wstring mod = Lower(parts[i]);
        if (mod == L"ctrl" || mod == L"control") combo.ctrl = true;
        else if (mod == L"alt")                  combo.alt = true;
        else if (mod == L"shift" || mod == L"maiusc") combo.shift = true;
        else if (mod == L"win" || mod == L"meta") combo.win = true;
        else {
            log::Warn(L"Modificatore sconosciuto in una scorciatoia: " + parts[i]);
            return {};
        }
    }

    const std::wstring key = parts.back();
    const std::wstring low = Lower(key);

    if (const auto it = std::find_if(kNamedKeys.begin(), kNamedKeys.end(),
                                     [&](const NamedKey& n) { return low == n.name; });
        it != kNamedKeys.end()) {
        combo.vk = it->vk;
        return combo;
    }

    if (low.size() >= 2 && low[0] == L'f') {
        const int n = _wtoi(low.c_str() + 1);
        if (n >= 1 && n <= 24) {
            combo.vk = static_cast<WORD>(VK_F1 + n - 1);
            return combo;
        }
    }

    if (key.size() == 1) {
        // VkKeyScanW tiene conto del layout: su una tastiera italiana '=' e i
        // simboli non stanno dove starebbero su una statunitense, e cablarli
        // sarebbe sbagliato per meta' degli utenti.
        const SHORT scan = VkKeyScanW(key[0]);
        if (scan != -1) {
            combo.vk = static_cast<WORD>(scan & 0xFF);
            const int state = (scan >> 8) & 0xFF;
            if (state & 1) combo.shift = true;
            if (state & 2) combo.ctrl  = true;
            if (state & 4) combo.alt   = true;
            return combo;
        }
    }

    log::Warn(L"Tasto sconosciuto in una scorciatoia: " + key);
    return {};
}

bool Bus::Keystroke(const ui::Action& a, const Context& ctx) {
    const KeyCombo combo = ParseCombo(a.keys);
    if (!combo.valid()) return false;

    // La verifica del bersaglio: se il foreground e' cambiato da quando
    // l'utente ha premuto, non si manda niente.
    if (ctx.target && GetForegroundWindow() != ctx.target) {
        log::Warn(L"Scorciatoia annullata: la finestra in primo piano e' cambiata");
        return false;
    }

    std::vector<INPUT> seq;
    seq.reserve(10);
    if (combo.ctrl)  Press(seq, VK_CONTROL, false);
    if (combo.alt)   Press(seq, VK_MENU,    false);
    if (combo.shift) Press(seq, VK_SHIFT,   false);
    if (combo.win)   Press(seq, VK_LWIN,    false);

    Press(seq, combo.vk, false);
    Press(seq, combo.vk, true);

    if (combo.win)   Press(seq, VK_LWIN,    true);
    if (combo.shift) Press(seq, VK_SHIFT,   true);
    if (combo.alt)   Press(seq, VK_MENU,    true);
    if (combo.ctrl)  Press(seq, VK_CONTROL, true);

    const UINT sent = SendInput(static_cast<UINT>(seq.size()), seq.data(), sizeof(INPUT));
    if (sent != seq.size()) {
        // Verso una finestra elevata UIPI blocca l'invio: e' il caso per cui
        // esiste l'helper opzionale (docs/security.md §3).
        log::LastError(L"SendInput incompleta (finestra elevata?)");
        return false;
    }
    return true;
}

bool Bus::Shell(const ui::Action& a) {
    if (a.exe.empty()) return false;

    // Gli argomenti si compongono qui, gia' quotati, e non passano mai per un
    // interprete: niente cmd.exe, niente ShellExecute su una stringa.
    std::wstring cmdline = L"\"" + a.exe + L"\"";
    for (const auto& arg : a.args) {
        cmdline += L" \"";
        for (wchar_t c : arg) {
            if (c == L'"') cmdline += L'\\';
            cmdline += c;
        }
        cmdline += L'"';
    }

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableLine(cmdline.begin(), cmdline.end());
    mutableLine.push_back(L'\0');

    if (!CreateProcessW(a.exe.c_str(), mutableLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        log::LastError(L"CreateProcessW: " + a.exe);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool Bus::Url(const ui::Action& a) {
    if (a.url.empty()) return false;
    const auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", a.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;
}

bool Bus::Invoke(const ui::Action& action, const Context& ctx, uint32_t granted) {
    const uint32_t need = Required(action);
    if ((need & ~granted) != 0) {
        log::Warn(L"Azione rifiutata: permesso non concesso");
        return false;
    }

    switch (action.kind) {
        case ui::ActionKind::None:
            return false;

        case ui::ActionKind::Internal:
            return internal_ ? internal_(action.name) : false;

        case ui::ActionKind::Keystroke:
            return Keystroke(action, ctx);

        case ui::ActionKind::Shell:
            return Shell(action);

        case ui::ActionKind::Url:
            return Url(action);

        case ui::ActionKind::Rpc:
            // Arriva con l'ExtHost, fase 5.
            log::Warn(L"Azione rpc non ancora supportata: " + action.name);
            return false;

        case ui::ActionKind::Macro: {
            for (const auto& step : action.steps)
                if (!Invoke(step, ctx, granted)) return false;
            return true;
        }
    }
    return false;
}

}  // namespace omni::action
