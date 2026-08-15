#pragma once
#include "Types.h"

namespace lumashot {
[[nodiscard]] RectI NormalizeRect(RectI value) noexcept;
[[nodiscard]] RectI IntersectRectangles(RectI first, RectI second) noexcept;
[[nodiscard]] RectI ClampRect(RectI value, RectI bounds) noexcept;
[[nodiscard]] bool ContainsPoint(RectI value, int x, int y) noexcept;
[[nodiscard]] RectI PlaceToolbar(RectI selection, RectI monitorBounds, int width, int height, int margin = 8) noexcept;
[[nodiscard]] POINT MapPointBetweenRects(POINT point, RectI sourceBounds, RectI targetBounds) noexcept;
[[nodiscard]] RectI MapRectBetweenRects(RectI value, RectI sourceBounds, RectI targetBounds) noexcept;
[[nodiscard]] RECT ToWin32Rect(RectI value) noexcept;
[[nodiscard]] RectI FromWin32Rect(const RECT& value) noexcept;
} // namespace lumashot
