<div align="center">
  <img src="src/LumaShot.App/assets/LumaShot.png" alt="LumaShot application icon" width="128">
  <h1>LumaShot</h1>
  <p>A native Windows 11 screenshot tool for HDR and mixed HDR/SDR desktops.</p>
  <p><strong>English</strong> · <a href="README.zh-CN.md">简体中文</a></p>
  <p><code>v0.1.0</code> · Windows 11 · x64</p>
</div>

## Project status

**v0.1.0 is a usable preview under active development.** The core capture, annotation, HDR calibration, clipboard, and export workflows are implemented and covered by automated tests plus real Windows Graphics Capture smoke tests. Release binaries are currently unsigned.

## Highlights

- Region, window, current-display, and all-display capture
- Native 16-bit-float scRGB capture with mixed HDR/SDR and mixed-DPI support
- Full-screen HDR-to-SDR calibration with a movable live-preview panel
- Pen, rectangle, arrow, and IME-aware text annotations with undo/redo
- Lossless HDR JPEG XR and tone-mapped sRGB PNG output
- SDR-compatible PNG, `CF_DIBV5`, and `CF_DIB` clipboard data
- System tray, configurable global hotkey, and optional `Enter`-to-copy
- Simplified Chinese and English interface

## Use

Launch `LumaShot.exe`; the app remains in the notification area. Double-click its tray icon or press the global hotkey (`Ctrl+Shift+PrintScreen` by default), select a capture mode, adjust or annotate the selection, then copy or save it. HDR selections default to JPEG XR; PNG and clipboard output are converted to SDR for broad application compatibility.

Keyboard shortcuts: `Enter` copy when enabled, `Ctrl+C` copy, `Ctrl+S` save, `Ctrl+Z` undo, `Ctrl+Y` redo, and `Esc` cancel.

## Requirements and build

- Windows 11 22H2 or later, x64
- Visual Studio 2022 Build Tools with Desktop development with C++ and the Windows 11 SDK
- Inno Setup 6 only when building the installer

```powershell
.\scripts\build.ps1 -Configuration Release -RunTests
.\scripts\package.ps1
```

See the [HDR guide](docs/HDR-Guide.md) and [test matrix](docs/TESTING.md) for technical details. Protected or DRM content and the Windows secure desktop may appear black; LumaShot does not bypass operating-system capture restrictions.
