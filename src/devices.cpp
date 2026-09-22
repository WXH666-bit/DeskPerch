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
    d.receiver = n.find(L"receiver") != std::wstring::npos || n.find(L"接收器") != std::wstring::npos ||
                 n.find(L"dongle") != std::wstring::npos || n.find(L"2.4g") != std::wstring::npos;
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
    bool inaccessible = false;
    std::map<std::wstring, std::vector<std::wstring>> related;
    GUID hid{};
    HidD_GetHidGuid(&hid);
    r.ok = interfaces(hid, [&](const std::wstring &path, SP_DEVINFO_DATA &info) {
        Handle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (!h) {
            inaccessible = true;
            return;
        }
        HIDD_ATTRIBUTES attr{sizeof(attr)};
        if (!HidD_GetAttributes(h.get(), &attr)) {
            inaccessible = true;
            return;
        }
        PHIDP_PREPARSED_DATA prep = nullptr;
        if (!HidD_GetPreparsedData(h.get(), &prep)) {
            inaccessible = true;
            return;
        }
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
        // Battery feature reports may live in a different top-level collection.
        related[d.root].push_back(d.path);
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
    for (auto &d : r.mice)
        d.batteryPaths = related[d.root];
    // A gaming mouse exposes keyboard usages for its buttons, not a second keyboard.
    std::erase_if(r.keyboards, [&](const Device &k) {
        // Shared receivers can contain a real keyboard. Suppress only the
        // verified mouse receiver or a wired mouse's extra keyboard collection.
        auto label = lower(k.name);
        if (!(k.vendor == 0x046d && k.product == 0xc547) &&
            batteryKind(k, Reading::unavailable()) != BatteryKind::None &&
            (k.receiver ||
             (label.find(L"mouse") == std::wstring::npos && label.find(L"鼠标") == std::wstring::npos)))
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
    r.ok = r.ok && !inaccessible;
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
            // A hub is a grouping of its downstream ports, not an occupied peripheral row.
            if (port.connected && p->DeviceIsHub)
                continue;
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
    ok = ok && !anyFailure;
    return result;
}
static Reading singleBattery(const Device &d) {
    Reading unsupported = Reading::unavailable(State::Unavailable, L"HID capabilities unavailable");
    if (!d.path.empty()) {
        Handle h(CreateFileW(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (h) {
            PHIDP_PREPARSED_DATA prep = nullptr;
            if (HidD_GetPreparsedData(h.get(), &prep)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS)
                    unsupported = Reading::unavailable(State::Unsupported, L"no standard battery feature");
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
                                hidBatteryPercent(v.UsagePage, 0x20, v.LogicalMin, v.LogicalMax, value)
                                    .has_value();
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
                    answer =
                        SUCCEEDED(hr) && v->DataSize == 1 && batteryPercent(std::span(v->Data, 1)).has_value()
                            ? Reading::valid(std::to_wstring(v->Data[0]) + L"%", L"Bluetooth BAS 0x2A19",
                                             180000, v->Data[0] <= 15)
                            : Reading::unavailable(State::Unavailable, L"Bluetooth BAS");
                    return;
                }
        });
        return answer;
    }
    return unsupported;
}
Reading standardBattery(const Device &d) {
    auto answer = singleBattery(d);
    if (answer.state == State::Valid)
        return answer;
    for (const auto &path : d.batteryPaths) {
        if (path == d.path)
            continue;
        Device collection = d;
        collection.path = path;
        collection.bluetooth = false;
        auto candidate = singleBattery(collection);
        if (candidate.state == State::Valid)
            return candidate;
        if (candidate.state == State::Unavailable)
            answer = candidate;
    }
    return answer;
}
static Reading inputConnection(const Device &d) {
    auto instance = d.root;
    DEVINST node = 0;
    auto cr = CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL);
    if (cr == CR_NO_SUCH_DEVNODE)
        return Reading::unavailable(State::Disconnected, L"PnP");
    ULONG flags = 0, problem = 0;
    if (cr != CR_SUCCESS || CM_Get_DevNode_Status(&flags, &problem, node, 0) != CR_SUCCESS ||
        !(flags & DN_STARTED) || (flags & DN_HAS_PROBLEM))
        return Reading::unavailable(State::Unavailable, L"PnP");
    if (d.receiver || d.bluetooth)
        return Reading::valid(L"状态未知", L"receiver/pairing presence, not peripheral presence");
    return Reading::valid(L"已连接 · USB", L"PnP external HID");
}
void DeviceService::stop() {
    SetEvent(stop_.get());
    SetEvent(wake_.get());
    ready_.notify_all();
    if (worker_.joinable()) {
        CancelSynchronousIo(worker_.native_handle());
        worker_.join();
    }
    for (auto &thread : collectors_)
        if (thread.joinable()) {
            CancelSynchronousIo(thread.native_handle());
            thread.join();
        }
}
void DeviceService::configure(const Settings &s, bool active, bool rescan) {
    std::lock_guard lock(mutex_);
    const bool changed = s.selected != config_.selected || active != active_;
    if (changed || rescan) {
        ++epoch_;
        for (auto &job : jobs_)
            pending_.erase(job.device.root);
        jobs_.clear();
        for (auto &[id, d] : latest_.devices)
            if (d.kind == DeviceKind::Mouse)
                d.battery = d.dpi = Reading::unavailable();
    }
    rescan_ |= rescan || changed;
    config_ = s;
    active_ = active;
    SetEvent(wake_.get());
    ready_.notify_all();
}
void DeviceService::run() {
    Inventory inventory;
    std::vector<Display> displays;
    std::vector<Port> ports;
    std::set<std::wstring> excluded;
    bool displaysOk = false, portsOk = false;
    uint64_t scanned = 0;
    size_t cursor = 0;
    std::map<std::wstring, std::shared_ptr<RootContext>> contexts;
    while (WaitForSingleObject(stop_.get(), 0) != WAIT_OBJECT_0) {
        bool active, rescan;
        uint64_t epoch;
        Settings config;
        Snapshot out;
        {
            std::lock_guard lock(mutex_);
            active = active_;
            rescan = std::exchange(rescan_, false);
            epoch = epoch_;
            config = config_;
            out = latest_;
        }
        if (active || rescan) {
            if (rescan || !scanned || now() - scanned >= 30000) {
                try {
                    inventory = enumerateInputs();
                } catch (...) {
                    inventory.ok = false;
                }
                excluded = inventory.excluded;
                try {
                    displays = enumerateDisplays(displaysOk, &excluded);
                } catch (...) {
                    displaysOk = false;
                }
                try {
                    ports = enumeratePorts(portsOk);
                } catch (...) {
                    portsOk = false;
                }
                scanned = now();
            }
            std::map<std::wstring, DeviceStatus> catalog;
            for (const auto &mouse : inventory.mice) {
                bool expanded = false;
                for (const auto &[id, prior] : out.devices)
                    if (prior.kind == DeviceKind::Mouse && prior.root == mouse.root) {
                        auto d = prior;
                        if (!active || rescan)
                            d.battery = d.dpi = Reading::unavailable();
                        catalog[id] = d;
                        expanded = true;
                    }
                if (!expanded) {
                    DeviceStatus d;
                    d.id = L"mouse:" + mouse.id;
                    d.name = mouse.name;
                    d.root = mouse.root;
                    d.connection = inputConnection(mouse);
                    catalog[d.id] = std::move(d);
                }
            }
            for (const auto &keyboard : inventory.keyboards) {
                DeviceStatus d;
                d.kind = DeviceKind::Keyboard;
                d.id = L"keyboard:" + keyboard.id;
                d.name = keyboard.name;
                d.root = keyboard.root;
                d.connection = inputConnection(keyboard);
                catalog[d.id] = std::move(d);
            }
            for (const auto &screen : displays) {
                DeviceStatus d;
                d.kind = DeviceKind::Display;
                d.id = L"display:" + screen.id;
                d.name = screen.name;
                d.connection = !displaysOk ? Reading::unavailable()
                               : !screen.connected
                                   ? Reading::unavailable(State::Disconnected)
                                   : Reading::valid(screen.enabled ? L"已启用" : L"已连接，未启用",
                                                    L"QueryDisplayConfig", 60000);
                d.connection.sampled = scanned;
                catalog[d.id] = std::move(d);
            }
            for (const auto &port : ports) {
                DeviceStatus d;
                d.kind = DeviceKind::Port;
                d.id = L"port:" + port.id;
                d.name = port.name;
                d.autoVisible = port.connected;
                d.connection = port.state != State::Valid ? Reading::unavailable()
                               : port.connected
                                   ? Reading::valid(L"已连接", L"USB hub port", 60000)
                                   : Reading::unavailable(State::Disconnected, L"USB port empty");
                d.connection.sampled = scanned;
                catalog[d.id] = std::move(d);
            }
            {
                std::lock_guard lock(mutex_);
                if (epoch == epoch_) {
                    // Preserve worker results that arrived during the directory scan.
                    if (!rescan && active) {
                        for (const auto &mouse : inventory.mice) {
                            bool fresh = std::any_of(
                                latest_.devices.begin(), latest_.devices.end(), [&](const auto &e) {
                                    return e.second.kind == DeviceKind::Mouse && e.second.root == mouse.root;
                                });
                            if (!fresh)
                                continue;
                            std::erase_if(catalog, [&](const auto &e) {
                                return e.second.kind == DeviceKind::Mouse && e.second.root == mouse.root;
                            });
                            for (const auto &[id, d] : latest_.devices)
                                if (d.kind == DeviceKind::Mouse && d.root == mouse.root)
                                    catalog[id] = d;
                        }
                    }
                    latest_.devices = std::move(catalog);
                    latest_.inventoryOk = inventory.ok;
                    latest_.displaysOk = displaysOk;
                    latest_.portsOk = portsOk;
                    latest_.inventoryAt = scanned;
                    latest_.excludedDevices = excluded;
                    latest_.mice = inventory.mice;
                    latest_.keyboards = inventory.keyboards;
                    latest_.displays = displays;
                    latest_.ports = ports;
                    std::erase_if(contexts, [&](auto &entry) {
                        return std::none_of(inventory.mice.begin(), inventory.mice.end(),
                                            [&](auto &m) { return m.root == entry.first; });
                    });
                    for (size_t i = 0; active && i < inventory.mice.size() && jobs_.size() < 32; ++i) {
                        auto &mouse = inventory.mice[(cursor + i) % inventory.mice.size()];
                        bool wanted =
                            config.selected.empty() ||
                            std::any_of(config.selected.begin(), config.selected.end(), [&](auto &entry) {
                                return entry.second.kind == DeviceKind::Mouse &&
                                       entry.first.starts_with(L"mouse:" + mouse.root + L"/");
                            });
                        if (!wanted || pending_.contains(mouse.root))
                            continue;
                        auto &ctx = contexts[mouse.root];
                        if (!ctx || ctx->epoch != epoch) {
                            ctx = std::make_shared<RootContext>();
                            ctx->epoch = epoch;
                        }
                        jobs_.push_back({mouse, inventory.controls, epoch, ctx});
                        pending_.insert(mouse.root);
                    }
                    if (!inventory.mice.empty())
                        cursor = (cursor + 32) % inventory.mice.size();
                }
            }
            ready_.notify_all();
            if (target_)
                PostMessageW(target_, updatedMessage, 0, 0);
        }
        HANDLE waits[] = {stop_.get(), wake_.get()};
        if (WaitForMultipleObjects(2, waits, FALSE, active ? 5000 : INFINITE) == WAIT_OBJECT_0)
            break;
    }
}
void DeviceService::collect() {
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(
                lock, [&] { return WaitForSingleObject(stop_.get(), 0) == WAIT_OBJECT_0 || !jobs_.empty(); });
            if (WaitForSingleObject(stop_.get(), 0) == WAIT_OBJECT_0)
                return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        std::vector<DeviceStatus> results;
        auto &ctx = *job.context;
        auto &mouse = job.device;
        auto cancelled = [&] {
            std::lock_guard lock(mutex_);
            return !active_ || epoch_ != job.epoch || WaitForSingleObject(stop_.get(), 0) == WAIT_OBJECT_0;
        };
        try {
            auto connection = inputConnection(mouse);
            if (!cancelled() && mouse.vendor == 0x046d && mouse.operational &&
                connection.state != State::Disconnected &&
                (!ctx.discovered || now() - ctx.discovered >= 30000)) {
                if (!ctx.channel)
                    ctx.channel = std::make_shared<HidChannel>();
                for (unsigned slot : {1u, 2u, 3u, 4u, 5u, 6u, 255u}) {
                    if (cancelled())
                        break;
                    if (slot == 255 && !ctx.mice.empty())
                        continue;
                    if (ctx.mice.contains(slot))
                        continue;
                    auto candidate = std::make_unique<LogitechMouse>();
                    if (candidate->connect(job.controls, mouse.root, stop_.get(), slot, ctx.channel))
                        ctx.mice[slot] = std::move(candidate);
                }
                ctx.discovered = now();
            }
            for (auto &[slot, logi] : ctx.mice) {
                if (cancelled())
                    break;
                DeviceStatus d;
                d.id = L"mouse:" + mouse.root +
                       (slot == 1 || slot == 255 ? L"/mouse" : L"/slot/" + std::to_wstring(slot));
                d.name = logi->name();
                d.root = mouse.root;
                if (connection.state == State::Disconnected)
                    d.battery = d.dpi = d.connection = connection;
                else {
                    bool ok = logi->poll(d.battery, d.dpi, false);
                    d.connection = ok ? Reading::valid(L"已连接", L"HID++ ping") : Reading::unavailable();
                    d.power = batteryKind(mouse, d.battery);
                }
                results.push_back(std::move(d));
            }
            if (results.empty() && !cancelled()) {
                DeviceStatus d;
                d.id = L"mouse:" + mouse.id;
                d.name = mouse.name;
                d.root = mouse.root;
                d.connection = connection;
                d.dpi = Reading::unavailable(State::Unsupported, L"no verified DPI protocol");
                if (connection.state == State::Disconnected || connection.state == State::Unavailable)
                    d.battery = d.dpi = connection;
                else {
                    if (!ctx.batteryAt || now() - ctx.batteryAt >= 60000) {
                        ctx.battery = standardBattery(mouse);
                        ctx.batteryAt = now();
                    }
                    d.battery = ctx.battery;
                    d.power = batteryKind(mouse, d.battery);
                    if (mouse.vendor == 0x046d &&
                        std::any_of(job.controls.begin(), job.controls.end(),
                                    [&](const auto &control) { return control.root == mouse.root; }))
                        d.dpi = Reading::unavailable(State::Unavailable, L"HID++ discovery unavailable");
                }
                results.push_back(std::move(d));
            }
        } catch (...) {
            DeviceStatus d;
            d.id = L"mouse:" + mouse.id;
            d.name = mouse.name;
            d.root = mouse.root;
            results.push_back(std::move(d));
        }
        {
            std::lock_guard lock(mutex_);
            pending_.erase(mouse.root);
            if (publishMouseResults(latest_, mouse.root, std::move(results), active_, epoch_, job.epoch) &&
                target_)
                PostMessageW(target_, updatedMessage, 0, 0);
        }
    }
}
} // namespace dp
