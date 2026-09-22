#include "desktop.h"
#include "devices.h"
#include "render.h"
#include "settings.h"
#include "tray_menu.h"
#include <shellapi.h>
#include <dbt.h>
#include <wtsapi32.h>
#include <windowsx.h>
#include <functional>
#include <fstream>

namespace dp {
constexpr UINT trayMessage = WM_APP + 2;
enum Command : UINT {
    Startup = 101,
    Compact,
    Lock,
    Visibility,
    Reset,
    Exit,
    BackgroundClear = 111,
    BackgroundWhite,
    ShowMenu
};
class App {
    HINSTANCE instance_;
    HWND owner_ = nullptr, tile_ = nullptr;
    DesktopHost desktop_;
    Settings settings_ = loadSettings();
    Snapshot snapshot_;
    std::optional<CardContent> drawn_;
    std::unique_ptr<DeviceService> devices_;
    NOTIFYICONDATAW tray_{};
    HICON icon_ = nullptr;
    HDEVNOTIFY notification_ = nullptr;
    UINT taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    bool sessionLocked_ = false, suspended_ = false, quitting_ = false, dragging_ = false, trayAdded_ = false;
    POINT dragCursor_{}, dragOrigin_{};
    UINT dpi_ = 96;
    unsigned retrySeconds_ = 1;
    int scroll_ = 0;
    CardContent content(std::optional<POINT> position = {}) {
        auto c = contentFor(snapshot_, settings_);
        auto mon = monitorForPoint(
            position.value_or(tile_ ? tilePosition() : restorePosition(settings_, {256, 48})));
        c.maxWidth = std::max(1, std::min(420, MulDiv(mon.work.right - mon.work.left, 96, mon.dpi)));
        c.maxHeight = std::max(32, std::min(600, MulDiv(mon.work.bottom - mon.work.top, 96, mon.dpi)));
        scroll_ =
            std::clamp(scroll_, 0, std::max(0, static_cast<int>(c.rows.size()) * 32 + 16 - c.maxHeight));
        c.scroll = scroll_;
        return c;
    }
    TrayMenuActions menuActions_;
    bool trayVersion4_ = false;
    bool active() const {
        return settings_.visible && !sessionLocked_ && !suspended_ && desktop_.valid() && tile_;
    }
    void sync(bool rescan = false) {
        if (devices_)
            devices_->configure(settings_, active(), rescan);
    }
    void persist() {
        if (!saveSettings(settings_))
            tooltip(L"DeskPerch · 设置保存失败");
    }
    void tooltip(std::wstring text) {
        if (text == tray_.szTip)
            return;
        wcsncpy_s(tray_.szTip, text.c_str(), _TRUNCATE);
        if (trayAdded_) {
            tray_.uFlags = NIF_TIP;
            Shell_NotifyIconW(NIM_MODIFY, &tray_);
        }
    }
    void addTray() {
        tray_ = {};
        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = owner_;
        tray_.uID = 1;
        tray_.uCallbackMessage = trayMessage;
        tray_.hIcon = icon_;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        wcscpy_s(tray_.szTip, L"DeskPerch");
        trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
        if (trayAdded_) {
            tray_.uVersion = NOTIFYICON_VERSION_4;
            trayVersion4_ = Shell_NotifyIconW(NIM_SETVERSION, &tray_) != FALSE;
        }
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->message(h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    void createTile() {
        if (quitting_ || !settings_.visible || sessionLocked_ || suspended_)
            return;
        if (tile_ && desktop_.valid() && GetParent(tile_) == desktop_.window())
            return;
        if (tile_) {
            DestroyWindow(tile_);
            tile_ = nullptr;
        }
        if (!desktop_.discover()) {
            tooltip(L"DeskPerch · 桌面挂载暂不可用，正在重试");
            SetTimer(owner_, 2, retrySeconds_ * 1000, nullptr);
            retrySeconds_ = std::min(30u, retrySeconds_ * 2);
            return;
        }
        auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(desktop_.window()));
        tile_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"DeskPerch.Card", L"",
                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, desktop_.window(), nullptr, instance_,
                                this);
        if (previous)
            SetThreadDpiAwarenessContext(previous);
        if (!tile_ || !desktop_.attach(tile_)) {
            if (tile_)
                DestroyWindow(tile_);
            tile_ = nullptr;
            tooltip(L"DeskPerch · 桌面挂载暂不可用");
            SetTimer(owner_, 2, retrySeconds_ * 1000, nullptr);
            retrySeconds_ = std::min(30u, retrySeconds_ * 2);
            return;
        }
        KillTimer(owner_, 2);
        retrySeconds_ = 1;
        drawn_.reset();
        reposition(false);
        ShowWindow(tile_, SW_SHOWNOACTIVATE);
        sync(true);
    }
    POINT tilePosition() const {
        RECT r{};
        GetWindowRect(tile_, &r);
        return {r.left, r.top};
    }
    void reposition(bool reset) {
        if (!tile_)
            return;
        auto approx = restorePosition(settings_, {256, 48}, reset);
        auto content = this->content(approx);
        dpi_ = monitorForPoint(approx).dpi;
        auto size = cardSize(content, dpi_);
        POINT pos = restorePosition(settings_, size, reset);
        dpi_ = monitorForPoint(pos).dpi;
        drawCard(tile_, content, pos, dpi_);
        drawn_ = content;
        // Temporary loading/error text can be wider than the final card. Never
        // replace the user's anchor with the resulting temporary screen clamp.
        if (reset || !settings_.positioned) {
            RECT r{};
            GetWindowRect(tile_, &r);
            rememberPosition(settings_, r);
        }
    }
    void redraw(bool force = false) {
        if (!active() || dragging_)
            return;
        auto content = this->content();
        if (!force && drawn_ && *drawn_ == content)
            return;
        auto preferred = restorePosition(settings_, {0, 0});
        dpi_ = monitorForPoint(preferred).dpi;
        content = this->content(preferred);
        auto size = cardSize(content, dpi_);
        auto pos = restorePosition(settings_, size);
        drawCard(tile_, content, pos, dpi_);
        drawn_ = content;
    }
    void settingsChanged(bool rescan = false) {
        persist();
        sync(rescan);
        redraw(true);
    }
    void command(UINT id) {
        switch (id) {
        case ShowMenu:
            menu();
            break;
        case BackgroundClear:
        case BackgroundWhite:
            settings_.background = static_cast<int>(id - BackgroundClear) + 1;
            settingsChanged();
            break;
        case Startup:
            if (!setStartup(!startupEnabled()))
                tooltip(L"DeskPerch · 自启动设置失败");
            break;
        case Compact:
            settings_.compact = !settings_.compact;
            settingsChanged();
            break;
        case Lock:
            settings_.locked = !settings_.locked;
            if (dragging_) {
                ReleaseCapture();
                dragging_ = false;
            }
            settingsChanged();
            break;
        case Visibility:
            settings_.visible = !settings_.visible;
            if (settings_.visible) {
                for (auto &[deviceId, d] : snapshot_.devices)
                    d.battery = d.dpi = d.connection = Reading::unavailable();
                createTile();
                if (tile_)
                    ShowWindow(tile_, SW_SHOWNOACTIVATE);
            } else if (tile_)
                ShowWindow(tile_, SW_HIDE);
            settingsChanged(true);
            break;
        case Reset:
            if (!tile_) {
                POINT cursor{};
                GetCursorPos(&cursor);
                settings_.positioned = false;
                settings_.monitor = monitorForPoint(cursor).name;
            }
            reposition(true);
            settingsChanged();
            break;
        case Exit:
            quitting_ = true;
            DestroyWindow(owner_);
            break;
        default:
            break;
        }
    }
    static std::wstring menuText(std::wstring s) {
        std::wstring r;
        for (auto c : s) {
            r += c;
            if (c == L'&')
                r += L'&';
        }
        return r;
    }
    void item(HMENU m, std::wstring name, bool checked, std::function<void()> action, bool radio = false) {
        UINT id = menuActions_.add(std::move(action));
        auto label = menuText(std::move(name));
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), id, label.c_str());
        if (radio) {
            MENUITEMINFOW info{sizeof(info)};
            info.fMask = MIIM_FTYPE;
            info.fType = MFT_RADIOCHECK;
            SetMenuItemInfoW(m, id, FALSE, &info);
        }
    }
    void deviceMenu(HMENU m, DeviceKind kind) {
        auto candidates = snapshot_.devices;
        for (const auto &d : visibleDevices(snapshot_, settings_, false))
            if (!candidates.contains(d.id))
                candidates[d.id] = d;
        Snapshot directory = snapshot_;
        directory.devices = candidates;
        Settings all;
        for (const auto &[id, d] : candidates)
            all.selected[id] = {d.kind, d.name};
        for (const auto &d : visibleDevices(directory, all, false))
            candidates[d.id].name = d.name;
        unsigned count = 0;
        for (const auto &[id, d] : candidates) {
            if (d.kind != kind)
                continue;
            ++count;
            item(m, d.name + L" · " + d.connection.text(now()), settings_.selected.contains(id),
                 [this, id, d] {
                     if (!settings_.selected.erase(id))
                         settings_.selected[id] = {
                             d.kind, snapshot_.devices.contains(id) ? snapshot_.devices.at(id).name : d.name};
                     scroll_ = 0;
                 });
        }
        if (!count)
            AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"未发现可选择的外接设备");
    }
    void menu() {
        if (!menuActions_.begin())
            return;
        HMENU root = CreatePopupMenu();
        AppendMenuW(root, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), Startup, L"开机自启动");
        AppendMenuW(root, MF_STRING | (settings_.compact ? MF_CHECKED : 0), Compact, L"简约显示");
        AppendMenuW(root, MF_STRING | (settings_.locked ? MF_CHECKED : 0), Lock, L"锁定位置");
        HMENU background = CreatePopupMenu();
        const wchar_t *backgroundNames[] = {L"透明", L"浅白"};
        for (UINT i = 0; i < 2; ++i)
            AppendMenuW(background, MF_STRING, BackgroundClear + i, backgroundNames[i]);
        CheckMenuRadioItem(background, BackgroundClear, BackgroundWhite,
                           BackgroundClear + settings_.background - 1, MF_BYCOMMAND);
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(background), L"容器背景");
        AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
        HMENU devices = CreatePopupMenu(), mouse = CreatePopupMenu(), keyboard = CreatePopupMenu(),
              displays = CreatePopupMenu(), usb = CreatePopupMenu();
        item(devices, L"自动显示已连接设备", settings_.selected.empty(), [this] {
            settings_.selected.clear();
            scroll_ = 0;
        });
        AppendMenuW(devices, MF_SEPARATOR, 0, nullptr);
        deviceMenu(mouse, DeviceKind::Mouse);
        deviceMenu(keyboard, DeviceKind::Keyboard);
        deviceMenu(displays, DeviceKind::Display);
        deviceMenu(usb, DeviceKind::Port);
        AppendMenuW(devices, MF_POPUP, reinterpret_cast<UINT_PTR>(mouse), L"鼠标");
        AppendMenuW(devices, MF_POPUP, reinterpret_cast<UINT_PTR>(keyboard), L"键盘");
        AppendMenuW(devices, MF_POPUP, reinterpret_cast<UINT_PTR>(displays), L"显示器");
        AppendMenuW(devices, MF_POPUP, reinterpret_cast<UINT_PTR>(usb), L"USB 接口");
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(devices), L"关注的设备 / 接口");
        AppendMenuW(root, MF_STRING, Visibility, settings_.visible ? L"隐藏挂件" : L"显示挂件");
        AppendMenuW(root, MF_STRING, Reset, L"重置位置");
        AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(root, MF_STRING, Exit, L"退出");
        POINT p{};
        GetCursorPos(&p);
        SetForegroundWindow(owner_);
        UINT selected =
            TrackPopupMenuEx(root, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, owner_, nullptr);
        PostMessageW(owner_, WM_NULL, 0, 0);
        DestroyMenu(root);
        auto action = menuActions_.finish(selected);
        if (action) {
            action();
            settingsChanged(true);
        } else if (selected && selected < 1000)
            command(selected);
    }
    LRESULT message(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (h == tile_) {
            switch (m) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(h, &ps);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_MOUSEWHEEL:
                scroll_ -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 96;
                redraw(true);
                return 0;
            case WM_LBUTTONDOWN:
                if (drawn_ && GET_X_LPARAM(l) >= cardSize(*drawn_, dpi_).cx - MulDiv(12, dpi_, 96)) {
                    scroll_ += GET_Y_LPARAM(l) < cardSize(*drawn_, dpi_).cy / 2 ? -160 : 160;
                    redraw(true);
                    return 0;
                }
                if (!settings_.locked) {
                    dragging_ = true;
                    GetCursorPos(&dragCursor_);
                    dragOrigin_ = tilePosition();
                    SetCapture(h);
                }
                return 0;
            case WM_MOUSEMOVE:
                if (dragging_) {
                    POINT cursor{};
                    GetCursorPos(&cursor);
                    auto p = POINT{dragOrigin_.x + cursor.x - dragCursor_.x,
                                   dragOrigin_.y + cursor.y - dragCursor_.y};
                    auto mon = monitorForPoint(cursor);
                    dpi_ = mon.dpi;
                    auto content = this->content(cursor);
                    auto size = cardSize(content, dpi_);
                    auto rect = clampRect({p.x, p.y, p.x + size.cx, p.y + size.cy}, mon.work);
                    drawCard(tile_, content, {rect.left, rect.top}, dpi_);
                    drawn_ = content;
                }
                return 0;
            case WM_LBUTTONUP:
                if (dragging_) {
                    dragging_ = false;
                    ReleaseCapture();
                    RECT r{};
                    GetWindowRect(h, &r);
                    rememberPosition(settings_, r);
                    persist();
                }
                return 0;
            case WM_CAPTURECHANGED:
                dragging_ = false;
                return 0;
            case WM_DPICHANGED:
            case WM_DPICHANGED_AFTERPARENT:
                redraw(true);
                return 0;
            case WM_NCDESTROY:
                tile_ = nullptr;
                drawn_.reset();
                if (!quitting_)
                    PostMessageW(owner_, WM_APP + 3, 0, 0);
                break;
            }
            return DefWindowProcW(h, m, w, l);
        }
        if (m == taskbarCreated_) {
            trayAdded_ = false;
            addTray();
            createTile();
            sync(true);
            return 0;
        }
        switch (m) {
        case WM_COPYDATA: {
            auto data = reinterpret_cast<const COPYDATASTRUCT *>(l);
            if (!data || data->dwData != 0x44504350 || data->cbData < sizeof(wchar_t) ||
                data->cbData > 65536 || data->cbData % sizeof(wchar_t))
                return FALSE;
            auto path = static_cast<const wchar_t *>(data->lpData);
            if (!path || path[data->cbData / sizeof(wchar_t) - 1] != L'\0')
                return FALSE;
            std::filesystem::path output(path);
            if (!output.is_absolute())
                return FALSE;
            if (!drawCard(nullptr, content(), {}, dpi_, output))
                return FALSE;
            output.replace_extension(L"txt");
            std::ofstream report(output);
            for (const auto &[id, d] : snapshot_.devices)
                report << utf8(d.name) << " | " << utf8(d.connection.text(now())) << " | "
                       << utf8(batteryText(d, now())) << " | " << utf8(d.dpi.text(now())) << "\n";
            return TRUE;
        }
        case trayMessage:
            if (trayContextEvent(trayVersion4_, l))
                menu();
            return 0;
        case DeviceService::updatedMessage: {
            snapshot_ = devices_->snapshot();
            bool namesChanged = false;
            for (auto &[key, choice] : settings_.selected) {
                auto found = snapshot_.devices.find(key);
                if (found != snapshot_.devices.end() && choice.name != found->second.name) {
                    choice.name = found->second.name;
                    namesChanged = true;
                }
            }
            if (namesChanged)
                persist();
            tooltip(L"DeskPerch");
            redraw();
            return 0;
        }
        case WM_COMMAND:
            command(LOWORD(w));
            return 0;
        case WM_TIMER:
            if (w == 3) {
                KillTimer(owner_, 3);
                sync(true);
            } else {
                if (!tile_ || !desktop_.valid())
                    createTile();
                redraw();
            }
            return 0;
        case WM_APP + 3:
            createTile();
            sync(true);
            return 0;
        case WM_DEVICECHANGE:
            if (w == DBT_DEVNODES_CHANGED || w == DBT_DEVICEARRIVAL || w == DBT_DEVICEREMOVECOMPLETE)
                SetTimer(owner_, 3, 250, nullptr);
            return TRUE;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (tile_ && !dragging_) {
                reposition(false);
                persist();
            }
            sync(true);
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (w == WTS_SESSION_LOCK || w == WTS_SESSION_UNLOCK) {
                sessionLocked_ = w == WTS_SESSION_LOCK;
                if (sessionLocked_) {
                    if (tile_)
                        ShowWindow(tile_, SW_HIDE);
                } else {
                    for (auto &[deviceId, d] : snapshot_.devices)
                        d.battery = d.dpi = d.connection = Reading::unavailable();
                    createTile();
                    if (tile_ && settings_.visible)
                        ShowWindow(tile_, SW_SHOWNOACTIVATE);
                }
                sync(true);
                redraw(true);
            }
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMSUSPEND) {
                suspended_ = true;
                sync();
            } else if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) {
                suspended_ = false;
                for (auto &[deviceId, d] : snapshot_.devices)
                    d.battery = d.dpi = d.connection = Reading::unavailable();
                createTile();
                sync(true);
                redraw(true);
            }
            return TRUE;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (w) {
                quitting_ = true;
                DestroyWindow(owner_);
            }
            return 0;
        case WM_CLOSE:
            quitting_ = true;
            DestroyWindow(owner_);
            return 0;
        case WM_DESTROY:
            quitting_ = true;
            if (devices_)
                devices_->stop();
            if (tile_)
                DestroyWindow(tile_);
            if (trayAdded_)
                Shell_NotifyIconW(NIM_DELETE, &tray_);
            trayAdded_ = false;
            if (notification_)
                UnregisterDeviceNotification(notification_);
            WTSUnRegisterSessionNotification(owner_);
            persist();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

  public:
    explicit App(HINSTANCE i) : instance_(i) {}
    ~App() {
        if (icon_)
            DestroyIcon(icon_);
    }
    int run() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"DeskPerch.Owner";
        RegisterClassExW(&wc);
        wc.lpszClassName = L"DeskPerch.Card";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"DeskPerch.Owner", L"DeskPerch", WS_POPUP, 0, 0, 0, 0,
                                 nullptr, nullptr, instance_, this);
        if (!owner_)
            return 2;
        icon_ = trayIcon();
        addTray();
        WTSRegisterSessionNotification(owner_, NOTIFY_FOR_THIS_SESSION);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        notification_ = RegisterDeviceNotificationW(
            owner_, &filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
        devices_ = std::make_unique<DeviceService>(owner_, settings_);
        createTile();
        sync(true);
        devices_->start();
        SetTimer(owner_, 1, 5000, nullptr);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        devices_.reset();
        return static_cast<int>(msg.wParam);
    }
};
} // namespace dp
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace dp;
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && std::wstring_view(argv[1]) == L"--capture") {
        auto path = std::filesystem::absolute(argv[2]).wstring();
        LocalFree(argv);
        auto owner = FindWindowW(L"DeskPerch.Owner", nullptr);
        if (!owner)
            return 3;
        COPYDATASTRUCT data{0x44504350, static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)), path.data()};
        DWORD_PTR result = 0;
        return SendMessageTimeoutW(owner, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG,
                                   5000, &result) &&
                       result
                   ? 0
                   : 5;
    }
    // Deterministic command interface for lifecycle/benchmark tools; never activates the UI.
    if (argc == 3 && std::wstring_view(argv[1]) == L"--control") {
        std::wstring action = argv[2];
        LocalFree(argv);
        auto owner = FindWindowW(L"DeskPerch.Owner", nullptr);
        if (!owner)
            return 3;
        UINT cmd = action == L"background-clear"   ? BackgroundClear
                   : action == L"menu"             ? ShowMenu
                   : action == L"background-white" ? BackgroundWhite
                   : action == L"exit"             ? Exit
                   : action == L"compact"          ? Compact
                   : action == L"visibility"       ? Visibility
                   : action == L"lock"             ? Lock
                   : action == L"reset"            ? Reset
                                                   : 0;
        if (!cmd)
            return 4;
        DWORD_PTR result = 0;
        return SendMessageTimeoutW(owner, WM_COMMAND, cmd, 0, SMTO_ABORTIFHUNG, 5000, &result) ? 0 : 5;
    }
    LocalFree(argv);
    Handle mutex(CreateMutexW(nullptr, TRUE, L"Local\\DeskPerch.UserSession.v1"));
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        return 1;
    int result = 0;
    {
        App app(instance);
        result = app.run();
    }
    Gdiplus::GdiplusShutdown(token);
    return result;
}
