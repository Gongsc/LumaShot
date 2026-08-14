# HDRSnapshot

HDRSnapshot is a native Windows 11 screenshot tool designed for HDR and mixed HDR/SDR desktops. It captures the desktop in linear 16-bit-float scRGB, can save lossless HDR JPEG XR files, and produces tone-mapped sRGB PNG/clipboard images for compatibility.

HDRSnapshot 是一款面向 Windows 11 的原生 HDR 截图工具。它以线性 16 位浮点 scRGB 捕获 HDR/SDR 混合桌面，可保存保留高光的无损 JPEG XR，也可生成兼容普通软件的色调映射 PNG 和剪贴板图像。

## Features

- Region, window, current-display, and all-display capture modes
- System tray and configurable global hotkey (`Ctrl+Shift+PrintScreen` by default)
- Pen, rectangle, arrow, and IME-aware text annotations with undo/redo
- Lossless `.jxr` output retaining values above SDR white
- Tone-mapped `.png`, `PNG`, `CF_DIBV5`, and `CF_DIB` clipboard output
- Simplified Chinese and English UI
- Per-monitor-v2 DPI and negative-coordinate/mixed-scale display handling
- Annotation color and line-width controls
- Fluent Design settings cards and compact vector-icon capture toolbars with instant hover labels

## Requirements and build

- Windows 11 22H2 or later, x64
- Visual Studio 2022 Build Tools with Desktop development with C++ and Windows 11 SDK
- Inno Setup 6 (only needed for the installer)

Build and test from a PowerShell prompt:

```powershell
.\scripts\build.ps1 -Configuration Release -RunTests
```

Create the portable ZIP and, when Inno Setup is installed, the installer:

```powershell
.\scripts\package.ps1
```

Unsigned builds can trigger Microsoft Defender SmartScreen. Pass a trusted Authenticode certificate thumbprint to `package.ps1` for signed releases.

## Usage

Launch `HDRSnapshot.exe`; it stays in the notification area. Double-click the icon or use the global hotkey. Choose a capture mode, make or adjust the selection, optionally annotate, then select Copy or Save. HDR selections default to JPEG XR; selecting PNG always creates an SDR-compatible tone-mapped image. Clipboard output is intentionally SDR for application compatibility.

启动 `HDRSnapshot.exe` 后，程序常驻通知区域。双击托盘图标或使用全局快捷键，选择截图模式并调整选区，可添加画笔、矩形、箭头或文字标注，最后选择“复制”或“保存”。HDR 选区默认保存为 JPEG XR；PNG 和剪贴板始终输出经过色调映射的 SDR 图像，以兼容画图、Office 和聊天软件。

快捷键：`Esc` 取消，`Ctrl+C` 复制，`Ctrl+S` 保存，`Ctrl+Z` 撤销，`Ctrl+Y` 重做。完整的 HDR 使用与验证说明见 [`docs/HDR-Guide.md`](docs/HDR-Guide.md)，测试矩阵见 [`docs/TESTING.md`](docs/TESTING.md)。

Protected/DRM content and the Windows secure desktop may appear black. HDRSnapshot does not attempt to bypass operating-system capture restrictions.
