#include "settings.h"
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <fstream>
namespace dp {
std::string serializeSettings(const Settings &s) {
    std::ostringstream o;
    o << "DeskPerch 2\n"
      << s.compact << ' ' << s.locked << ' ' << s.visible << ' ' << s.positioned << ' ' << s.allDisplays
      << ' ' << s.x << ' ' << s.y << '\n';
    for (auto *p : {&s.monitor, &s.mouse, &s.keyboard})
        o << std::quoted(utf8(*p)) << '\n';
    for (auto *v : {&s.ports, &s.displays}) {
        o << v->size() << '\n';
        for (auto &p : *v)
            o << std::quoted(utf8(p)) << '\n';
    }
    o << s.background << '\n';
    return o.str();
}
std::optional<Settings> parseSettings(std::string_view text) {
    if (text.size() > 131072)
        return {};
    std::istringstream in{std::string(text)};
    std::string name;
    int version = 0;
    Settings s;
    if (!(in >> name >> version) || name != "DeskPerch" || (version != 1 && version != 2))
        return {};
    if (!(in >> s.compact >> s.locked >> s.visible >> s.positioned >> s.allDisplays >> s.x >> s.y) ||
        std::abs(static_cast<int64_t>(s.x)) > 100000 || std::abs(static_cast<int64_t>(s.y)) > 100000)
        return {};
    for (auto *p : {&s.monitor, &s.mouse, &s.keyboard}) {
        std::string v;
        if (!(in >> std::quoted(v)) || v.size() > 8192)
            return {};
        *p = wide(v);
        if (!v.empty() && p->empty())
            return {};
    }
    for (auto *v : {&s.ports, &s.displays}) {
        size_t n;
        if (!(in >> n) || n > 512)
            return {};
        for (size_t i = 0; i < n; ++i) {
            std::string a;
            if (!(in >> std::quoted(a)) || a.size() > 8192)
                return {};
            auto b = wide(a);
            if (b.empty())
                return {};
            v->insert(b);
        }
    }
    if (version == 2 && (!(in >> s.background) || s.background < 0 || s.background > 2))
        return {};
    if (s.background == 0)
        s.background = 2; // Retired smoked background; keep all other preferences.
    in >> std::ws;
    if (!in.eof())
        return {};
    return s;
}
std::filesystem::path settingsPath() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        return {};
    auto r = std::filesystem::path(p) / L"DeskPerch" / L"settings.dat";
    CoTaskMemFree(p);
    return r;
}
Settings loadSettings() {
    auto p = settingsPath();
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    std::string v(131073, '\0');
    f.read(v.data(), static_cast<std::streamsize>(v.size()));
    v.resize(static_cast<size_t>(f.gcount()));
    return parseSettings(v).value_or(Settings{});
}
bool saveSettings(const Settings &s) {
    auto p = settingsPath();
    if (p.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
        return false;
    auto temp = p;
    temp += L".tmp";
    auto data = serializeSettings(s);
    Handle h(
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(h.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
              written == data.size() && FlushFileBuffers(h.get());
    h.reset();
    if (ok)
        ok = MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok)
        DeleteFileW(temp.c_str());
    return ok;
}
constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool startupEnabled() {
    wchar_t buf[32768]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, L"DeskPerch", RRF_RT_REG_SZ, nullptr, buf, &size) !=
        ERROR_SUCCESS)
        return false;
    return std::wstring(buf) == L"\"" + exePath() + L"\"";
}
bool setStartup(bool enabled) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    auto value = L"\"" + exePath() + L"\"";
    LSTATUS r =
        enabled ? RegSetValueExW(k, L"DeskPerch", 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                 static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)))
                : RegDeleteValueW(k, L"DeskPerch");
    RegCloseKey(k);
    return r == ERROR_SUCCESS || (!enabled && r == ERROR_FILE_NOT_FOUND);
}
} // namespace dp
