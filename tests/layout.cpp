#include "render.h"
#include <iostream>
#include <stdexcept>
using namespace dp;
int main() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        return 2;
    int result = 0;
    try {
        Snapshot snapshot;
        for (int i = 0; i < 40; ++i) {
            DeviceStatus d;
            d.id = L"mouse:" + std::to_wstring(i);
            d.name = L"很长的外接鼠标名称 Long external gaming mouse " + std::to_wstring(i);
            d.connection = Reading::valid(L"已连接", L"fixture");
            d.battery = Reading::valid(L"75% · 充电中", L"fixture");
            d.dpi =
                i % 2 ? Reading::unavailable(State::Unsupported) : Reading::valid(L"1600 × 3200", L"fixture");
            snapshot.devices[d.id] = d;
        }
        Settings settings;
        for (bool compact : {false, true})
            for (int background : {1, 2}) {
                settings.compact = compact;
                settings.background = background;
                auto content = contentFor(snapshot, settings);
                if (content.rows.size() != 40)
                    throw std::runtime_error("lost mouse rows");
                for (UINT dpi : {96u, 120u, 144u, 192u}) {
                    auto size = cardSize(content, dpi);
                    if (size.cx != MulDiv(420, dpi, 96) || size.cy != MulDiv(600, dpi, 96))
                        throw std::runtime_error("scaled viewport bounds");
                    content.scroll = 40 * 32 + 16 - 600;
                    if (!drawCard(nullptr, content, {}, dpi))
                        throw std::runtime_error("render overflow");
                }
                content.maxWidth = 200;
                content.maxHeight = 160;
                if (cardSize(content, 96).cx != 200 || cardSize(content, 96).cy != 160)
                    throw std::runtime_error("small work area bounds");
            }
        settings.selected[L"keyboard:none"] = {DeviceKind::Keyboard, L"Keyboard"};
        settings.compact = true;
        if (contentFor(snapshot, settings).rows.front().value != L"未关注鼠标")
            throw std::runtime_error("compact selection placeholder");
        std::cout << "Layout tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    Gdiplus::GdiplusShutdown(token);
    return result;
}
