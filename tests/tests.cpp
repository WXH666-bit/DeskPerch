#include "model.h"
#include "hidpp.h"
#include "tray_menu.h"
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
        s.selected[L"mouse:one"] = {DeviceKind::Mouse, L"Mouse A"};
        s.selected[L"keyboard:two"] = {DeviceKind::Keyboard, L"Keyboard B"};
        s.x = -42;
        s.background = 2;
        auto copy = parseSettings(serializeSettings(s));
        check(copy && copy->selected == s.selected && copy->mouse.empty() && copy->ports.empty() &&
                  copy->x == -42 && copy->compact,
              "settings roundtrip");
        check(copy && copy->background == 2, "background persists");
        auto legacy = std::string("DeskPerch 2\n1 1 0 1 0 -42 24\n\"monitor\"\n\"old mouse\"\n\"old "
                                  "keyboard\"\n1\n\"port\"\n0\n1\n");
        auto migrated = parseSettings(legacy);
        check(migrated && migrated->background == 1 && migrated->selected.empty() &&
                  migrated->mouse.empty() && migrated->locked && !migrated->visible && migrated->compact &&
                  migrated->x == -42,
              "v2 resets selections while preserving preferences");
        legacy[10] = '1';
        legacy.resize(legacy.size() - 2);
        check(parseSettings(legacy) && parseSettings(legacy)->selected.empty(), "v1 migration");
        check(Settings{}.background == 2, "first launch defaults to pale white");
        s.background = 1;
        check(parseSettings(serializeSettings(s))->background == 1, "explicit transparent choice persists");
        s.background = 0;
        check(parseSettings(serializeSettings(s))->background == 2,
              "retired smoked choice migrates to white");
        s.background = 2;
        s.background = 9;
        check(!parseSettings(serializeSettings(s)), "invalid background rejected");
        s.background = 2;
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
        Snapshot many;
        many.inventoryOk = many.displaysOk = true;
        DeviceStatus mouse;
        mouse.id = L"mouse:a";
        mouse.name = L"Same";
        mouse.connection = Reading::valid(L"已连接", L"test");
        mouse.battery = Reading::valid(L"62%", L"test", 180000);
        mouse.dpi = Reading::valid(L"1600", L"test");
        many.devices[mouse.id] = mouse;
        mouse.id = L"mouse:b";
        mouse.dpi = Reading::unavailable();
        many.devices[mouse.id] = mouse;
        DeviceStatus port;
        port.id = L"port:a";
        port.kind = DeviceKind::Port;
        port.connection = Reading::unavailable(State::Disconnected);
        many.devices[port.id] = port;
        Settings attention;
        auto rows = visibleDevices(many, attention, false);
        check(rows.size() == 2 && rows[0].name != rows[1].name,
              "auto excludes idle ports and distinguishes mice");
        check(rows[0].dpi.state == State::Valid && rows[1].dpi.state == State::Unavailable,
              "one failure does not poison another mouse");
        attention.selected[port.id] = {DeviceKind::Port, L"saved port"};
        check(visibleDevices(many, attention, false).size() == 1 &&
                  visibleDevices(many, attention, true).empty(),
              "first selection is global; compact never auto-adds mouse");
        attention.selected[L"mouse:a"] = {DeviceKind::Mouse, L"saved mouse"};
        check(visibleDevices(many, attention, false).size() == 2, "cross category multi selection");
        many.devices.erase(L"mouse:a");
        rows = visibleDevices(many, attention, true);
        check(rows.size() == 1 && rows[0].name == L"saved mouse" &&
                  rows[0].connection.state == State::Disconnected,
              "missing selected mouse retains name");
        many.inventoryOk = false;
        check(visibleDevices(many, attention, true)[0].connection.state == State::Unavailable,
              "enumeration failure is not unplug");
        attention.selected.clear();
        check(visibleDevices(many, attention, false).size() == 1, "clear selections restores auto");
        mouse.power = BatteryKind::None;
        check(batteryText(mouse, now()) == L"-", "confirmed batteryless");
        mouse.power = BatteryKind::Present;
        check(batteryText(mouse, now()) == L"62%", "rechargeable retains actual battery");
        check(batteryPercent(std::array<uint8_t, 1>{0}) == 0u &&
                  batteryPercent(std::array<uint8_t, 1>{100}) == 100u,
              "BAS endpoints");
        check(!batteryPercent(std::array<uint8_t, 1>{101}) &&
                  !batteryPercent(std::array<uint8_t, 2>{50, 0}) && !batteryPercent({}),
              "BAS rejects out of range, oversized and missing data");
        Device wired;
        wired.root = L"usb\\vid_1532";
        wired.vendor = 0x1532;
        wired.product = 0x006e;
        check(batteryKind(wired, Reading::unavailable()) == BatteryKind::None, "documented wired model");
        check(batteryKind(wired, Reading::valid(L"40%", L"HID")) == BatteryKind::Present,
              "actual battery overrides catalog");
        wired.product = 0x9999;
        check(batteryKind(wired, Reading::unavailable()) == BatteryKind::Unknown,
              "USB alone is not batteryless");
        check(hidBatteryPercent(6, 0x20, 0, 100, 55) == 55u, "HID percentage feature");
        check(!hidBatteryPercent(6, 0x20, 0, 4, 2) && !hidBatteryPercent(6, 0x20, 0, 100, 101) &&
                  !hidBatteryPercent(1, 0x20, 0, 100, 50),
              "reject levels, overflow, wrong usage page");
        mouse.connection = Reading::unavailable();
        check(batteryText(mouse, now()) == L"暂不可用", "connection failure suppresses previous battery");
        mouse.connection = Reading::valid(L"状态未知", L"receiver");
        mouse.battery = Reading::unavailable(State::Unsupported);
        check(batteryText(mouse, now()) == L"状态未知", "receiver presence does not imply online");
        Snapshot publication;
        Device physical;
        physical.root = L"root-a";
        publication.mice.push_back(physical);
        DeviceStatus fresh;
        fresh.root = physical.root;
        fresh.id = L"mouse:a";
        fresh.dpi = Reading::valid(L"800", L"test");
        check(!publishMouseResults(publication, physical.root, {fresh}, true, 2, 1),
              "reject late generation");
        check(!publishMouseResults(publication, physical.root, {fresh}, false, 2, 2), "reject while hidden");
        check(publishMouseResults(publication, physical.root, {fresh}, true, 2, 2), "publish current result");
        publication.mice.clear();
        fresh.dpi = Reading::valid(L"9999", L"late");
        check(!publishMouseResults(publication, physical.root, {fresh}, true, 2, 2) &&
                  publication.devices.at(fresh.id).dpi.value == L"800",
              "removed root cannot resurrect late data");
        TrayMenuActions popup;
        Settings choices;
        auto populate = [&] {
            check(popup.begin(), "start popup session");
            popup.add([&] {
                if (!choices.selected.erase(L"mouse:test"))
                    choices.selected[L"mouse:test"] = {DeviceKind::Mouse, L"Test mouse"};
            });
            popup.add([&] {
                if (!choices.selected.erase(L"port:test"))
                    choices.selected[L"port:test"] = {DeviceKind::Port, L"Test port"};
            });
        };
        populate();
        check(!popup.begin(), "duplicate callback cannot replace open popup actions");
        auto clicked = popup.finish(1000);
        check(static_cast<bool>(clicked), "click survives rejected nested popup");
        clicked();
        check(choices.selected.size() == 1 && choices.selected.contains(L"mouse:test"),
              "first click selects only requested device");
        check(!popup.finish(1000), "completed popup cannot dispatch twice");
        populate();
        popup.finish(1001)();
        auto savedChoices = parseSettings(serializeSettings(choices));
        check(savedChoices && savedChoices->selected.size() == 2 &&
                  savedChoices->selected.contains(L"port:test"),
              "cross category choices survive config roundtrip");
        populate();
        check(!popup.finish(0) && choices.selected.size() == 2, "cancel preserves selections");
        populate();
        popup.finish(1000)();
        populate();
        popup.finish(1001)();
        check(choices.selected.empty(), "last unchecked choice restores auto mode");
        check(trayContextEvent(true, MAKELPARAM(WM_CONTEXTMENU, 1)) &&
                  !trayContextEvent(true, MAKELPARAM(WM_RBUTTONUP, 1)),
              "version 4 uses context event only");
        check(trayContextEvent(false, WM_RBUTTONUP) && !trayContextEvent(false, WM_CONTEXTMENU),
              "legacy tray uses right button release only");
        std::cout << "All core tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
