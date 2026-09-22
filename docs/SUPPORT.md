# 设备支持 / Device support

| Provider | Coverage |
| --- | --- |
| Logitech HID++ | Read-only device name, battery and active DPI. Tested with a LIGHTSPEED receiver and PRO X Wireless. Other models require verification. |
| DPI | `0x2201` and `0x2202`; the latter has parser tests but no corresponding hardware validation. Zero is not replaced with a preset. |
| Battery | HID++ `0x1004` / `0x1000`, explicitly defined 0–100 HID Feature Reports and paired Bluetooth Battery Service. Standard HID/BLE need additional hardware testing. |
| Keyboard | Physical external PnP devices, with duplicate HID collections merged. Receiver/pairing presence alone does not establish that a wireless keyboard is online. |
| USB | Hub port queries, user-connectable properties and confirmed USB 2/3 companion merging. Unknown physical mappings are labeled. |
| Display | QueryDisplayConfig connection/enabled paths, excluding known integrated output types. No inference about physical power. |

仅显示能够确认的数据；不支持、暂不可用、断开和过期分开处理。单个共享接收器内多个鼠标的逐槽位选择尚未实现。设备无序列号时，换 USB 端口可能改变 Windows 标识，需重新选择关注对象。

Unsupported, unavailable, disconnected and expired readings are distinct. Per-slot selection of multiple mice on one shared receiver is not implemented. A device without a serial number may receive a different Windows identifier when moved to another USB port.

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
