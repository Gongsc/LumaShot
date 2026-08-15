# Test and acceptance matrix

## Automated

Run from PowerShell:

```powershell
.\scripts\build.ps1 -Configuration Release -RunTests
.\bin\Release\LumaShot.Tests.exe --capture-smoke
```

The test executable covers negative-coordinate geometry and cropping, per-display SDR-white normalization on mixed SDR/HDR desktops, half-float values above `1.0`, Direct2D/CPU SDR conversion, hue-preserving highlight compression, all annotation variants and undo/redo, English/Chinese resources, settings migration and corrupt-file recovery, PNG encoding, lossless floating-point JPEG XR round trips, and the `CF_DIBV5`/`CF_DIB` payload structures. `--capture-smoke` performs a real Windows Graphics Capture session, validates the returned `R16G16B16A16_FLOAT` frames, and reports each display's HDR peak, SDR white level, and sampled content luminance.

## Windows 11 manual acceptance

Repeat the following on single-SDR, single-HDR, and mixed HDR/SDR desktops, including 100%, 150%, and 200% display scaling where available:

- Exercise region, window, current-display, and all-display modes; include a cross-display region and a display with a negative virtual origin.
- Move and resize the selection using every edge and corner. Confirm the cursor changes to four-way move, horizontal/vertical resize, and the matching diagonal resize cursor before and throughout each drag, and that output dimensions exactly match the selected physical pixels.
- Draw pen, rectangle, arrow, and Chinese/English IME text annotations; cycle color and width; test undo and redo.
- In Settings, confirm the HDR section contains only Start calibration—no embedded sliders or thumbnail. Start calibration on an HDR desktop and verify fine text and one-pixel edges remain sharp at each display's native physical resolution. Drag and reposition the translucent panel, adjust both sliders, and verify the full-screen capture updates continuously. Confirm Reset restores defaults, Cancel/`Esc` discards changes, and Apply persists them for later PNG and clipboard output. On an SDR-only desktop, confirm the app reports that calibration is unavailable.
- Confirm the overlay and toolbar never appear in output, with and without cursor capture.
- Save PNG and compare it with an Xbox Game Bar capture of the same HDR frame; verify similar SDR white, midtone contrast, saturation, and highlight clipping. Save JXR from an HDR display and verify highlights in Windows Photos; decode it and confirm a channel value above `1.0` where the source contains HDR highlights.
- Paste into Paint, Office, and representative chat applications. Confirm PNG, `CF_DIBV5`, and `CF_DIB` consumers receive the SDR image.
- Exercise clipboard contention, a read-only save target, a window closing during capture, display topology changes, and graphics-device reset/recovery.
- Confirm protected or DRM content may be black and that the application does not attempt to bypass the operating-system policy.
