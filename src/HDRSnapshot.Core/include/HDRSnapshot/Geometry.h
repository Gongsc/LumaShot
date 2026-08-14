#pragma once
#include "Types.h"

namespace hdrsnapshot {
[[nodiscard]] RectI NormalizeRect(RectI value) noexcept;
[[nodiscard]] RectI IntersectRectangles(RectI first, RectI second) noexcept;
[[nodiscard]] RectI ClampRect(RectI value, RectI bounds) noexcept;
[[nodiscard]] bool ContainsPoint(RectI value, int x, int y) noexcept;
[[nodiscard]] RECT ToWin32Rect(RectI value) noexcept;
[[nodiscard]] RectI FromWin32Rect(const RECT& value) noexcept;
} // namespace hdrsnapshot

