# 设备支持 / Device support

狼途 / LANGTU：照片已确认型号为 M8 MAX，并已找到官网链接的 M8 网页驱动及电量/DPI 查询逻辑。目前实物尚未接入，设备标识和回复结构仍待核对，尚未启用该私有协议。[适配调查记录](LANGTU-M8.md)。通用 HID/BLE 路径仍可用于符合相应标准的设备。

The photo identifies M8 MAX. The official M8 web driver's read queries have been located; hardware identity and response validation are still pending before enabling its private protocol. Generic HID/BLE paths remain brand-independent.

| Provider | Coverage |
| --- | --- |
| Logitech HID++ | Read-only device name, battery and active DPI. Tested with a LIGHTSPEED receiver and PRO X Wireless. Other models require verification. |
| DPI | `0x2201` and `0x2202`; the latter has parser tests but no corresponding hardware validation. Zero is not replaced with a preset. |
| Battery | HID++ `0x1004` / `0x1000`, explicitly defined 0–100 HID Feature Reports and paired Bluetooth Battery Service. Standard HID/BLE need additional hardware testing. |
| Keyboard | Physical external PnP devices, with duplicate HID collections merged. Receiver/pairing presence alone does not establish that a wireless keyboard is online. |
| USB | Hub port queries, user-connectable properties and confirmed USB 2/3 companion merging. Unknown physical mappings are labeled. |
| Display | QueryDisplayConfig connection/enabled paths, excluding known integrated output types. No inference about physical power. |

仅显示能够确认的数据；不支持、暂不可用、断开和过期分开处理。已实现逐槽位鼠标上下文、共享串行请求通道与独立结果；多槽位接收器尚无实物验证。设备无序列号时，换 USB 端口可能改变 Windows 标识，需重新选择关注对象。

Unsupported, unavailable, disconnected and expired readings are distinct. Per-slot mouse contexts and serialized requests on one shared receiver channel are implemented; multi-slot hardware validation is still pending. A device without a serial number may receive a different Windows identifier when moved to another USB port.

设备事件合并刷新；可见时在线状态／DPI 每 5 秒查询，电量每 60 秒查询。隐藏、锁屏或休眠时停止主动轮询。DPI／在线状态有效期 15 秒，电量 180 秒；查询失败会移除旧数值。

Events trigger coalesced refreshes. While visible, online state/DPI are queried every 5 seconds and battery every 60 seconds. Polling pauses while hidden, locked or suspended. Online/DPI readings expire after 15 seconds and battery after 180 seconds; failures clear current values.

## 验证边界 / Validation limits

Explorer 宿主、多屏缩放、Win+D、全屏、睡眠恢复及厂商软件共存均应在具体机器上验证。CI 仅执行编译和无硬件单元测试，不代表实机验收通过。资源目标不是已保证的性能数值；可使用仓库内脚本在自己的电脑上采样。

Explorer hosting, mixed-DPI monitors, Show Desktop, fullscreen behavior, resume and vendor-software coexistence need target-machine testing. CI only builds and runs hardware-independent tests. Resource goals are not guaranteed measurements; the included scripts support local sampling.

## References

- [Logitech HID++](https://github.com/Logitech/cpg-docs/blob/master/hidpp20/README.rst)
- [Microsoft USB hub port query](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbioctl/ni-usbioctl-ioctl_usb_get_node_connection_information_ex)
- [QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [Display output types](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ne-wingdi-displayconfig_video_output_technology)

## 多设备版本 / Multi-device update

自动模式显示当前外接设备，手动模式全局多选。最多两个采集工作线程，队列上限 32 个物理设备；同一接收器不会同时进入两个线程。选择变更、暂停或拔插触发采集代次更新，过时代次的响应不再发布。鼠标在线/DPI 仍按 5 秒周期调度，单个请求超时会延后该设备下一次完成时间。

Automatic mode lists external devices; manual mode filters globally. Two collection workers, a bounded 32-root queue, serialized receiver channels and generation checks prevent cross-device publication and stale responses after removal or pause. Five-second scheduling is retained; timeouts can delay completion.

`-` 仅用于明确的无电池型号：Logitech G502 HERO (`046d:c08b`) 与 Razer DeathAdder Essential (`1532:006e`, `1532:0071`, `1532:0098`)。这是设备资料匹配，不是新增厂商命令，也不表示这些型号已实机验证。实际电池报告优先于型号表。其他 USB 鼠标没有电池证据时仍显示暂不支持／暂不可用，不猜测。

Batteryless catalog: G502 HERO and DeathAdder Essential IDs above. Catalog recognition uses published model identities, adds no private commands and does not imply hardware validation. An actual battery report takes precedence. Unrecognized USB models retain unknown battery applicability.

- [G502 HERO identity](https://github.com/libratbag/libratbag/blob/master/data/devices/logitech-g502-hero.device) / [manufacturer wired specification](https://www.logitechg.com/en-us/products/gaming-mice/g502-hero-gaming-mouse.html)
- [Razer model identities](https://openrazer.github.io/) / [manufacturer guide](https://dl.razerzone.com/master-guides/RazerSynapse3/DeathAdderEssential-00000110-en.pdf)
- [USB HID usages](https://www.usb.org/hid)
- [Microsoft Bluetooth battery service guidance](https://learn.microsoft.com/en-us/windows-hardware/design/accessory-guidelines/bluetooth-accessory-guidelines-battery-profiles-services)

通用 HID 只读取明确的 Battery Strength Feature（Usage Page 6、Usage 0x20、逻辑范围 0–100）。同一实体的相关 HID 集合都会查找；BLE BAS 只接受单字节 0–100，失败不沿用过期值。未读取普通键鼠输入报告。通用键盘仅显示连接状态，不新增电量。无实际电量协议的无线接收器只显示状态未知。

Standard HID reads only explicit 0–100 Battery Strength Features across related collections. BLE BAS accepts exactly one byte in 0–100. Ordinary mouse/keyboard input reports are not read. Generic keyboards expose connection state only; receiver presence does not prove wireless peripheral presence.
