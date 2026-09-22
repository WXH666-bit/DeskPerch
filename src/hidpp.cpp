#include "hidpp.h"
#include <hidsdi.h>
#include <winioctl.h>
#include <hidclass.h>
namespace dp {
ReplyKind classifyReply(std::span<const uint8_t> b, uint8_t d, uint8_t f, uint8_t fn) {
    if (b.size() < 7 || (b[0] != 0x10 && b[0] != 0x11) || (b[0] == 0x11 && b.size() < 20) || b[1] != d)
        return ReplyKind::Ignore;
    if ((b[2] == 0xff || b[2] == 0x8f) && b[3] == f && b[4] == fn)
        return ReplyKind::Error;
    return b[2] == f && b[3] == fn ? ReplyKind::Success : ReplyKind::Ignore;
}
std::optional<DpiValue> decodeDpi(std::span<const uint8_t> p, bool extended, bool separateY) {
    if (p.size() < 3 || p[0] != 0)
        return {};
    unsigned x = (p[1] << 8) | p[2], y = x;
    if (extended && separateY) {
        if (p.size() < 7)
            return {};
        y = (p[5] << 8) | p[6];
    }
    // A zero is not a measured DPI. Do not substitute pointer speed or a preset.
    if (!x || !y)
        return {};
    return DpiValue{x, y};
}
bool HidChannel::open(const Device &d, HANDLE stop, const Device *companion) {
    file_.reset(CreateFileW(d.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!file_)
        return false;
    length_ = d.outputLength;
    if (length_ != 7 && length_ != 20) {
        file_.reset();
        return false;
    }
    short_.reset();
    if (companion)
        short_.reset(CreateFileW(companion->path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED, nullptr));
    HANDLE c = nullptr;
    if (stop)
        DuplicateHandle(GetCurrentProcess(), stop, GetCurrentProcess(), &c, SYNCHRONIZE, FALSE, 0);
    cancel_.reset(c);
    return true;
}
bool HidChannel::receive(std::span<uint8_t> buffer, DWORD &n, DWORD timeout) {
    if (!short_)
        return transfer(false, buffer.first(length_), n, timeout);
    // Windows routes 7-byte and 20-byte HID++ replies to different collections.
    // Keep both reads pending so a short reply to a long request is not lost.
    Handle e1(CreateEventW(nullptr, TRUE, FALSE, nullptr)), e2(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED o1{}, o2{};
    o1.hEvent = e1.get();
    o2.hEvent = e2.get();
    std::array<uint8_t, 20> b1{};
    std::array<uint8_t, 7> b2{};
    DWORD n1 = 0, n2 = 0;
    BOOL a = ReadFile(file_.get(), b1.data(), 20, &n1, &o1);
    DWORD ea = a ? 0 : GetLastError();
    BOOL b = ReadFile(short_.get(), b2.data(), 7, &n2, &o2);
    DWORD eb = b ? 0 : GetLastError();
    if (a)
        SetEvent(e1.get());
    if (b)
        SetEvent(e2.get());
    HANDLE events[] = {e1.get(), e2.get(), cancel_.get()};
    DWORD result = (a || ea == ERROR_IO_PENDING) && (b || eb == ERROR_IO_PENDING)
                       ? WaitForMultipleObjects(cancel_ ? 3 : 2, events, FALSE, timeout)
                       : WAIT_FAILED;
    bool ok = false;
    if (result == WAIT_OBJECT_0) {
        ok = GetOverlappedResult(file_.get(), &o1, &n1, FALSE) != FALSE;
        if (ok) {
            n = n1;
            std::copy_n(b1.begin(), n, buffer.begin());
        }
    } else if (result == WAIT_OBJECT_0 + 1) {
        ok = GetOverlappedResult(short_.get(), &o2, &n2, FALSE) != FALSE;
        if (ok) {
            n = n2;
            std::copy_n(b2.begin(), n, buffer.begin());
        }
    }
    if (!a && ea == ERROR_IO_PENDING) {
        CancelIoEx(file_.get(), &o1);
        GetOverlappedResult(file_.get(), &o1, &n1, TRUE);
    }
    if (!b && eb == ERROR_IO_PENDING) {
        CancelIoEx(short_.get(), &o2);
        GetOverlappedResult(short_.get(), &o2, &n2, TRUE);
    }
    return ok;
}
bool HidChannel::transfer(bool write, std::span<uint8_t> buffer, DWORD &n, DWORD timeout) {
    if (!file_)
        return false;
    if (cancel_ && WaitForSingleObject(cancel_.get(), 0) == WAIT_OBJECT_0)
        return false;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED ov{};
    ov.hEvent = event.get();
    n = 0;
    BOOL done = write ? WriteFile(file_.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &n, &ov)
                      : ReadFile(file_.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &n, &ov);
    if (done)
        return true;
    if (GetLastError() != ERROR_IO_PENDING)
        return false;
    HANDLE waits[] = {event.get(), cancel_.get()};
    DWORD r = WaitForMultipleObjects(cancel_ ? 2 : 1, waits, FALSE, timeout);
    if (r != WAIT_OBJECT_0) {
        CancelIoEx(file_.get(), &ov);
        GetOverlappedResult(file_.get(), &ov, &n, TRUE);
        return false;
    }
    return GetOverlappedResult(file_.get(), &ov, &n, FALSE) != FALSE;
}
HidReply HidChannel::request(uint8_t d, uint8_t f, uint8_t function, std::span<const uint8_t> args,
                             DWORD timeout) {
    // Only vendor-defined HID control collections are opened by this class.
    // The software ID separates our read queries from G HUB/other HID++ clients.
    constexpr uint8_t softwareId = 0x0d;
    uint8_t fn = static_cast<uint8_t>((function << 4) | softwareId);
    std::vector<uint8_t> packet(length_);
    packet[0] = length_ == 20 ? 0x11 : 0x10;
    packet[1] = d;
    packet[2] = f;
    packet[3] = fn;
    if (args.size() > packet.size() - 4)
        return {};
    std::copy(args.begin(), args.end(), packet.begin() + 4);
    // Discard pending control notifications/responses before issuing this request.
    DWORD count = 0;
    std::array<uint8_t, 20> old{};
    for (int i = 0; i < 8; ++i)
        if (!receive(old, count, 1))
            break;
    if (cancel_ && WaitForSingleObject(cancel_.get(), 0) == WAIT_OBJECT_0)
        return {};
    // This receiver requires the documented control-report API, not interrupt WriteFile.
    // The worker's synchronous I/O is cancelled during shutdown; all replies have a deadline.
    if (!HidD_SetOutputReport(file_.get(), packet.data(), static_cast<ULONG>(packet.size())))
        return {false, 0x10000 | GetLastError(), {}};
    const auto deadline = now() + timeout;
    while (now() < deadline) {
        std::array<uint8_t, 20> b{};
        const auto tick = now();
        if (tick >= deadline || !receive(b, count, static_cast<DWORD>(deadline - tick)))
            break;
        auto kind = classifyReply(std::span(b).first(count), d, f, fn);
        if (kind == ReplyKind::Success)
            return {true, 0, std::vector<uint8_t>(b.begin() + 4, b.begin() + count)};
        if (kind == ReplyKind::Error)
            return {false, b[5], {}};
    }
    return {};
}
std::optional<uint8_t> LogitechMouse::feature(uint16_t id) {
    const uint8_t args[] = {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0};
    auto r = channel_.request(slot_, 0, 0, args);
    if (!r.ok || r.data.empty())
        return {};
    return r.data[0];
}
void LogitechMouse::discoverCapabilities() {
    if (dpiCapability_ == State::Unavailable) {
        auto ext = feature(0x2202);
        if (ext && *ext) {
            const uint8_t sensor = 0;
            auto caps = channel_.request(slot_, *ext, 1, std::span(&sensor, 1));
            if (caps.ok && caps.data.size() > 2) {
                dpiFeature_ = *ext;
                extended_ = true;
                separateY_ = (caps.data[2] & 1) != 0;
                dpiCapability_ = State::Valid;
            }
        } else {
            auto basic = feature(0x2201);
            if (basic && *basic) {
                dpiFeature_ = *basic;
                dpiCapability_ = State::Valid;
            } else if (ext && basic)
                dpiCapability_ = State::Unsupported;
        }
    }
    if (batteryCapability_ == State::Unavailable) {
        auto unified = feature(0x1004);
        if (unified && *unified) {
            auto caps = channel_.request(slot_, *unified, 0);
            if (caps.ok && caps.data.size() > 1) {
                batteryFeature_ = *unified;
                batteryId_ = 0x1004;
                batteryPercent_ = (caps.data[1] & 2) != 0;
                batteryCapability_ = batteryPercent_ ? State::Valid : State::Unsupported;
            }
        } else {
            auto legacy = feature(0x1000);
            if (legacy && *legacy) {
                batteryFeature_ = *legacy;
                batteryId_ = 0x1000;
                batteryPercent_ = true;
                batteryCapability_ = State::Valid;
            } else if (unified && legacy)
                batteryCapability_ = State::Unsupported;
        }
    }
}
bool LogitechMouse::connect(const std::vector<Device> &controls, const std::wstring &root, HANDLE stop) {
    name_.clear();
    batteryFeature_ = dpiFeature_ = nameFeature_ = 0;
    batteryRead_ = 0;
    batteryId_ = 0;
    extended_ = separateY_ = batteryPercent_ = false;
    dpiCapability_ = batteryCapability_ = State::Unavailable;
    for (auto &d : controls) {
        if (d.root != root || d.vendor != 0x046d || d.usagePage < 0xff00 || d.outputLength != 20)
            continue;
        auto shortIt = std::find_if(controls.begin(), controls.end(),
                                    [&](const Device &c) { return c.root == d.root && c.outputLength == 7; });
        if (!channel_.open(d, stop, shortIt == controls.end() ? nullptr : &*shortIt))
            continue;
        for (unsigned slot : {1u, 2u, 3u, 4u, 5u, 6u, 255u}) {
            if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
                return false;
            const uint8_t ping[] = {0, 0, 0x5a};
            auto r = channel_.request(static_cast<uint8_t>(slot), 0, 1, ping, slot == 1 ? 1200 : 300);
            if (!r.ok || r.data.size() < 3 || r.data[0] < 2 || r.data[2] != 0x5a)
                continue;
            slot_ = static_cast<uint8_t>(slot);
            auto nf = feature(0x0005);
            if (!nf || !*nf)
                continue;
            nameFeature_ = *nf;
            auto type = channel_.request(slot_, nameFeature_, 2);
            if (!type.ok || type.data.empty() || type.data[0] != 3)
                continue;
            auto len = channel_.request(slot_, nameFeature_, 0);
            std::string text;
            if (len.ok && !len.data.empty() && len.data[0] > 0 && len.data[0] <= 128) {
                unsigned length = len.data[0];
                while (text.size() < length) {
                    uint8_t at = static_cast<uint8_t>(text.size());
                    auto chunk = channel_.request(slot_, nameFeature_, 1, std::span(&at, 1));
                    if (!chunk.ok || chunk.data.empty())
                        break;
                    size_t n = std::min<size_t>(chunk.data.size(), length - text.size());
                    text.append(reinterpret_cast<const char *>(chunk.data.data()), n);
                }
                if (text.size() == length)
                    name_ = wide(text);
            }
            if (name_.empty())
                name_ = L"罗技鼠标";
            discoverCapabilities();
            control_ = d;
            return true;
        }
        channel_.close();
    }
    return false;
}
bool LogitechMouse::poll(Reading &battery, Reading &dpi, bool refreshBattery) {
    const uint8_t ping[] = {0, 0, 0xa6};
    auto live = channel_.request(slot_, 0, 1, ping, 1200);
    if (!live.ok || live.data.size() < 3 || live.data[2] != 0xa6) {
        battery_ = battery = Reading::unavailable(State::Unavailable, L"HID++ ping");
        dpi = Reading::unavailable(State::Unavailable, L"HID++ ping");
        batteryRead_ = 0;
        return false;
    }
    discoverCapabilities();
    if (dpiFeature_) {
        const uint8_t sensor = 0;
        auto r = channel_.request(slot_, dpiFeature_, extended_ ? 5 : 2, std::span(&sensor, 1));
        auto v = r.ok ? decodeDpi(r.data, extended_, separateY_) : std::nullopt;
        dpi =
            v ? Reading::valid(std::to_wstring(v->x) + (v->x == v->y ? L"" : L" × " + std::to_wstring(v->y)),
                               extended_ ? L"HID++ 0x2202" : L"HID++ 0x2201")
              : Reading::unavailable(State::Unavailable, L"HID++ DPI");
    } else
        dpi = Reading::unavailable(dpiCapability_, L"HID++ feature discovery");
    if (!batteryFeature_ || !batteryPercent_) {
        battery_ = Reading::unavailable(batteryCapability_, L"HID++ battery percentage");
    } else if (refreshBattery || !batteryRead_ || now() - batteryRead_ >= 60000) {
        auto r = channel_.request(slot_, batteryFeature_, batteryId_ == 0x1004 ? 1 : 0);
        batteryRead_ = now();
        if (r.ok && r.data.size() >= 3 && r.data[0] <= 100 && (batteryId_ == 0x1004 || r.data[0] != 0)) {
            auto v = std::to_wstring(r.data[0]) + L"%";
            if (r.data[2] == 1 || r.data[2] == 2)
                v += L" · 充电中";
            battery_ = Reading::valid(v, batteryId_ == 0x1004 ? L"HID++ 0x1004" : L"HID++ 0x1000", 180000,
                                      r.data[0] <= 15);
        } else
            battery_ = Reading::unavailable(State::Unavailable, L"HID++ battery");
    }
    battery = battery_;
    return true;
}
} // namespace dp
