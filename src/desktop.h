#pragma once
#include "model.h"
namespace dp {
class DesktopHost {
    HWND host_ = nullptr, icons_ = nullptr;

  public:
    bool discover();
    bool valid() const;
    HWND window() const { return host_; }
    bool attach(HWND child);
};
struct Monitor {
    HMONITOR handle = nullptr;
    std::wstring name;
    RECT work{};
    UINT dpi = 96;
};
std::vector<Monitor> monitors();
Monitor monitorForPoint(POINT);
POINT restorePosition(const Settings &, SIZE size, bool reset = false);
void rememberPosition(Settings &, RECT);
} // namespace dp
