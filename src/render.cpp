#include "render.h"
#include <cmath>
namespace dp {
using namespace Gdiplus;
CardContent contentFor(const Snapshot &s, const Settings &c) {
    CardContent r;
    r.compact = c.compact;
    r.background = c.background;
    auto readings =
        std::array<Reading, 5>{s.battery, s.dpi, usbSummary(s, c), displaySummary(s, c), s.keyboard};
    for (size_t i = 0; i < 5; ++i) {
        r.values[i] = readings[i].text(now());
        // Absence is ordinary status, not automatically an alert. Only an
        // explicit, still-valid warning (for example low battery) gets color.
        r.warning[i] = readings[i].warning && readings[i].effective(now()) == State::Valid;
    }
    return r;
}
std::wstring compactText(const CardContent &c) {
    bool numeric = !c.values[1].empty() && c.values[1][0] >= L'0' && c.values[1][0] <= L'9';
    return L"鼠标 " + c.values[0] + L"  ·  " + (numeric ? c.values[1] + L" DPI" : L"DPI " + c.values[1]);
}
SIZE cardSize(const CardContent &c, UINT dpi) {
    Bitmap b(1, 1, PixelFormat32bppPARGB);
    Graphics g(&b);
    Font f(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    RectF bounds;
    if (!c.compact) {
        int width = 256;
        for (auto &value : c.values) {
            g.MeasureString(value.c_str(), -1, &f, PointF(0, 0), &bounds);
            width = std::max(width, static_cast<int>(std::ceil(bounds.Width)) + 100);
        }
        return {MulDiv(std::min(width, 340), dpi, 96), MulDiv(172, dpi, 96)};
    }
    auto text = compactText(c);
    g.MeasureString(text.c_str(), -1, &f, PointF(0, 0), &bounds);
    return {MulDiv(std::clamp(static_cast<int>(std::ceil(bounds.Width)) + 32, 180, 380), dpi, 96),
            MulDiv(40, dpi, 96)};
}
void rounded(GraphicsPath &p, REAL x, REAL y, REAL w, REAL h, REAL radius) {
    REAL d = radius * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
bool drawCard(HWND hwnd, const CardContent &c, POINT screen, UINT dpi, const std::filesystem::path &png) {
    auto size = cardSize(c, dpi);
    Bitmap bitmap(size.cx, size.cy, PixelFormat32bppPARGB);
    {
        Graphics g(&bitmap);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        REAL scale = dpi / 96.0f;
        g.ScaleTransform(scale, scale);
        REAL w = size.cx / scale, h = size.cy / scale;
        const REAL radius = c.compact ? 18.0f : 26.0f;
        GraphicsPath shape;
        rounded(shape, 1.5f, 1.5f, w - 3, h - 4, radius);
        const bool clear = c.background == 1;
        // Alpha 1 preserves drag hit testing without a visible background tint.
        SolidBrush fill(clear ? Color(1, 255, 255, 255) : Color(64, 250, 252, 255));
        g.FillPath(&fill, &shape);
        Pen border(Color(70, 239, 244, 247), 0.75f);
        g.DrawPath(&border, &shape);
        Font font(L"Microsoft YaHei UI", 13, FontStyleRegular, UnitPixel),
            valueFont(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
        SolidBrush label(clear ? Color(255, 40, 48, 59) : Color(255, 62, 72, 86)),
            normal(Color(255, 22, 30, 42)), warning(Color(255, 143, 76, 14));
        // Dark filled glyphs remain readable on pale areas; the light keyline
        // isolates them from dark wallpaper, stronger on the transparent variant.
        auto text = [&](const std::wstring &value, Font &f, const RectF &box, StringFormat &format,
                        Brush *ink) {
            GraphicsPath glyphs;
            FontFamily family;
            f.GetFamily(&family);
            glyphs.AddString(value.c_str(), -1, &family, f.GetStyle(), f.GetSize(), box, &format);
            Pen halo(Color(clear ? 235 : 185, 255, 255, 255), clear ? 2.2f : 1.3f);
            halo.SetLineJoin(LineJoinRound);
            g.DrawPath(&halo, &glyphs);
            g.FillPath(ink, &glyphs);
        };
        StringFormat left;
        left.SetLineAlignment(StringAlignmentCenter);
        left.SetFormatFlags(StringFormatFlagsNoWrap);
        left.SetTrimming(StringTrimmingEllipsisCharacter);
        if (c.compact) {
            auto value = compactText(c);
            RectF box(16, 0, w - 32, h - 2);
            text(value, valueFont, box, left, c.warning[0] || c.warning[1] ? &warning : &normal);
        } else {
            const wchar_t *labels[] = {L"鼠标电量", L"鼠标 DPI", L"USB 接口", L"外接屏幕", L"外接键盘"};
            StringFormat right(&left);
            right.SetAlignment(StringAlignmentFar);
            for (int i = 0; i < 5; ++i) {
                RectF a(16, 14.0f + i * 28, 68, 28), b(82, 14.0f + i * 28, w - 98, 28);
                text(labels[i], font, a, left, &label);
                text(c.values[i], valueFont, b, right, c.warning[i] ? &warning : &normal);
            }
        }
    }
    if (!png.empty()) {
        CLSID encoder = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        if (bitmap.Save(png.c_str(), &encoder, nullptr) != Ok)
            return false;
    }
    if (!hwnd)
        return true;
    HBITMAP hbitmap = nullptr;
    if (bitmap.GetHBITMAP(Color(0, 0, 0, 0), &hbitmap) != Ok)
        return false;
    HDC dc = CreateCompatibleDC(nullptr);
    auto old = SelectObject(dc, hbitmap);
    POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    BOOL ok = UpdateLayeredWindow(hwnd, nullptr, &screen, &size, dc, &origin, 0, &blend, ULW_ALPHA);
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(hbitmap);
    int radius = MulDiv(c.compact ? 36 : 52, dpi, 96);
    auto region = CreateRoundRectRgn(1, 1, size.cx - 1, size.cy - 1, radius, radius);
    if (!SetWindowRgn(hwnd, region, FALSE))
        DeleteObject(region);
    return ok != FALSE;
}
HICON trayIcon() {
    auto resource = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (resource)
        return resource;
    Bitmap b(32, 32, PixelFormat32bppPARGB);
    Graphics g(&b);
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    GraphicsPath p;
    rounded(p, 3, 3, 26, 26, 8);
    SolidBrush fill(Color(255, 251, 252, 254)), white(Color(255, 126, 138, 153));
    g.FillPath(&fill, &p);
    g.FillRectangle(&white, 9, 10, 14, 3);
    g.FillRectangle(&white, 9, 16, 9, 3);
    g.FillEllipse(&white, 20, 20, 4, 4);
    HICON icon = nullptr;
    b.GetHICON(&icon);
    return icon;
}
} // namespace dp
