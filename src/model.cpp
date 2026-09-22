#include "model.h"
#include <cwctype>
namespace dp {
BatteryKind batteryKind(const Device &d, const Reading &reading) {
    if (reading.state == State::Valid)
        return BatteryKind::Present;
    // Positive model identification only; absence of a battery report proves nothing.
    // Sources and verification limits are recorded in docs/SUPPORT.md.
    if (!d.bluetooth && !d.receiver && d.root.starts_with(L"usb\\") &&
        ((d.vendor == 0x046d && d.product == 0xc08b) ||
         (d.vendor == 0x1532 && (d.product == 0x006e || d.product == 0x0098 || d.product == 0x0071))))
        return BatteryKind::None;
    return BatteryKind::Unknown;
}
std::optional<unsigned> hidBatteryPercent(unsigned page, unsigned usage, long minimum, long maximum,
                                          unsigned value) {
    if (page != 6 || usage != 0x20 || minimum != 0 || maximum != 100 || value > 100)
        return {};
    return value;
}
std::optional<unsigned> batteryPercent(std::span<const uint8_t> value) {
    if (value.size() != 1 || value[0] > 100)
        return {};
    return value[0];
}
std::wstring batteryText(const DeviceStatus &d, uint64_t tick) {
    if (d.connection.effective(tick) == State::Disconnected)
        return L"未连接";
    if (d.connection.effective(tick) == State::Unavailable || d.connection.effective(tick) == State::Expired)
        return d.connection.text(tick);
    if (d.connection.value == L"状态未知" && d.battery.effective(tick) != State::Valid)
        return L"状态未知";
    return d.power == BatteryKind::None ? L"-" : d.battery.text(tick);
}
bool publishMouseResults(Snapshot &snapshot, const std::wstring &root, std::vector<DeviceStatus> results,
                         bool active, uint64_t currentGeneration, uint64_t resultGeneration) {
    if (!active || currentGeneration != resultGeneration || results.empty() ||
        std::none_of(snapshot.mice.begin(), snapshot.mice.end(),
                     [&](const auto &d) { return d.root == root; }) ||
        std::any_of(results.begin(), results.end(),
                    [&](const auto &d) { return d.root != root || d.kind != DeviceKind::Mouse; }))
        return false;
    std::erase_if(snapshot.devices,
                  [&](const auto &e) { return e.second.kind == DeviceKind::Mouse && e.second.root == root; });
    for (auto &d : results) {
        auto id = d.id;
        snapshot.devices[id] = std::move(d);
    }
    return true;
}
std::vector<DeviceStatus> visibleDevices(const Snapshot &s, const Settings &c, bool compact) {
    std::vector<DeviceStatus> rows;
    for (const auto &[id, d] : s.devices) {
        if (compact && d.kind != DeviceKind::Mouse)
            continue;
        if (!c.selected.empty() && !c.selected.contains(id))
            continue;
        if (c.selected.empty() && (!d.autoVisible || d.connection.effective(now()) == State::Disconnected))
            continue;
        rows.push_back(d);
    }
    for (const auto &[id, choice] : c.selected) {
        if (s.devices.contains(id) || (compact && choice.kind != DeviceKind::Mouse))
            continue;
        bool known = choice.kind == DeviceKind::Display ? s.displaysOk : s.inventoryOk;
        // A hub query can partially succeed; absence from that result is not proof of unplugging.
        if (choice.kind == DeviceKind::Port)
            known = s.portsOk;
        DeviceStatus d;
        d.id = id;
        d.name = choice.name;
        d.kind = choice.kind;
        d.connection = Reading::unavailable(known ? State::Disconnected : State::Unavailable, L"inventory");
        d.battery = d.dpi = d.connection;
        rows.push_back(std::move(d));
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto &a, const auto &b) { return a.kind != b.kind ? a.kind < b.kind : a.id < b.id; });
    // Identity-derived numbers do not shift when selection order changes.
    std::map<std::pair<DeviceKind, std::wstring>, size_t> totals;
    for (const auto &d : rows)
        ++totals[{d.kind, d.name}];
    for (auto &d : rows) {
        auto key = std::make_pair(d.kind, d.name);
        if (totals[key] > 1) {
            uint32_t number = 2166136261u;
            for (auto ch : d.id) {
                number ^= static_cast<uint32_t>(ch);
                number *= 16777619u;
            }
            d.name += L" #" + std::to_wstring(number);
        }
    }
    return rows;
}
std::wstring wide(std::string_view v) {
    if (v.empty())
        return {};
    int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v.data(), static_cast<int>(v.size()), nullptr, 0);
    if (!n)
        return {};
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v.data(), static_cast<int>(v.size()), r.data(), n);
    return r;
}
std::string utf8(std::wstring_view v) {
    if (v.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}
std::wstring exePath() {
    std::wstring s(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, s.data(), static_cast<DWORD>(s.size()));
    s.resize(n);
    return s;
}
std::wstring errorText(DWORD code) {
    wchar_t *p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&p), 0, nullptr);
    std::wstring s = p ? p : L"操作未成功";
    LocalFree(p);
    return s;
}
State Reading::effective(uint64_t t) const {
    return state == State::Valid && (t < sampled || t - sampled > ttl) ? State::Expired : state;
}
std::wstring Reading::text(uint64_t t) const {
    switch (effective(t)) {
    case State::Valid:
        return value;
    case State::Disconnected:
        return L"未连接";
    case State::Unsupported:
        return L"暂不支持";
    case State::Expired:
        return L"状态已过期";
    default:
        return L"暂不可用";
    }
}
Reading Reading::valid(std::wstring v, std::wstring s, uint64_t ttl, bool warning) {
    return {State::Valid, std::move(v), std::move(s), now(), ttl, warning};
}
Reading Reading::unavailable(State s, std::wstring source) {
    return {s, L"", std::move(source), now(), 15000, s == State::Disconnected};
}
Reading usbSummary(const Snapshot &s, const Settings &c) {
    if (c.ports.empty())
        return Reading::valid(L"未选择接口", L"selection", 60000);
    if (!s.portsOk)
        return Reading::unavailable();
    unsigned connected = 0, unknown = 0, unmapped = 0;
    for (auto &id : c.ports) {
        auto it = std::find_if(s.ports.begin(), s.ports.end(), [&](const Port &p) { return p.id == id; });
        if (it == s.ports.end() || it->state == State::Unavailable)
            ++unknown;
        else {
            connected += it->connected;
            unmapped += !it->physical;
        }
    }
    auto v = std::to_wstring(connected) + L" 个已连接";
    if (unknown)
        v += L" · " + std::to_wstring(unknown) + L" 未知";
    if (unmapped)
        v = L"端口 " + v;
    auto r = Reading::valid(v, L"USB hub ports", 60000, unknown > 0);
    r.sampled = s.inventoryAt;
    return r;
}
bool internalDisplayTechnology(UINT32 t) {
    return t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS ||
           t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
           t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
}
Reading displaySummary(const Snapshot &s, const Settings &c) {
    if (!s.displaysOk)
        return Reading::unavailable();
    if (!c.allDisplays && c.displays.empty())
        return Reading::valid(L"未选择显示器", L"selection", 60000);
    if (c.allDisplays && s.displays.empty()) {
        auto r = Reading::valid(L"未连接", L"QueryDisplayConfig external displays", 60000);
        r.sampled = s.inventoryAt;
        return r;
    }
    unsigned active = 0, connected = 0, missing = 0, disconnected = 0;
    for (auto &d : s.displays)
        if (c.allDisplays || c.displays.contains(d.id)) {
            active += d.enabled;
            connected += d.connected;
            if (!c.allDisplays && !d.connected)
                ++disconnected;
        }
    if (!c.allDisplays)
        for (auto &id : c.displays)
            if (std::none_of(s.displays.begin(), s.displays.end(),
                             [&](const Display &d) { return d.id == id; }))
                ++missing;
    std::wstring v = std::to_wstring(active) + L" 台启用";
    if (connected > active)
        v += L" · " + std::to_wstring(connected - active) + L" 未启用";
    if (missing)
        v += L" · " + std::to_wstring(missing) + L" 未知";
    if (disconnected)
        v += L" · " + std::to_wstring(disconnected) + L" 断开";
    auto r = Reading::valid(v, L"QueryDisplayConfig", 60000, missing + disconnected > 0);
    r.sampled = s.inventoryAt;
    return r;
}
RECT clampRect(RECT r, RECT work) {
    LONG w = std::min(r.right - r.left, work.right - work.left),
         h = std::min(r.bottom - r.top, work.bottom - work.top);
    r.left = std::clamp(r.left, work.left, work.right - w);
    r.top = std::clamp(r.top, work.top, work.bottom - h);
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}
} // namespace dp
