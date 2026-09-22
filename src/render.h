#pragma once
#include "model.h"
#include <objidl.h>
#include <gdiplus.h>
namespace dp {
struct CardRow {
    std::wstring name, value;
    bool warning = false;
    bool operator==(const CardRow &) const = default;
};
struct CardContent {
    std::vector<CardRow> rows;
    bool compact = false;
    int background = 2, maxWidth = 420, maxHeight = 600, scroll = 0;
    bool operator==(const CardContent &) const = default;
};
CardContent contentFor(const Snapshot &, const Settings &);
SIZE cardSize(const CardContent &, UINT dpi);
bool drawCard(HWND, const CardContent &, POINT screen, UINT dpi, const std::filesystem::path &png = {});
HICON trayIcon();
} // namespace dp
