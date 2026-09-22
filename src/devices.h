#pragma once
#include "hidpp.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace dp {
struct Inventory {
    std::vector<Device> mice, keyboards, controls;
    std::set<std::wstring> excluded;
    bool ok = false;
};
Inventory enumerateInputs();
std::vector<Display> enumerateDisplays(bool &ok, std::set<std::wstring> *excluded = nullptr);
std::vector<Port> enumeratePorts(bool &ok);
Reading standardBattery(const Device &d);
class DeviceService {
    HWND target_ = nullptr;
    Handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)},
        wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::thread worker_;
    mutable std::mutex mutex_;
    Settings config_;
    Snapshot latest_;
    bool active_ = true, rescan_ = true, refresh_ = true;
    void run();

  public:
    static constexpr UINT updatedMessage = WM_APP + 1;
    DeviceService(HWND target, const Settings &s) : target_(target), config_(s) {}
    ~DeviceService() { stop(); }
    void start() {
        worker_ = std::thread([this] { run(); });
    }
    void stop();
    void configure(const Settings &, bool active, bool rescan = false);
    Snapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return latest_;
    }
};
} // namespace dp
