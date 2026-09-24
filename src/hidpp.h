#pragma once
#include "model.h"
namespace dp {
enum class ReplyKind { Ignore, Success, Error };
ReplyKind classifyReply(std::span<const uint8_t>, uint8_t device, uint8_t feature, uint8_t function);
struct DpiValue {
    unsigned x = 0, y = 0;
};
std::optional<DpiValue> decodeDpi(std::span<const uint8_t> payload, bool extended, bool separateY);
struct BatterySample {
    unsigned percent = 0, level = 0, status = 0;
    uint64_t sampled = 0;
};
std::optional<BatterySample> decodeHidppBattery(std::span<const uint8_t>, uint16_t featureId,
                                                uint64_t sampled);
class BatteryGuard {
    std::optional<BatterySample> trusted_, candidate_;
    uint64_t candidateAt_ = 0;
    unsigned candidateCount_ = 0;
    bool pending_ = false, offlineAfterPending_ = false, offlineBeforeSample_ = false;

  public:
    void offline();
    Reading observe(const BatterySample &, bool protect);
};
struct HidReply {
    bool ok = false;
    unsigned error = 0;
    std::vector<uint8_t> data;
};
class HidChannel {
    Handle file_, short_, cancel_;
    unsigned length_ = 20;
    bool transfer(bool write, std::span<uint8_t> buffer, DWORD &n, DWORD timeout);
    bool receive(std::span<uint8_t> buffer, DWORD &n, DWORD timeout);

  public:
    HidChannel() = default;
    bool open(const Device &, HANDLE stop, const Device *companion = nullptr);
    bool opened() const { return static_cast<bool>(file_); }
    HidReply request(uint8_t device, uint8_t feature, uint8_t function, std::span<const uint8_t> args = {},
                     DWORD timeout = 300);
    void close() {
        file_.reset();
        short_.reset();
    }
};
class LogitechMouse {
    std::shared_ptr<HidChannel> channel_ = std::make_shared<HidChannel>();
    Device control_;
    uint8_t slot_ = 0, nameFeature_ = 0, batteryFeature_ = 0, dpiFeature_ = 0;
    uint16_t batteryId_ = 0;
    bool extended_ = false, separateY_ = false, batteryPercent_ = false;
    std::wstring name_;
    uint64_t batteryRead_ = 0;
    Reading battery_;
    BatteryGuard batteryGuard_;
    bool protectBattery_ = false;
    State dpiCapability_ = State::Unavailable, batteryCapability_ = State::Unavailable;
    std::optional<uint8_t> feature(uint16_t id);
    void discoverCapabilities();

  public:
    bool connect(const std::vector<Device> &controls, const std::wstring &root, HANDLE stop,
                 unsigned requestedSlot = 0, std::shared_ptr<HidChannel> shared = {});
    bool poll(Reading &battery, Reading &dpi, bool refreshBattery);
    const std::wstring &name() const { return name_; }
    const std::wstring &root() const { return control_.root; }
    unsigned slot() const { return slot_; }
};
} // namespace dp
