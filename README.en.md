# DeskPerch

[中文](README.md) | [English](README.en.md)

<img src="assets/deskperch.png" width="88" alt="DeskPerch logo">

A quiet, lightweight peripheral-status widget for the Windows desktop. Built with C++20, native Win32 and GDI+, with no installer, account, administrator privileges or additional runtime required.

[Latest release](https://github.com/WXH666-bit/DeskPerch/releases/latest) · [Actions](https://github.com/WXH666-bit/DeskPerch/actions) · [Device support](docs/SUPPORT.md) · [Validation record](docs/VALIDATION.md)

## Features

- Full view: one row per external mouse (battery and real DPI), keyboard, display and USB port.
- Compact view: battery and DPI per mouse, with names when multiple mice are shown. Long lists have wheel scrolling and a narrow scrollbar.
- Hosted in Explorer's desktop icon view, below ordinary apps, without a taskbar button, Alt+Tab entry or automatic focus activation.
- Recognized built-in keyboards, touchpads, integrated panels and internal USB ports are excluded.
- Clear and pale-white backgrounds; pale white is the first-launch default, then the user's choice is restored. Dark text, bold values and adapted light outlines improve legibility.
- Tray-only controls for background, layout, position locking, device selection, visibility, startup and exit.
- Saved position/preferences, multi-monitor position recovery and one instance per user session.

No driver installation, hardware tuning, DPI modification, ads, telemetry or network functionality. Styling uses static transparency without continuous wallpaper capture, real-time refraction or idle animation.

## Getting started

Download the portable ZIP from Releases, extract it and run `DeskPerch.exe`. The standalone EXE also works. On first launch the widget appears near the upper-right corner of the primary monitor's work area.

Right-click the system tray icon (possibly in the overflow area) for settings. Drag the widget while unlocked. No checked objects means automatic display of external peripherals and occupied external ports; idle ports and hub devices are hidden. The first check enters global manual mode: only checked objects are shown, across all categories. Uncheck the last object or choose “自动显示已连接设备” to restore automatic mode. Disconnected selections retain their names. Compact view never adds an unselected mouse. Hide keeps the tray; Exit ends the process. Startup is off by default; after moving the EXE, toggle startup off/on to update the path.

Target: Windows 11 24H2/25H2 x64. Actual hardware testing has used Windows 11 25H2; other builds, wallpaper tools and monitor combinations need validation. The EXE is unsigned. The application UI is currently Chinese; documentation is bilingual.

## Data and privacy

| Display | Meaning |
| --- | --- |
| Battery / DPI | Unexpired data actually reported by the device |
| 未连接 — Not connected | Confirmed absence; connecting every device category is optional |
| 暂不支持 — Unsupported | No supported capability was found |
| 暂不可用 / 状态未知 — Unavailable / Unknown | Failed query, timeout or unconfirmed wireless presence |
| 状态已过期 — Expired | An old reading is no longer presented as current |

Unknown battery is never 0%; pointer speed or presets never replace real DPI. Enabled display paths do not imply physical power. USB port numbers may differ from case labels.

Other brands use standard HID Feature / Bluetooth BAS percentage reports; unverified DPI protocols remain unsupported. Only positively identified batteryless wired models display `-`; USB attachment alone is not proof. See the support table.

Configuration v3 migrates v1/v2 attention filters to automatic mode while keeping appearance, position, locking, visibility and layout. Subsequent starts restore multiple selections and offline names.

Preferences are stored in `%LOCALAPPDATA%\DeskPerch\settings.dat`. No typed input, pointer movement, other app content or device-state history is recorded.

## Build and package

Requires Visual Studio 2022 with Desktop development with C++, Windows SDK, CMake 3.24+ and PowerShell.

```powershell
.\build.ps1
.\tools\package.ps1 -Version v1.0.0
```

The build runs CTest and writes `out/DeskPerch.exe`; exit that EXE before rebuilding. Packaging writes a portable ZIP, standalone EXE and SHA-256 checksums to `dist`. MSVC is linked statically; only Windows DLLs are required.

The probe and measurement tools are optional development utilities. Unit tests do not establish real-world desktop behavior or 24-hour stability. Local diagnostics, screenshots, raw measurements and development-session reports are excluded from the repository.

## Actions and Releases

Push to `main`, open a PR or manually run the workflow to build, test and retain packaged Actions artifacts. Push a `vX.Y.Z` tag to automatically publish a Release after tests pass, including the portable ZIP, EXE and `SHA256SUMS.txt`.

```powershell
git tag v1.0.0
git push origin v1.0.0
```

Publishing uses the built-in `GITHUB_TOKEN`, with no additional token setup. Use a new version tag per release; existing assets are not overwritten. Checksums cover uploaded binaries and ZIP, not the separate source archives generated by GitHub.
