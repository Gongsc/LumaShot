# Soft Fluent icon set v2

This set redesigns all 15 capture-overlay actions on a 24 × 24 optical grid.

- Rounded 1.8 px strokes follow the softer geometry used by the WinUI 3 control center.
- The white foreground stays legible on the dark capture toolbar; the color picker uses three restrained accent dots.
- `toolbar-atlas.svg` is the 360 × 24 runtime source in the exact order expected by `CaptureOverlay`.
- `src/LumaShot.App/assets/toolbar-icons.png` is the rasterized runtime atlas.

Order: region, window, monitor, all displays, pen, rectangle, arrow, text, undo, redo, color, line width, copy, save, cancel.
