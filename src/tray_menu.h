#pragma once
#include "common.h"
#include <functional>
namespace dp {
// TrackPopupMenu runs a nested message loop. A second tray callback must not
// replace the actions belonging to the popup that is still being displayed.
class TrayMenuActions {
    bool open_ = false;
    std::vector<std::function<void()>> actions_;

  public:
    bool begin() {
        if (open_)
            return false;
        open_ = true;
        actions_.clear();
        return true;
    }
    UINT add(std::function<void()> action) {
        actions_.push_back(std::move(action));
        return 1000 + static_cast<UINT>(actions_.size() - 1);
    }
    std::function<void()> finish(UINT selected) {
        std::function<void()> action;
        if (open_ && selected >= 1000 && selected - 1000 < actions_.size())
            action = std::move(actions_[selected - 1000]);
        actions_.clear();
        open_ = false;
        return action;
    }
};
inline bool trayContextEvent(bool version4, LPARAM message) {
    return version4 ? LOWORD(message) == WM_CONTEXTMENU : message == WM_RBUTTONUP;
}
} // namespace dp
