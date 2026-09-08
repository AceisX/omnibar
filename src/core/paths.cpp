#include "paths.h"

#include <shlobj.h>

namespace omni::paths {
namespace {

std::wstring g_exeDir, g_root, g_profiles, g_extensions, g_state, g_logs;
bool         g_portable = false;
bool         g_ready    = false;

std::wstring DirOfModule() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    const size_t slash = buf.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring{} : buf.substr(0, slash + 1);
}

std::wstring RoamingAppData() {
    PWSTR    p  = nullptr;
    HRESULT  hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &p);
    if (FAILED(hr) || !p) { if (p) CoTaskMemFree(p); return {}; }
    std::wstring out(p);
    CoTaskMemFree(p);
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    return out;
}

// CreateDirectory che non si lamenta se la cartella c'e' gia'.
bool Ensure(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

bool Init() {
    if (g_ready) return true;

    g_exeDir = DirOfModule();
    if (g_exeDir.empty()) return false;

    // La presenza del marker decide la modalita', non un'opzione: un'opzione
    // dovrebbe stare in un file, e quel file starebbe gia' in uno dei due posti.
    g_portable = GetFileAttributesW((g_exeDir + L"portable.flag").c_str()) != INVALID_FILE_ATTRIBUTES;

    if (g_portable) {
        g_root = g_exeDir;
    } else {
        const std::wstring appdata = RoamingAppData();
        if (appdata.empty()) return false;
        g_root = appdata + kAppId + L"\\";
        if (!Ensure(g_root)) return false;
    }

    g_profiles   = g_root + L"profiles\\";
    g_extensions = g_root + L"extensions\\";
    g_state      = g_root + L"state\\";
    g_logs       = g_root + L"logs\\";

    const bool ok = Ensure(g_profiles) && Ensure(g_extensions) && Ensure(g_state) && Ensure(g_logs);
    g_ready = true;   // anche se qualche cartella non c'e': l'host parte lo stesso
    return ok;
}

bool Portable() { return g_portable; }

const std::wstring& ExeDir()     { return g_exeDir; }
const std::wstring& Root()       { return g_root; }
const std::wstring& Profiles()   { return g_profiles; }
const std::wstring& Extensions() { return g_extensions; }
const std::wstring& State()      { return g_state; }
const std::wstring& Logs()       { return g_logs; }

std::wstring ConfigFile() { return g_root + L"omnibar.toml"; }

}  // namespace omni::paths
