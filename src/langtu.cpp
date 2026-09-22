#include "langtu.h"
namespace dp {
bool supportedLangtu(const Device &d) {
    // Exact receiver verified on the borrowed M8 MAX. Do not enable a whole VID.
    return d.vendor == 0xa8a5 && d.product == 0x2255 && d.name == L"LTM8 2.4G";
}
static bool header(std::span<const uint8_t> p, uint8_t command) {
    // Payload excludes Windows' leading report ID. Unsolicited AA FA/AA ED
    // notifications and replies to other commands are never data for this query.
    return p.size() == 64 && p[0] == 0xaa && p[1] == command && p[2] == 0xa5 && p[5] == 1 && p[6] == 1 &&
           p[7] == 1;
}
std::optional<unsigned> decodeLangtuBattery(std::span<const uint8_t> p) {
    if (!header(p, 0x30) || p[4] != 0x0a || p[8] > 100)
        return {};
    return p[8];
}
std::optional<unsigned> decodeLangtuDpi(std::span<const uint8_t> p) {
    if (!header(p, 0x0e) || p[4] != 0x2e || p[11] < 1 || p[11] > 6 || p[12] < 1 || p[12] > p[11])
        return {};
    for (unsigned i = 0; i < p[11]; ++i) {
        unsigned value = p[13 + 2 * i] | (p[14 + 2 * i] << 8);
        if (value < 50 || value > 50000)
            return {};
    }
    unsigned offset = 13 + (p[12] - 1) * 2;
    unsigned dpi = p[offset] | (p[offset + 1] << 8);
    if (dpi < 50 || dpi > 50000)
        return {};
    return dpi;
}
static bool transfer(HANDLE h, HANDLE stop, bool write, std::span<uint8_t> b, DWORD &n, DWORD timeout) {
    if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
        return false;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event)
        return false;
    OVERLAPPED ov{};
    ov.hEvent = event.get();
    BOOL ok = write ? WriteFile(h, b.data(), static_cast<DWORD>(b.size()), &n, &ov)
                    : ReadFile(h, b.data(), static_cast<DWORD>(b.size()), &n, &ov);
    if (ok)
        return true;
    if (GetLastError() != ERROR_IO_PENDING)
        return false;
    HANDLE waits[]{event.get(), stop};
    if (WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, timeout) != WAIT_OBJECT_0) {
        CancelIoEx(h, &ov);
        GetOverlappedResult(h, &ov, &n, TRUE);
        return false;
    }
    return GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
}
static std::vector<uint8_t> query(const Device &control, HANDLE stop, uint8_t command) {
    // Fresh handle has no old notification queue. Only vendor control reports
    // are opened; no pointer movement or keyboard input is collected.
    Handle file(CreateFileW(control.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                            nullptr));
    if (!file)
        return {};
    std::array<uint8_t, 65> packet{0, 0x55, command, 0xa5, 0x0b, 0x2e, 1, 1, 1, 0, 0}, reply{};
    DWORD n = 0;
    if (!transfer(file.get(), stop, true, packet, n, 300) || n != packet.size())
        return {};
    auto deadline = now() + 500;
    for (auto tick = now(); tick < deadline; tick = now()) {
        if (!transfer(file.get(), stop, false, reply, n, static_cast<DWORD>(deadline - tick)))
            break;
        if (n == 65 && reply[0] == 0 && header(std::span(reply).subspan(1), command))
            return {reply.begin() + 1, reply.end()};
    }
    return {};
}
void LangtuMouse::poll(const Device &mouse, const std::vector<Device> &controls, HANDLE stop,
                       DeviceStatus &d) {
    constexpr auto source = L"LANGTU M8 read query";
    d.power = BatteryKind::Present;
    d.connection = d.battery = d.dpi = Reading::unavailable(State::Unavailable, source);
    auto control = std::find_if(controls.begin(), controls.end(), [&](const Device &c) {
        return supportedLangtu(c) && c.root == mouse.root && c.usagePage == 0xff01 && c.usage == 0x10 &&
               c.inputLength == 65 && c.outputLength == 65;
    });
    if (!supportedLangtu(mouse) || control == controls.end())
        return;
    auto dpi = decodeLangtuDpi(query(*control, stop, 0x0e));
    if (!dpi) {
        batteryAt_ = 0;
        battery_ = Reading::unavailable(State::Unavailable, source);
        return;
    }
    d.dpi = Reading::valid(std::to_wstring(*dpi), source);
    d.connection = Reading::valid(L"已连接 · 2.4G", source);
    if (!batteryAt_ || now() - batteryAt_ >= 60000) {
        auto percent = decodeLangtuBattery(query(*control, stop, 0x30));
        battery_ = percent ? Reading::valid(std::to_wstring(*percent) + L"%", source, 180000, *percent <= 15)
                           : Reading::unavailable(State::Unavailable, source);
        batteryAt_ = now();
    }
    d.battery = battery_;
}
} // namespace dp
