#include "model.h"
#include "hidpp.h"
#include <iostream>
#include <stdexcept>
using namespace dp;
void check(bool v, const char *what) {
    if (!v)
        throw std::runtime_error(what);
}
int main() {
    try {
        auto r = Reading::valid(L"82%", L"test", 100);
        check(r.text(r.sampled + 101) == L"状态已过期", "expired reading must not expose value");
        check(Reading::unavailable().text(now()) != L"0%", "unknown battery not zero");
        std::array<uint8_t, 7> response{0x10, 1, 3, 0x2d, 0, 6, 64};
        check(classifyReply(response, 1, 3, 0x2d) == ReplyKind::Success, "matching reply");
        check(classifyReply(response, 1, 3, 0x2e) == ReplyKind::Ignore, "other client");
        check(classifyReply(response, 2, 3, 0x2d) == ReplyKind::Ignore, "other device");
        check(classifyReply(std::span(response).first(4), 1, 3, 0x2d) == ReplyKind::Ignore, "short packet");
        response[0] = 0x11;
        check(classifyReply(response, 1, 3, 0x2d) == ReplyKind::Ignore, "truncated long report");
        response = {0x10, 1, 0xff, 3, 0x2d, 9, 0};
        check(classifyReply(response, 1, 3, 0x2d) == ReplyKind::Error, "protocol error");
        std::array<uint8_t, 9> dpi{0, 6, 64, 3, 32, 12, 128, 3, 32};
        auto v = decodeDpi(dpi, true, true);
        check(v && v->x == 1600 && v->y == 3200, "separate XY DPI");
        dpi[1] = dpi[2] = 0;
        check(!decodeDpi(dpi, false, false), "no preset substitution");
        Settings s;
        s.compact = true;
        s.mouse = L"USB\\设备\"/123";
        s.ports = {L"Hub1/1", L"Hub2/2"};
        s.x = -42;
        s.background = 2;
        auto copy = parseSettings(serializeSettings(s));
        check(copy && copy->mouse == s.mouse && copy->ports == s.ports && copy->x == -42 && copy->compact,
              "settings roundtrip");
        check(copy && copy->background == 2, "background persists");
        auto legacy = serializeSettings(s);
        legacy.replace(0, 11, "DeskPerch 1");
        legacy.resize(legacy.size() - 2);
        auto migrated = parseSettings(legacy);
        check(migrated && migrated->background == 2 && migrated->mouse == s.mouse,
              "legacy settings use pale white without losing device selection");
        check(Settings{}.background == 2, "first launch defaults to pale white");
        s.background = 1;
        check(parseSettings(serializeSettings(s))->background == 1, "explicit transparent choice persists");
        s.background = 0;
        check(parseSettings(serializeSettings(s))->background == 2,
              "retired smoked choice migrates to white");
        s.background = 2;
        auto badBackground = serializeSettings(s);
        badBackground[badBackground.size() - 2] = '9';
        check(!parseSettings(badBackground), "invalid background rejected");
        check(!parseSettings("DeskPerch 9"), "unknown config version");
        check(!parseSettings(serializeSettings(s) + "garbage"), "trailing corrupted config");
        check(!parseSettings(std::string(131073, 'x')), "oversized config");
        auto invalid = serializeSettings(s);
        invalid.replace(invalid.find("1 0 1"), 5, "2 0 1");
        check(!parseSettings(invalid), "invalid boolean config");
        auto rect = clampRect({-1000, 999, -744, 1171}, {0, 0, 1920, 1080});
        check(rect.left == 0 && rect.bottom == 1080, "visible recovery");
        rect = clampRect({-5000, -5000, -4744, -4828}, {-1920, -1080, 0, 0});
        check(rect.left == -1920 && rect.top == -1080, "negative-coordinate monitor recovery");
        Snapshot snap;
        check(internalDisplayTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED),
              "exclude internal eDP panel");
        check(internalDisplayTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS), "exclude LVDS panel");
        check(internalDisplayTechnology(static_cast<UINT32>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL)),
              "exclude internal panel");
        check(!internalDisplayTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI), "keep HDMI display");
        check(!internalDisplayTechnology(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL),
              "keep external DisplayPort");
        check(!internalDisplayTechnology(static_cast<UINT32>(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER)),
              "unknown transport is not proof of internal panel");
        snap.portsOk = true;
        snap.inventoryAt = now();
        snap.ports = {{L"a", L"A", L"", State::Valid, true, true}};
        Settings cfg;
        cfg.ports = {L"a", L"b"};
        check(usbSummary(snap, cfg).text(now()).find(L"未知") != std::wstring::npos,
              "missing port not disconnected");
        snap.displaysOk = true;
        snap.displays = {{L"d", L"screen", true, false}};
        check(displaySummary(snap, cfg).text(now()).find(L"未启用") != std::wstring::npos,
              "connected not enabled");
        snap.inventoryAt = now() - 61000;
        check(displaySummary(snap, cfg).effective(now()) == State::Expired, "display inventory expires");
        snap.inventoryAt = now();
        cfg.allDisplays = false;
        cfg.displays = {L"missing"};
        check(displaySummary(snap, cfg).text(now()).find(L"未知") != std::wstring::npos,
              "missing display is unknown");
        cfg.displays = {L"d"};
        snap.displays[0].connected = false;
        check(displaySummary(snap, cfg).text(now()).find(L"断开") != std::wstring::npos,
              "confirmed disconnected display");
        std::cout << "All core tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
