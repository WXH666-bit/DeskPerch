#pragma once
#include "common.h"
namespace dp {
enum class State { Valid, Disconnected, Unsupported, Unavailable, Expired };
enum class DeviceKind { Mouse, Keyboard, Display, Port };
enum class BatteryKind { Unknown, Present, None };
struct Reading {
    State state = State::Unavailable;
    std::wstring value, source;
    uint64_t sampled = 0, ttl = 15000;
    bool warning = false;
    State effective(uint64_t t) const;
    std::wstring text(uint64_t t) const;
    static Reading valid(std::wstring value, std::wstring source, uint64_t ttl = 15000, bool warning = false);
    static Reading unavailable(State state = State::Unavailable, std::wstring source = L"");
};
struct Device {
    std::wstring id, name, path, root, container, transport;
    bool mouse = false, keyboard = false, receiver = false, bluetooth = false;
    bool present = true, operational = true, internal = false;
    unsigned vendor = 0, product = 0, usagePage = 0, usage = 0, inputLength = 0, outputLength = 0;
    std::vector<std::wstring> batteryPaths;
};
struct DeviceStatus {
    DeviceKind kind = DeviceKind::Mouse;
    std::wstring id, name, root;
    Reading connection, battery, dpi;
    BatteryKind power = BatteryKind::Unknown;
    bool autoVisible = true;
};
struct Selection {
    DeviceKind kind = DeviceKind::Mouse;
    std::wstring name;
    bool operator==(const Selection &) const = default;
};
struct Display {
    std::wstring id, name;
    bool connected = false, enabled = false;
};
struct Port {
    std::wstring id, name, companion;
    State state = State::Unavailable;
    bool connected = false, physical = false;
};
struct Snapshot {
    std::map<std::wstring, DeviceStatus> devices;
    std::vector<Device> mice, keyboards;
    std::vector<Display> displays;
    std::vector<Port> ports;
    std::set<std::wstring> excludedDevices;
    bool inventoryOk = false, displaysOk = false, portsOk = false;
    uint64_t inventoryAt = 0;
};
struct Settings {
    std::map<std::wstring, Selection> selected;
    int background = 2; // 1 clear, 2 pale white (default); legacy 0 migrates to 2
    bool compact = false, locked = false, visible = true, positioned = false, allDisplays = true;
    int x = 24, y = 24;
    std::wstring monitor, mouse, keyboard;
    std::set<std::wstring> ports, displays;
};
bool publishMouseResults(Snapshot &, const std::wstring &root, std::vector<DeviceStatus>, bool active,
                         uint64_t currentGeneration, uint64_t resultGeneration);
std::vector<DeviceStatus> visibleDevices(const Snapshot &, const Settings &, bool compact);
std::wstring batteryText(const DeviceStatus &, uint64_t tick);
BatteryKind batteryKind(const Device &, const Reading &);
std::optional<unsigned> hidBatteryPercent(unsigned page, unsigned usage, long minimum, long maximum,
                                          unsigned value);
std::optional<unsigned> batteryPercent(std::span<const uint8_t> value);
Reading usbSummary(const Snapshot &, const Settings &);
Reading displaySummary(const Snapshot &, const Settings &);
bool internalDisplayTechnology(UINT32 technology);
RECT clampRect(RECT desired, RECT work);
std::string serializeSettings(const Settings &);
std::optional<Settings> parseSettings(std::string_view);
} // namespace dp
