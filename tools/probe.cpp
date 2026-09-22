#include "devices.h"
#include "settings.h"
#include "render.h"
#include <iostream>
#include <fstream>

using namespace dp;
void windowReport(std::ostream &o) {
    auto owner = FindWindowW(L"DeskPerch.Owner", nullptr);
    HWND card = nullptr;
    EnumWindows(
        [](HWND h, LPARAM data) -> BOOL {
            EnumChildWindows(
                h,
                [](HWND c, LPARAM p) -> BOOL {
                    wchar_t name[80]{};
                    GetClassNameW(c, name, 80);
                    if (wcscmp(name, L"DeskPerch.Card") == 0)
                        *reinterpret_cast<HWND *>(p) = c;
                    return TRUE;
                },
                data);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&card));
    o << "owner_present=" << (owner != nullptr) << " card_present=" << (card != nullptr) << "\n";
    if (card) {
        RECT r{};
        GetWindowRect(card, &r);
        auto style = GetWindowLongPtrW(card, GWL_STYLE), ex = GetWindowLongPtrW(card, GWL_EXSTYLE);
        wchar_t parent[80]{};
        GetClassNameW(GetParent(card), parent, 80);
        o << "parent=" << utf8(parent) << " visible=" << IsWindowVisible(card)
          << " child=" << !!(style & WS_CHILD) << " no_activate=" << !!(ex & WS_EX_NOACTIVATE)
          << " tool_window=" << !!(ex & WS_EX_TOOLWINDOW) << " topmost=" << !!(ex & WS_EX_TOPMOST) << "\n";
        o << "rect=" << r.left << "," << r.top << "," << r.right << "," << r.bottom
          << " dpi=" << GetDpiForWindow(card) << "\n";
        auto fg = GetForegroundWindow();
        for (auto c = GetWindow(GetParent(card), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            wchar_t cls[80]{};
            GetClassNameW(c, cls, 80);
            RECT cr{};
            GetWindowRect(c, &cr);
            o << "sibling=" << utf8(cls) << " visible=" << IsWindowVisible(c)
              << " ex=" << GetWindowLongPtrW(c, GWL_EXSTYLE)
              << " rect=" << cr.left << "," << cr.top << "," << cr.right << "," << cr.bottom << "\n";
        }
        auto hit = WindowFromPoint({(r.left+r.right)/2,(r.top+r.bottom)/2});
        wchar_t hitClass[80]{};
        GetClassNameW(hit, hitClass, 80);
        o << "center_window=" << utf8(hitClass) << "\n";
        o << "app_has_foreground=" << (fg == owner || fg == card) << " center_hits_card="
          << (WindowFromPoint({(r.left + r.right) / 2, (r.top + r.bottom) / 2}) == card)
          << " corner_hits_card=" << (WindowFromPoint({r.left, r.top}) == card) << "\n";
    }
}
int wmain(int argc, wchar_t **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);
    std::ostream *stream = &std::cout;
    std::ofstream file;
    bool render = argc == 3 && std::wstring_view(argv[1]) == L"--render";
    bool windows = argc >= 2 && std::wstring_view(argv[1]) == L"--window";
    if (!render && ((windows && argc == 3) || (!windows && argc == 2))) {
        file.open(std::filesystem::path(argv[argc - 1]), std::ios::binary);
        if (!file)
            return 2;
        stream = &file;
    }
    auto &o = *stream;
    if (windows) {
        windowReport(o);
        return 0;
    }
    auto inventory = enumerateInputs();
    Snapshot snapshot;
    snapshot.inventoryOk = inventory.ok;
    snapshot.mice = inventory.mice;
    snapshot.keyboards = inventory.keyboards;
    o << "DeskPerch real-device diagnostic (read-only queries)\n";
    for (auto &d : inventory.mice) {
        o << "MOUSE " << utf8(d.name) << "\n";
        LogitechMouse m;
        Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (m.connect(inventory.controls, d.root, stop.get())) {
            Reading b, dpi;
            m.poll(b, dpi, true);
            o << "HIDPP name=" << utf8(m.name()) << " slot=" << m.slot()
              << "\nbattery=" << utf8(b.text(now())) << " source=" << utf8(b.source)
              << "\ndpi=" << utf8(dpi.text(now())) << " source=" << utf8(dpi.source) << "\n";
            snapshot.battery = b;
            snapshot.dpi = dpi;
            snapshot.mouseName = m.name();
        } else {
            snapshot.battery = standardBattery(d);
            snapshot.dpi = Reading::unavailable(State::Unsupported);
            o << "HIDPP unsupported/unavailable; battery=" << utf8(snapshot.battery.text(now())) << "\n";
        }
    }
    for (auto &d : inventory.keyboards)
        o << "KEYBOARD " << utf8(d.name) << " transport=" << utf8(d.transport) << "\n";
    if (!inventory.keyboards.empty()) {
        auto &k = inventory.keyboards.front();
        snapshot.keyboard = k.receiver || k.bluetooth ? Reading::valid(L"状态未知", L"PnP")
                                                      : Reading::valid(L"已连接 · " + k.transport, L"PnP");
    }
    snapshot.displays = enumerateDisplays(snapshot.displaysOk);
    o << "DISPLAYS ok=" << snapshot.displaysOk << "\n";
    for (auto &d : snapshot.displays)
        o << utf8(d.name) << " connected=" << d.connected << " enabled=" << d.enabled << "\n";
    snapshot.ports = enumeratePorts(snapshot.portsOk);
    snapshot.inventoryAt = now();
    o << "PORTS ok=" << snapshot.portsOk << "\n";
    for (auto &p : snapshot.ports)
        o << utf8(p.name) << " connected=" << p.connected << " physical=" << p.physical
          << " state=" << static_cast<int>(p.state) << "\n";
    if (render) {
        std::filesystem::path folder = argv[2];
        std::filesystem::create_directories(folder);
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        {
            Settings s;
            for (UINT dpi : {96u, 120u, 144u, 192u})
                for (bool compact : {false, true}) {
                    s.compact = compact;
                    auto path =
                        folder / ((compact ? L"compact-" : L"full-") + std::to_wstring(dpi) + L".png");
                    if (!drawCard(nullptr, contentFor(snapshot, s), {}, dpi, path))
                        return 3;
                }
        }
        Gdiplus::GdiplusShutdown(token);
    }
    return inventory.ok ? 0 : 1;
}
