# Test and acceptance matrix

## Automated

Run from PowerShell:

```powershell
.\scripts\build.ps1 -Configuration Release -RunTests
.\bin\Release\HDRSnapshot.Tests.exe --capture-smoke
```

The test executable covers negative-coordinate geometry and cropping, mixed SDR/HDR composition, half-float values above `1.0`, Direct2D/CPU SDR tone mapping, all annotation variants and undo/redo, English/Chinese resources, settings round trips and corrupt-file recovery, PNG encoding, lossless floating-point JPEG XR round trips, and the `CF_DIBV5`/`CF_DIB` payload structures. `--capture-smoke` performs a real Windows Graphics Capture session and validates the returned `R16G16B16A16_FLOAT` frame dimensions.

## Windows 11 manual acceptance

Repeat the following on single-SDR, single-HDR, and mixed HDR/SDR desktops, including 100%, 150%, and 200% display scaling where available:

- Exercise region, window, current-display, and all-display modes; include a cross-display region and a display with a negative virtual origin.
- Move and resize the selection using every edge and corner. Confirm output dimensions exactly match the selected physical pixels.
- Draw pen, rectangle, arrow, and Chinese/English IME text annotations; cycle color and width; test undo and redo.
- Confirm the overlay and toolbar never appear in output, with and without cursor capture.
- Save PNG and verify normal SDR appearance. Save JXR from an HDR display and verify highlights in Windows Photos; decode it and confirm a channel value above `1.0` where the source contains HDR highlights.
- Paste into Paint, Office, and representative chat applications. Confirm PNG, `CF_DIBV5`, and `CF_DIB` consumers receive the SDR image.
- Exercise clipboard contention, a read-only save target, a window closing during capture, display topology changes, and graphics-device reset/recovery.
- Confirm protected or DRM content may be black and that the application does not attempt to bypass the operating-system policy.
