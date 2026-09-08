#include "log.h"

#include "paths.h"

#include <mutex>

namespace omni::log {
namespace {

constexpr DWORD kRotateBytes = 512 * 1024;

std::mutex g_mutex;
HANDLE     g_file  = INVALID_HANDLE_VALUE;
Level      g_level = Level::Info;

const wchar_t* Tag(Level l) {
    switch (l) {
        case Level::Trace: return L"TRACE";
        case Level::Debug: return L"DEBUG";
        case Level::Info:  return L"INFO ";
        case Level::Warn:  return L"WARN ";
        case Level::Error: return L"ERROR";
    }
    return L"?????";
}

// Il file precedente diventa .1: una sola generazione, perche' due non ci
// hanno mai detto niente in piu' e occupano il doppio.
void RotateIfNeeded(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return;
    if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow < kRotateBytes) return;

    const std::wstring old = path + L".1";
    DeleteFileW(old.c_str());
    MoveFileW(path.c_str(), old.c_str());
}

std::wstring Stamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

void WriteRaw(const std::wstring& line) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    const std::string utf8 = ToUtf8(line);
    DWORD written = 0;
    WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

std::wstring SystemMessage(DWORD code) {
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring out = (n && buf) ? std::wstring(buf, n) : L"(nessun messaggio)";
    if (buf) LocalFree(buf);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

}  // namespace

void Open(Level minimum) {
    std::scoped_lock lock(g_mutex);
    g_level = minimum;
    if (g_file != INVALID_HANDLE_VALUE) return;
    if (paths::Logs().empty()) return;

    const std::wstring path = paths::Logs() + L"omnibar.log";
    RotateIfNeeded(path);

    // FILE_SHARE_READ: si deve poter aprire il log mentre l'app gira, che e'
    // esattamente quando serve leggerlo.
    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return;

    const bool fresh = (GetLastError() != ERROR_ALREADY_EXISTS);
    SetFilePointer(g_file, 0, nullptr, FILE_END);

    // BOM sul file nuovo: il contenuto e' gia' UTF-8 valido, ma senza BOM il
    // Blocco note e PowerShell 5.1 leggono in ANSI e mostrano accenti rotti.
    // Un log che si legge male non serve a niente.
    if (fresh) {
        static constexpr unsigned char kBom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        WriteFile(g_file, kBom, sizeof(kBom), &written, nullptr);
    }
}

void Close() {
    std::scoped_lock lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

void SetLevel(Level minimum) {
    std::scoped_lock lock(g_mutex);
    g_level = minimum;
}

bool Enabled(Level level) { return level >= g_level; }

void Write(Level level, std::wstring_view text) {
    if (!Enabled(level)) return;
    std::scoped_lock lock(g_mutex);
    if (g_file == INVALID_HANDLE_VALUE) return;

    std::wstring line = Stamp();
    line += L" [";
    line += Tag(level);
    line += L"] ";
    line.append(text);
    line += L"\r\n";
    WriteRaw(line);
}

void LastError(std::wstring_view what) {
    const DWORD code = GetLastError();
    std::wstring line(what);
    line += L" — errore ";
    line += std::to_wstring(code);
    line += L": ";
    line += SystemMessage(code);
    Write(Level::Error, line);
}

void Hresult(std::wstring_view what, HRESULT hr) {
    wchar_t hex[16];
    swprintf_s(hex, L"0x%08X", static_cast<unsigned>(hr));
    std::wstring line(what);
    line += L" — HRESULT ";
    line += hex;
    line += L": ";
    line += SystemMessage(static_cast<DWORD>(hr));
    Write(Level::Error, line);
}

}  // namespace omni::log
