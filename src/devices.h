#pragma once
#include "hidpp.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>
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
    struct RootContext {
        uint64_t epoch = 0, discovered = 0, batteryAt = 0;
        std::shared_ptr<HidChannel> channel;
        std::map<unsigned, std::unique_ptr<LogitechMouse>> mice;
        Reading battery;
    };
    struct Job {
        Device device;
        std::vector<Device> controls;
        uint64_t epoch = 0;
        std::shared_ptr<RootContext> context;
    };
    HWND target_ = nullptr;
    Handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)},
        wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::thread worker_;
    std::array<std::thread, 2> collectors_;
    std::condition_variable ready_;
    std::deque<Job> jobs_;
    std::set<std::wstring> pending_;
    uint64_t epoch_ = 1;
    mutable std::mutex mutex_;
    Settings config_;
    Snapshot latest_;
    bool active_ = true, rescan_ = true;
    void run();
    void collect();

  public:
    static constexpr UINT updatedMessage = WM_APP + 1;
    DeviceService(HWND target, const Settings &s) : target_(target), config_(s) {}
    ~DeviceService() { stop(); }
    void start() {
        for (auto &thread : collectors_)
            thread = std::thread([this] { collect(); });
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
