#pragma once
#include "model.h"
#include <objidl.h>
#include <gdiplus.h>
namespace dp {
struct CardContent {
    std::array<std::wstring, 5> values;
    std::array<bool, 5> warning{};
    bool compact = false;
    int background = 2;
    bool operator==(const CardContent &) const = default;
};
CardContent contentFor(const Snapshot &, const Settings &);
SIZE cardSize(const CardContent &, UINT dpi);
bool drawCard(HWND, const CardContent &, POINT screen, UINT dpi, const std::filesystem::path &png = {});
HICON trayIcon();
} // namespace dp
