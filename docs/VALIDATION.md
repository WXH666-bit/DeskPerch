# 多设备版本验证 / Multi-device validation

## 后续：狼途 M8 MAX 实机适配（2026-09-22）

借用鼠标通过 `A8A5:2255 / LTM8 2.4G` 接入。初次只读查询为 76%、2400 DPI；用户关闭底部电源后，接收器仍回复 75%，DPI 六档为全 FF，因此适配拒绝无效配置并撤下所有数值。用户开机切档后捕获 800 DPI，正式采集器随后读取 72%、1600 DPI。替换本机 EXE 后的实际挂件渲染为 79%、1600 DPI，无多余键盘行，配置文件保持不变。电量为固件上报值，开关机前后有波动，未作平滑或推算。

Release 编译和 core/layout 测试通过，新增正常帧、关机全 FF、短帧、错误命令／通知、非法档位及电量范围测试。未验证有线／蓝牙、充电、厂商工具并行、多鼠标及长时间资源表现；旧版资源采样不代表此次新增适配的实测结果。

Borrowed M8 MAX 2.4G hardware passed read-query and physical DPI-change checks. Powered-off receiver battery replies must not establish online status; invalid DPI configuration suppresses the readings. The running widget was updated and rendered successfully with unchanged preferences. Wired/Bluetooth, charging, concurrent vendor software and long-duration resource checks remain pending.

## 后续：锁定重启位置修复

保留用户保存的位置锚点，不再用加载文字变宽后的临时边界调整覆盖它；数据恢复后重新从锚点计算位置，模式切换不重写锚点。拖动中暂停自动重定位，松开后保存用户位置。

锁定且可见状态连续重启 3 次，左上角均为 (2099, 1361) 物理像素，设置文件内容未变化；core/layout 测试通过。可用 `tools/restart-position.ps1` 重复验证。下方资源采样对应其记录的旧 EXE 哈希，并非本次位置修复构建的重新采样。

The saved anchor survives temporary loading/error-text expansion and mode changes. Three locked restarts retained identical coordinates and unchanged preferences. Automatic repositioning pauses during dragging. Earlier resource results apply to their recorded binary hash.

日期 / Date: 2026-09-22。当前分支本地 Release 构建；没有修改签名或 GitHub Release 工作流。

## 自动测试 / Automated checks

MSVC x64 Release 编译、CTest core 与 layout 通过。覆盖：

- v1/v2 关注范围重置与其他设置保留；v3 多选、设备名称往返保存、损坏配置拒绝。
- 自动过滤空闲端口、首次勾选只选该项、跨类别选择、清空后恢复自动、简约模式不擅自补选。
- 关注对象缺席时保留名称；枚举失败不当作断开；两只鼠标读数独立。
- HID++ 错误、短报文、其他槽位、其他软件标识响应；DPI 不以预设替代。
- HID 百分比逻辑范围、错误 Usage、超范围值；BAS 缺失、多字节、0/100/101 边界样本。
- 已确认有线型号、USB 未知型号、实际电池报告优先；连接失败隐藏旧电量。
- 采集代次变化、隐藏和设备移除后的响应拒绝发布。
- 40 台模拟鼠标，透明与浅白、完整与简约、100%/125%/150%/200% 缩放、600 DIP 滚动视口、小工作区限宽限高。

Release build and both CTest targets pass. Tests cover migration, global selection, individual failure handling, HID++ reply matching, percentage validation, power applicability, stale-result rejection and 40-device layouts at four DPI scales. Fixture tests are not hardware certification.

## 本机验证 / Local hardware

PRO X Wireless（LIGHTSPEED）读取到真实电量 85%、DPI 2000，来源分别为 HID++ 0x1004、0x2201。自动列表包含鼠标和两个已连接外部 USB 接口；内置键盘、内置面板和空闲接口未出现在桌面列表。诊断截图已检查。窗口探测确认：父窗口 SHELLDLL_DefView，child/no_activate/tool_window 均为 1，topmost 与 app_has_foreground 为 0；卡片中心可命中、透明圆角不命中。

PRO X Wireless reported 85% and 2000 DPI through HID++ 0x1004/0x2201. Automatic view showed the mouse and two occupied external USB ports, excluding built-in devices and idle ports. Rendered output was inspected.

## 尚待实机验证 / Pending hardware validation

- 两只实际鼠标、共享接收器多个鼠标槽位、真实 HID Battery Strength 与 BLE BAS 外设。
- G502 HERO / DeathAdder Essential 无电池识别仅按资料匹配，尚无相应实物。
- 用户实际切换 DPI、关机/睡眠/充电、反复拔插及厂商软件并行操作。
- 多屏实际跨缩放拖动、物理显示器断开、Explorer 重启、锁屏唤醒及全屏交互回归。
- 多设备硬件压力、预热 5 分钟后的每模式 10 分钟正式资源测试、24 小时稳定性及 DWM/GPU 受控对照。

Multiple physical mice, shared receiver slots, generic HID/BLE battery hardware, power changes, hotplug, mixed-monitor desktop recovery, long-duration resource stability and controlled GPU/DWM comparisons remain unverified. No additional brand is claimed as hardware-tested.

## 最终 EXE 短时资源采样 / Final EXE resource sample

Windows 11 build 26200，32 个逻辑处理器；预热 15 秒，每模式 60 秒、2 秒间隔。实际负载为一只鼠标和两个占用 USB 接口，不是多鼠标硬件基准。CPU 按整机计算能力归一化。

| 模式 / Mode | 工作集均值/峰值 MiB | 私有提交均值/峰值 MiB | CPU 均值/峰值 % | 句柄范围 | 线程范围 |
| --- | ---: | ---: | ---: | ---: | ---: |
| full | 22.68 / 26.02 | 4.73 / 5.62 | 0.0048 / 0.0971 | 211–260 | 8–10 |
| compact | 25.76 / 26.01 | 5.44 / 5.68 | 0.0121 / 0.2178 | 257–261 | 6–9 |
| hidden | 25.68 / 25.68 | 5.38 / 5.38 | 0.0008 / 0.0243 | 257–257 | 6–6 |

This is a short local sample with one physical mouse and two occupied ports, not a multiple-mouse or long-term benchmark. CPU percentages are normalized across logical processors. Two collector workers are separate from the coordinator/UI and Windows-created threads.

SHA-256: `36A4B32B8CC5BB3749E31BD7913E53B101D14CAAA0B1CFAA31A20BDD92CCB320`

## 操作回归 / Interaction regression

2026-09-22 托盘选择修复：Release 构建及 core/layout 测试通过。新增覆盖菜单重入不覆盖点击动作、一次点击只执行一次、跨类别多选、取消菜单、取消最后选择恢复自动，以及选择的配置序列化恢复。按实际托盘版本分派右键事件，菜单打开时不再强制重扫设备。此次未完成真实鼠标操作托盘的端到端验证，待用户确认。

Tray selection fix: Release build and core/layout tests pass, including reentrant popup protection, single dispatch, cross-category selection, cancellation, restoring automatic mode and configuration round-trip. Physical tray-click end-to-end verification remains pending.

原生命令集成测试通过：重复启动保持单实例、模式切换保持左上角并收缩、隐藏/恢复、锁定持久化、退出进程消失及重启设置恢复。10 轮、60 次模式/可见性/锁定操作后，句柄 257 → 257；工作集 26.54 → 26.61 MiB，私有提交 6.08 → 6.07 MiB。操作阶段约 4.17 秒，平均 CPU 0.832%，不属于闲置采样。测试后恢复原显示模式、可见性与锁定状态。

Native-command integration passed duplicate launch, anchoring/shrink, visibility, lock persistence, clean exit and restart checks. Ten cycles / sixty changes completed with unchanged handle count. This short stress run is not a long-term leak test; it does not simulate hardware hotplug or physical desktop input.
