# Modern white icon concept v1

- 15 standalone SVG candidates corresponding to every capture-overlay button.
- 24 × 24 optical grid, exported at 64 × 64 while retaining vector precision.
- White strokes, transparent canvas, rounded caps and joins.
- `toolbar-atlas.svg` defines the runtime order. Its transparent rasterization is embedded as `src/LumaShot.App/assets/toolbar-icons.png`.
- The capture overlay now uses this set; the former GDI drawings remain only as a resource-load failure fallback.
