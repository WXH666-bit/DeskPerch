#pragma once
#include "model.h"
namespace dp {
bool supportedLangtu(const Device &);
std::optional<unsigned> decodeLangtuBattery(std::span<const uint8_t>);
std::optional<unsigned> decodeLangtuDpi(std::span<const uint8_t>);
class LangtuMouse {
    Reading battery_;
    uint64_t batteryAt_ = 0;

  public:
    void poll(const Device &, const std::vector<Device> &, HANDLE stop, DeviceStatus &);
};
} // namespace dp
