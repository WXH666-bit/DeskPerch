#include "desktop.h"
#include <shellscalingapi.h>
namespace dp {
bool DesktopHost::discover() {
    host_ = icons_ = nullptr;
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto self = reinterpret_cast<DesktopHost *>(p);
            wchar_t name[64]{};
            GetClassNameW(h, name, 64);
            if (wcscmp(name, L"Progman") && wcscmp(name, L"WorkerW"))
                return TRUE;
            auto view = FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr);
            auto list = view ? FindWindowExW(view, nullptr, L"SysListView32", nullptr) : nullptr;
            if (list && IsWindowVisible(h) && IsWindowVisible(view)) {
                // Modern Explorer composites DefView separately (WS_EX_LAYERED).
                // A Progman sibling can be logically visible but absent from its surface.
                // Join the actual icon-view surface, above its list within desktop only.
                self->host_ = view;
                self->icons_ = list;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    return valid();
}
bool DesktopHost::valid() const {
    return IsWindow(host_) && IsWindow(icons_) && GetParent(icons_) == host_ && IsWindowVisible(host_);
}
bool DesktopHost::attach(HWND child) {
    if (!valid())
        return false;
    ShowWindow(child, SW_HIDE);
    SetWindowLongPtrW(child, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    SetLastError(0);
    auto previous = SetParent(child, host_);
    if (!previous && GetLastError() != 0)
        return false;
    return SetWindowPos(child, HWND_TOP, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
}
std::vector<Monitor> monitors() {
    std::vector<Monitor> list;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(h, &mi)) {
                UINT x = 96, y = 96;
                GetDpiForMonitor(h, MDT_EFFECTIVE_DPI, &x, &y);
                reinterpret_cast<std::vector<Monitor> *>(p)->push_back({h, mi.szDevice, mi.rcWork, x});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&list));
    return list;
}
Monitor monitorForPoint(POINT p) {
    auto list = monitors();
    auto h = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    for (auto &m : list)
        if (m.handle == h)
            return m;
    return list.empty() ? Monitor{nullptr, L"", {0, 0, 1920, 1080}, 96} : list.front();
}
POINT restorePosition(const Settings &c, SIZE size, bool reset) {
    auto list = monitors();
    POINT cursor{};
    GetCursorPos(&cursor);
    Monitor m = monitorForPoint(reset ? cursor : POINT{0, 0});
    if (!reset)
        for (auto &candidate : list)
            if (candidate.name == c.monitor) {
                m = candidate;
                break;
            }
    POINT p;
    if (c.positioned && !reset) {
        p = {m.work.left + MulDiv(c.x, m.dpi, 96), m.work.top + MulDiv(c.y, m.dpi, 96)};
    } else {
        p = {m.work.right - size.cx - MulDiv(24, m.dpi, 96), m.work.top + MulDiv(24, m.dpi, 96)};
    }
    auto r = clampRect({p.x, p.y, p.x + size.cx, p.y + size.cy}, m.work);
    return {r.left, r.top};
}
void rememberPosition(Settings &c, RECT r) {
    auto m = monitorForPoint({r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2});
    c.monitor = m.name;
    c.x = MulDiv(r.left - m.work.left, 96, m.dpi);
    c.y = MulDiv(r.top - m.work.top, 96, m.dpi);
    c.positioned = true;
}
} // namespace dp
