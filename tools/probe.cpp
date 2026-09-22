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
              << " ex=" << GetWindowLongPtrW(c, GWL_EXSTYLE) << " rect=" << cr.left << "," << cr.top << ","
              << cr.right << "," << cr.bottom << "\n";
        }
        auto hit = WindowFromPoint({(r.left + r.right) / 2, (r.top + r.bottom) / 2});
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
    DeviceService service(nullptr, Settings{});
    service.start();
    // Allow discovery of receiver slots before taking a real collector snapshot.
    Sleep(12000);
    auto snapshot = service.snapshot();
    service.stop();
    o << "DeskPerch real-device diagnostic (read-only queries)\n";
    for (const auto &[id, d] : snapshot.devices)
        o << "DEVICE " << utf8(d.name) << " kind=" << static_cast<int>(d.kind)
          << "\nconnection=" << utf8(d.connection.text(now())) << "\nbattery=" << utf8(batteryText(d, now()))
          << " source=" << utf8(d.battery.source) << "\ndpi=" << utf8(d.dpi.text(now()))
          << " source=" << utf8(d.dpi.source) << "\n";
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
    return snapshot.inventoryOk ? 0 : 1;
}
