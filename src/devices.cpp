#include "devices.h"
#include <objbase.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <hidclass.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <bluetoothleapis.h>
#include <functional>

namespace dp {
struct DeviceSet {
    HDEVINFO h = INVALID_HANDLE_VALUE;
    ~DeviceSet() {
        if (h != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(h);
    }
};
std::wstring property(DEVINST dev, const DEVPROPKEY &key) {
    ULONG bytes = 0;
    DEVPROPTYPE type = 0;
    CM_Get_DevNode_PropertyW(dev, &key, &type, nullptr, &bytes, 0);
    if (bytes < sizeof(wchar_t) || bytes > 65536)
        return {};
    std::vector<BYTE> b(bytes + 2, 0);
    if (CM_Get_DevNode_PropertyW(dev, &key, &type, b.data(), &bytes, 0) != CR_SUCCESS)
        return {};
    if (type == DEVPROP_TYPE_STRING || type == DEVPROP_TYPE_STRING_LIST)
        return reinterpret_cast<const wchar_t *>(b.data());
    if (type == DEVPROP_TYPE_GUID && bytes == sizeof(GUID)) {
        wchar_t s[64]{};
        StringFromGUID2(*reinterpret_cast<GUID *>(b.data()), s, 64);
        return s;
    }
    return {};
}
std::wstring devId(DEVINST dev) {
    wchar_t id[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(dev, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        return {};
    return lower(id);
}
void identity(Device &d, DEVINST node) {
    ULONG flags = 0, problem = 0;
    d.operational = CM_Get_DevNode_Status(&flags, &problem, node, 0) == CR_SUCCESS && (flags & DN_STARTED) &&
                    !(flags & DN_HAS_PROBLEM);
    d.container = property(node, DEVPKEY_Device_ContainerId);
    d.id = devId(node);
    d.root = d.id;
    auto name = property(node, DEVPKEY_Device_FriendlyName);
    if (name.empty())
        name = property(node, DEVPKEY_Device_BusReportedDeviceDesc);
    if (!name.empty())
        d.name = name;
    for (int i = 0; i < 12; ++i) {
        auto id = devId(node);
        auto desc = property(node, DEVPKEY_Device_BusReportedDeviceDesc);
        if (id.starts_with(L"bth")) {
            d.bluetooth = true;
            d.transport = L"蓝牙";
        }
        if (id.starts_with(L"usb\\vid_") && id.find(L"&mi_") == std::wstring::npos) {
            d.root = id;
            if (!desc.empty())
                d.name = desc;
            d.container = property(node, DEVPKEY_Device_ContainerId);
            ULONG policy = 0, bytes = sizeof(policy);
            DEVPROPTYPE type = 0;
            if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_RemovalPolicy, &type,
                                         reinterpret_cast<BYTE *>(&policy), &bytes, 0) == CR_SUCCESS &&
                policy == CM_REMOVAL_POLICY_EXPECT_NO_REMOVAL) {
                d.internal = true;
                d.transport = L"内置";
            }
            break;
        }
        if (id.starts_with(L"acpi\\")) {
            d.internal = !d.bluetooth;
            d.root = id;
            if (d.transport.empty())
                d.transport = L"内置";
            break;
        }
        if (id.starts_with(L"bthle\\") || id.starts_with(L"bthenum\\dev_")) {
            d.root = id;
            break;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
            break;
        node = parent;
    }
    auto n = lower(d.name);
    d.receiver = n.find(L"receiver") != std::wstring::npos || n.find(L"接收器") != std::wstring::npos;
    if (d.receiver)
        d.transport = L"无线接收器";
    else if (d.transport.empty() && d.root.starts_with(L"usb\\"))
        d.transport = L"USB";
}
bool interfaces(const GUID &guid, const std::function<void(const std::wstring &, SP_DEVINFO_DATA &)> &fn) {
    DeviceSet set{SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (set.h == INVALID_HANDLE_VALUE)
        return false;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA face{sizeof(face)};
        if (!SetupDiEnumDeviceInterfaces(set.h, nullptr, &guid, i, &face))
            return GetLastError() == ERROR_NO_MORE_ITEMS;
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(set.h, &face, nullptr, 0, &bytes, nullptr);
        if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536)
            continue;
        std::vector<BYTE> b(bytes);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(b.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA info{sizeof(info)};
        if (SetupDiGetDeviceInterfaceDetailW(set.h, &face, detail, bytes, nullptr, &info))
            fn(detail->DevicePath, info);
    }
}
Inventory enumerateInputs() {
    Inventory r;
    GUID hid{};
    HidD_GetHidGuid(&hid);
    r.ok = interfaces(hid, [&](const std::wstring &path, SP_DEVINFO_DATA &info) {
        Handle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (!h)
            return;
        HIDD_ATTRIBUTES attr{sizeof(attr)};
        if (!HidD_GetAttributes(h.get(), &attr))
            return;
        PHIDP_PREPARSED_DATA prep = nullptr;
        if (!HidD_GetPreparsedData(h.get(), &prep))
            return;
        HIDP_CAPS caps{};
        auto status = HidP_GetCaps(prep, &caps);
        HidD_FreePreparsedData(prep);
        if (status != HIDP_STATUS_SUCCESS)
            return;
        Device d;
        d.path = path;
        d.vendor = attr.VendorID;
        d.product = attr.ProductID;
        d.usagePage = caps.UsagePage;
        d.usage = caps.Usage;
        d.inputLength = caps.InputReportByteLength;
        d.outputLength = caps.OutputReportByteLength;
        wchar_t name[256]{};
        if (HidD_GetProductString(h.get(), name, sizeof(name)))
            d.name = name;
        identity(d, info.DevInst);
        bool physical = d.root.starts_with(L"usb\\vid_") || d.root.starts_with(L"acpi\\") || d.bluetooth;
        if (!physical)
            return;
        if (caps.UsagePage >= 0xff00 && d.vendor == 0x046d)
            r.controls.push_back(d);
        if (caps.UsagePage != 1)
            return;
        d.mouse = caps.Usage == 2;
        d.keyboard = caps.Usage == 6;
        if (!d.mouse && !d.keyboard)
            return;
        if (d.mouse &&
            (d.root.starts_with(L"acpi\\") || lower(d.name).find(L"touchpad") != std::wstring::npos))
            return;
        d.id = d.root + (d.mouse ? L"/mouse" : L"/keyboard");
        if (d.name.empty())
            d.name = d.mouse ? L"鼠标" : L"键盘";
        auto &list = d.mouse ? r.mice : r.keyboards;
        if (std::none_of(list.begin(), list.end(), [&](auto &other) { return other.id == d.id; }))
            list.push_back(d);
    });
    // PS/2/internal keyboards do not necessarily expose a HID interface.
    const GUID keyboardClass = {0x4d36e96b, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    DeviceSet set{SetupDiGetClassDevsW(&keyboardClass, nullptr, nullptr, DIGCF_PRESENT)};
    if (set.h != INVALID_HANDLE_VALUE)
        for (DWORD i = 0;; ++i) {
            SP_DEVINFO_DATA info{sizeof(info)};
            if (!SetupDiEnumDeviceInfo(set.h, i, &info))
                break;
            auto id = devId(info.DevInst);
            if (!id.starts_with(L"acpi\\"))
                continue;
            Device d;
            d.keyboard = true;
            identity(d, info.DevInst);
            d.id = d.root + L"/keyboard";
            d.name = L"内置键盘";
            if (std::none_of(r.keyboards.begin(), r.keyboards.end(), [&](auto &k) { return k.id == d.id; }))
                r.keyboards.push_back(d);
        }
    auto excludeInternal = [&](const Device &d) {
        if (!d.internal)
            return false;
        r.excluded.insert(d.id);
        return true;
    };
    std::erase_if(r.mice, excludeInternal);
    std::erase_if(r.keyboards, excludeInternal);
    std::erase_if(r.controls, [](const Device &d) { return d.internal; });
    // A gaming mouse exposes keyboard usages for its buttons, not a second keyboard.
    std::erase_if(r.keyboards, [&](const Device &k) {
        // Shared receivers can contain a real keyboard. Suppress only the
        // verified mouse receiver or a wired mouse's extra keyboard collection.
        if (k.receiver && !(k.vendor == 0x046d && k.product == 0xc547))
            return false;
        return std::any_of(r.mice.begin(), r.mice.end(), [&](const Device &m) { return m.root == k.root; });
    });
    auto order = [](const Device &a, const Device &b) {
        return a.operational != b.operational ? a.operational : a.id < b.id;
    };
    std::sort(r.mice.begin(), r.mice.end(), order);
    std::sort(r.keyboards.begin(), r.keyboards.end(), order);
    auto keyboardRank = [](const Device &d) {
        auto n = lower(d.name);
        if (d.transport == L"USB" &&
            (n.find(L"keyboard") != std::wstring::npos || n.find(L"键盘") != std::wstring::npos))
            return 0;
        if (d.transport == L"内置")
            return 1;
        return 2;
    };
    std::stable_sort(r.keyboards.begin(), r.keyboards.end(),
                     [&](const Device &a, const Device &b) { return keyboardRank(a) < keyboardRank(b); });
    return r;
}
std::vector<Display> enumerateDisplays(bool &ok, std::set<std::wstring> *excluded) {
    ok = false;
    std::vector<Display> result;
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 pc = 0, mc = 0;
        constexpr UINT32 flags = QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE;
        if (GetDisplayConfigBufferSizes(flags, &pc, &mc) != ERROR_SUCCESS || pc > 4096 || mc > 4096)
            return result;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        auto status = QueryDisplayConfig(flags, &pc, paths.data(), &mc, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (status != ERROR_SUCCESS)
            return result;
        for (UINT32 i = 0; i < pc; ++i) {
            auto &p = paths[i];
            DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
            name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            name.header.size = sizeof(name);
            name.header.adapterId = p.targetInfo.adapterId;
            name.header.id = p.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || !name.monitorDevicePath[0])
                continue;
            auto id = lower(name.monitorDevicePath);
            if (internalDisplayTechnology(p.targetInfo.outputTechnology) ||
                internalDisplayTechnology(name.outputTechnology)) {
                if (excluded)
                    excluded->insert(id);
                continue;
            }
            auto it = std::find_if(result.begin(), result.end(), [&](auto &d) { return d.id == id; });
            bool connected = p.targetInfo.targetAvailable != FALSE;
            bool active = (p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
            if (it == result.end()) {
                std::wstring label = name.monitorFriendlyDeviceName;
                if (label.empty()) {
                    auto instance = id;
                    if (instance.starts_with(L"\\\\?\\"))
                        instance.erase(0, 4);
                    auto guid = instance.find(L"#{");
                    if (guid != std::wstring::npos)
                        instance.resize(guid);
                    std::replace(instance.begin(), instance.end(), L'#', L'\\');
                    DEVINST node = 0;
                    if (CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
                        label = property(node, DEVPKEY_Device_FriendlyName);
                        if (label.empty())
                            label = property(node, DEVPKEY_Device_DeviceDesc);
                    }
                }
                result.push_back({id, label.empty() ? L"显示器" : label, connected, active});
            } else {
                it->connected |= connected;
                it->enabled |= active;
            }
        }
        ok = true;
        break;
    }
    std::sort(result.begin(), result.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return result;
}
std::vector<Port> enumeratePorts(bool &ok) {
    std::vector<Port> result;
    unsigned hubIndex = 0;
    bool anyFailure = false;
    ok = interfaces(GUID_DEVINTERFACE_USB_HUB, [&](const std::wstring &path, SP_DEVINFO_DATA &info) {
        ++hubIndex;
        Handle h(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr));
        if (!h) {
            anyFailure = true;
            return;
        }
        USB_NODE_INFORMATION node{};
        node.NodeType = UsbHub;
        DWORD n = 0;
        if (!DeviceIoControl(h.get(), IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node), &node,
                             sizeof(node), &n, nullptr)) {
            anyFailure = true;
            return;
        }
        auto stable = property(info.DevInst, DEVPKEY_Device_LocationPaths);
        if (stable.empty())
            stable = devId(info.DevInst);
        for (ULONG index = 1; index <= node.u.HubInformation.HubDescriptor.bNumberOfPorts; ++index) {
            std::vector<BYTE> conn(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO));
            auto p = reinterpret_cast<PUSB_NODE_CONNECTION_INFORMATION_EX>(conn.data());
            p->ConnectionIndex = index;
            bool queried = DeviceIoControl(h.get(), IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, p,
                                           static_cast<DWORD>(conn.size()), p,
                                           static_cast<DWORD>(conn.size()), &n, nullptr) != FALSE;
            Port port;
            port.id = lower(stable) + L"/port/" + std::to_wstring(index);
            port.name = L"集线器 " + std::to_wstring(hubIndex) + L" · 端口 " + std::to_wstring(index);
            port.state =
                queried ? (p->ConnectionStatus == NoDeviceConnected || p->ConnectionStatus == DeviceConnected
                               ? State::Valid
                               : State::Unavailable)
                        : State::Unavailable;
            port.connected = queried && p->ConnectionStatus == DeviceConnected;
            std::vector<BYTE> props(4096, 0);
            auto cp = reinterpret_cast<PUSB_PORT_CONNECTOR_PROPERTIES>(props.data());
            cp->ConnectionIndex = index;
            if (DeviceIoControl(h.get(), IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES, cp,
                                static_cast<DWORD>(props.size()), cp, static_cast<DWORD>(props.size()), &n,
                                nullptr)) {
                port.physical = cp->UsbPortProperties.PortIsUserConnectable;
                // Hide known internal/non-user-connectable ports; don't guess on older hubs.
                if (!port.physical)
                    continue;
                if (cp->CompanionPortNumber && cp->CompanionHubSymbolicLinkName[0])
                    port.companion = lower(cp->CompanionHubSymbolicLinkName) + L"/" +
                                     std::to_wstring(cp->CompanionPortNumber);
                if (cp->UsbPortProperties.PortConnectorIsTypeC)
                    port.name += L" (USB-C)";
            } else
                port.name += L" (映射未确认)";
            // Keep symbolic address only until companion pairs have been merged.
            port.name += L"|" + lower(path) + L"/" + std::to_wstring(index);
            if (port.connected) {
                wchar_t suffix[48]{};
                swprintf_s(suffix, L" · %04X:%04X", p->DeviceDescriptor.idVendor,
                           p->DeviceDescriptor.idProduct);
                auto pos = port.name.find(L'|');
                port.name.insert(pos, suffix);
            }
            result.push_back(std::move(port));
        }
    });
    auto normalize = [](std::wstring s) {
        s = lower(s);
        if (s.starts_with(L"\\\\?\\") || s.starts_with(L"\\??\\"))
            s.erase(0, 4);
        return s;
    };
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i].id.empty() || result[i].companion.empty())
            continue;
        for (size_t j = i + 1; j < result.size(); ++j) {
            auto sep = result[j].name.find(L'|');
            if (sep == std::wstring::npos)
                continue;
            if (normalize(result[j].name.substr(sep + 1)) == normalize(result[i].companion)) {
                auto &a = result[i];
                auto &b = result[j];
                a.id = std::min(a.id, b.id);
                a.connected |= b.connected;
                if (a.connected)
                    a.state = State::Valid;
                else if (b.state == State::Unavailable)
                    a.state = State::Unavailable;
                if (b.connected)
                    a.name = b.name;
                b.id.clear();
                break;
            }
        }
    }
    std::erase_if(result, [](auto &p) { return p.id.empty(); });
    for (auto &p : result) {
        p.name = p.name.substr(0, p.name.find(L'|'));
        p.companion.clear();
    }
    ok = ok && (!anyFailure || !result.empty());
    return result;
}
Reading standardBattery(const Device &d) {
    if (!d.path.empty()) {
        Handle h(CreateFileW(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (h) {
            PHIDP_PREPARSED_DATA prep = nullptr;
            if (HidD_GetPreparsedData(h.get(), &prep)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS && caps.FeatureReportByteLength &&
                    caps.FeatureReportByteLength <= 4096) {
                    USHORT count = caps.NumberFeatureValueCaps;
                    std::vector<HIDP_VALUE_CAPS> values(count);
                    if (count &&
                        HidP_GetValueCaps(HidP_Feature, values.data(), &count, prep) == HIDP_STATUS_SUCCESS)
                        for (unsigned i = 0; i < count; ++i) {
                            auto &v = values[i];
                            // Generic Device Controls / Battery Strength, explicitly 0..100 only.
                            bool usage = v.IsRange ? (v.Range.UsageMin <= 0x20 && v.Range.UsageMax >= 0x20)
                                                   : v.NotRange.Usage == 0x20;
                            if (v.UsagePage != 6 || !usage || v.LogicalMin != 0 || v.LogicalMax != 100)
                                continue;
                            std::vector<char> report(caps.FeatureReportByteLength, 0);
                            report[0] = static_cast<char>(v.ReportID);
                            ULONG value = 0;
                            bool valid =
                                HidD_GetFeature(h.get(), report.data(), static_cast<ULONG>(report.size())) &&
                                HidP_GetUsageValue(HidP_Feature, 6, v.LinkCollection, 0x20, &value, prep,
                                                   report.data(), static_cast<ULONG>(report.size())) ==
                                    HIDP_STATUS_SUCCESS &&
                                value <= 100;
                            HidD_FreePreparsedData(prep);
                            return valid ? Reading::valid(std::to_wstring(value) + L"%",
                                                          L"HID Battery Strength", 180000, value <= 15)
                                         : Reading::unavailable(State::Unavailable, L"HID Battery Strength");
                        }
                }
                HidD_FreePreparsedData(prep);
            }
        }
    }
    if (d.bluetooth) {
        // BAS service interface: paired devices only, no scan/pair/connect UI.
        const GUID bas = {0x0000180f, 0, 0x1000, {0x80, 0, 0, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
        Reading answer = Reading::unavailable(State::Unsupported, L"Bluetooth BAS");
        interfaces(bas, [&](const std::wstring &path, SP_DEVINFO_DATA &info) {
            Device service;
            identity(service, info.DevInst);
            if (service.root != d.root && (service.container.empty() || service.container != d.container))
                return;
            Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr));
            if (!h) {
                answer = Reading::unavailable();
                return;
            }
            USHORT count = 0;
            BluetoothGATTGetCharacteristics(h.get(), nullptr, 0, nullptr, &count, BLUETOOTH_GATT_FLAG_NONE);
            if (!count || count > 256)
                return;
            std::vector<BTH_LE_GATT_CHARACTERISTIC> chars(count);
            if (FAILED(BluetoothGATTGetCharacteristics(h.get(), nullptr, count, chars.data(), &count,
                                                       BLUETOOTH_GATT_FLAG_NONE))) {
                answer = Reading::unavailable();
                return;
            }
            for (auto &ch : chars)
                if (ch.CharacteristicUuid.IsShortUuid && ch.CharacteristicUuid.Value.ShortUuid == 0x2a19 &&
                    ch.IsReadable) {
                    std::array<BYTE, 128> buffer{};
                    USHORT required = 0;
                    auto v = reinterpret_cast<PBTH_LE_GATT_CHARACTERISTIC_VALUE>(buffer.data());
                    auto hr = BluetoothGATTGetCharacteristicValue(
                        h.get(), &ch, static_cast<ULONG>(buffer.size()), v, &required,
                        BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE);
                    answer = SUCCEEDED(hr) && v->DataSize == 1 && v->Data[0] <= 100
                                 ? Reading::valid(std::to_wstring(v->Data[0]) + L"%", L"Bluetooth BAS 0x2A19",
                                                  180000, v->Data[0] <= 15)
                                 : Reading::unavailable(State::Unavailable, L"Bluetooth BAS");
                    return;
                }
        });
        return answer;
    }
    return Reading::unavailable(State::Unsupported, L"device capabilities");
}
void DeviceService::stop() {
    if (worker_.joinable()) {
        SetEvent(stop_.get());
        CancelSynchronousIo(worker_.native_handle());
        worker_.join();
    }
}
void DeviceService::configure(const Settings &s, bool active, bool rescan) {
    std::lock_guard lock(mutex_);
    bool deviceChanged = s.mouse != config_.mouse || s.keyboard != config_.keyboard;
    refresh_ |= deviceChanged || (!active_ && active);
    rescan_ |= rescan || deviceChanged || (!active_ && active);
    config_ = s;
    active_ = active;
    SetEvent(wake_.get());
}
void DeviceService::run() {
    Inventory inventory;
    Snapshot out;
    std::unique_ptr<LogitechMouse> logi;
    std::wstring logiRoot;
    uint64_t lastScan = 0, lastBattery = 0, lastConnect = 0;
    Reading otherBattery;
    for (;;) {
        if (WaitForSingleObject(stop_.get(), 0) == WAIT_OBJECT_0)
            break;
        Settings config;
        bool active, rescan, refresh;
        {
            std::lock_guard lock(mutex_);
            config = config_;
            active = active_;
            rescan = std::exchange(rescan_, false);
            refresh = std::exchange(refresh_, false);
        }
        if (active || rescan) {
            try {
                // PnP may recreate the same device path after a rapid unplug/replug.
                // Existing handles then refer to the removed PDO and must be reopened.
                if (rescan) {
                    logi.reset();
                    lastConnect = lastBattery = 0;
                }
                if (rescan || !lastScan || now() - lastScan >= 30000) {
                    inventory = enumerateInputs();
                    out.mice = inventory.mice;
                    out.keyboards = inventory.keyboards;
                    out.inventoryOk = inventory.ok;
                    out.excludedDevices = inventory.excluded;
                    try {
                        out.displays = enumerateDisplays(out.displaysOk, &out.excludedDevices);
                    } catch (...) {
                        out.displaysOk = false;
                    }
                    try {
                        out.ports = enumeratePorts(out.portsOk);
                    } catch (...) {
                        out.portsOk = false;
                    }
                    lastScan = out.inventoryAt = now();
                }
                // Hidden widgets refresh inventory only on explicit events/menu requests.
                // They never keep old numeric readings current or poll device telemetry.
                if (!active) {
                    out.battery = out.dpi = out.keyboard = Reading::unavailable();
                } else {
                    auto select = [](const std::vector<Device> &list,
                                     const std::wstring &id) -> const Device * {
                        if (id.empty())
                            return list.empty() ? nullptr : &list.front();
                        auto it = std::find_if(list.begin(), list.end(), [&](auto &d) { return d.id == id; });
                        return it == list.end() ? nullptr : &*it;
                    };
                    if (out.excludedDevices.contains(config.mouse))
                        config.mouse.clear();
                    if (out.excludedDevices.contains(config.keyboard))
                        config.keyboard.clear();
                    auto mouse = select(inventory.mice, config.mouse);
                    auto keyboard = select(inventory.keyboards, config.keyboard);
                    out.selectedMouse = mouse ? mouse->id : config.mouse;
                    out.selectedKeyboard = keyboard ? keyboard->id : config.keyboard;
                    auto missingState = [&](const std::wstring &id) {
                        if (!inventory.ok)
                            return State::Unavailable;
                        if (id.empty())
                            return State::Disconnected;
                        auto instance = id.substr(0, id.rfind(L'/'));
                        DEVINST node = 0;
                        auto result = CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL);
                        return result == CR_NO_SUCH_DEVNODE ? State::Disconnected : State::Unavailable;
                    };
                    if (!mouse) {
                        out.battery = out.dpi = Reading::unavailable(missingState(config.mouse), L"PnP");
                        out.mouseName = L"鼠标";
                        logi.reset();
                        logiRoot.clear();
                    } else if (!mouse->operational) {
                        out.battery = out.dpi =
                            Reading::unavailable(State::Unavailable, L"PnP device not started");
                        out.mouseName = mouse->name;
                        logi.reset();
                        lastConnect = 0;
                    } else {
                        out.mouseName = mouse->name;
                        if (logiRoot != mouse->root) {
                            logi.reset();
                            logiRoot = mouse->root;
                            lastConnect = lastBattery = 0;
                        }
                        if (mouse->vendor == 0x046d && !logi &&
                            (!lastConnect || now() - lastConnect >= 30000 || refresh)) {
                            auto candidate = std::make_unique<LogitechMouse>();
                            if (candidate->connect(inventory.controls, mouse->root, stop_.get()))
                                logi = std::move(candidate);
                            lastConnect = now();
                        }
                        if (logi) {
                            bool responding = logi->poll(out.battery, out.dpi, refresh);
                            out.mouseName = logi->name();
                            for (auto &m : out.mice)
                                if (m.id == mouse->id)
                                    m.name = out.mouseName;
                            if (!responding && now() - lastConnect >= 30000)
                                logi.reset();
                        } else {
                            if (mouse->vendor == 0x046d) {
                                out.battery = out.dpi = Reading::unavailable(
                                    State::Unavailable, L"HID++ device did not answer capability discovery");
                            } else {
                                if (refresh || !lastBattery || now() - lastBattery >= 60000) {
                                    otherBattery = standardBattery(*mouse);
                                    lastBattery = now();
                                }
                                out.battery = otherBattery;
                                out.dpi =
                                    Reading::unavailable(State::Unsupported, L"no verified DPI protocol");
                            }
                        }
                    }
                    if (!keyboard)
                        out.keyboard = Reading::unavailable(missingState(config.keyboard), L"PnP");
                    else if (!keyboard->operational)
                        out.keyboard = Reading::unavailable(State::Unavailable, L"PnP device not started");
                    else if (keyboard->receiver || keyboard->bluetooth)
                        out.keyboard =
                            Reading::valid(L"状态未知", L"receiver/paired-device presence only", 15000, true);
                    else
                        out.keyboard = Reading::valid(
                            L"已连接" + (keyboard->transport.empty() ? L"" : L" · " + keyboard->transport),
                            L"PnP physical keyboard", 15000);
                }
            } catch (...) {
                out.battery = out.dpi = out.keyboard = Reading::unavailable();
            }
            {
                std::lock_guard lock(mutex_);
                latest_ = out;
            }
            if (target_)
                PostMessageW(target_, updatedMessage, 0, 0);
        }
        HANDLE events[] = {stop_.get(), wake_.get()};
        auto result = WaitForMultipleObjects(2, events, FALSE, active ? 5000 : INFINITE);
        if (result == WAIT_OBJECT_0)
            break;
    }
}
} // namespace dp
