#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace dp {
class Handle {
    HANDLE h_ = INVALID_HANDLE_VALUE;

  public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&x) noexcept : h_(std::exchange(x.h_, INVALID_HANDLE_VALUE)) {}
    Handle &operator=(Handle &&x) noexcept {
        if (this != &x) {
            reset();
            h_ = std::exchange(x.h_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) {
        if (h_ && h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
        h_ = h;
    }
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
};
inline uint64_t now() {
    return GetTickCount64();
}
std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);
std::wstring lower(std::wstring text);
std::wstring exePath();
std::wstring errorText(DWORD code);
} // namespace dp
