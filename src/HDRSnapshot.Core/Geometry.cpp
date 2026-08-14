#include <HDRSnapshot/Geometry.h>
#include <algorithm>

namespace hdrsnapshot {

RectI NormalizeRect(RectI value) noexcept {
    if (value.left > value.right) std::swap(value.left, value.right);
    if (value.top > value.bottom) std::swap(value.top, value.bottom);
    return value;
}

RectI IntersectRectangles(RectI first, RectI second) noexcept {
    RectI result{std::max(first.left, second.left), std::max(first.top, second.top),
                 std::min(first.right, second.right), std::min(first.bottom, second.bottom)};
    return result.empty() ? RectI{} : result;
}

RectI ClampRect(RectI value, RectI bounds) noexcept {
    value = NormalizeRect(value);
    value.left = std::clamp(value.left, bounds.left, bounds.right);
    value.top = std::clamp(value.top, bounds.top, bounds.bottom);
    value.right = std::clamp(value.right, bounds.left, bounds.right);
    value.bottom = std::clamp(value.bottom, bounds.top, bounds.bottom);
    return value;
}

bool ContainsPoint(RectI value, int x, int y) noexcept {
    return x >= value.left && x < value.right && y >= value.top && y < value.bottom;
}

RectI PlaceToolbar(RectI selection, RectI monitorBounds, int width, int height, int margin) noexcept {
    width = std::max(1, width);
    height = std::max(1, height);
    margin = std::max(0, margin);
    RectI visibleSelection = IntersectRectangles(NormalizeRect(selection), monitorBounds);
    if (visibleSelection.empty()) visibleSelection = monitorBounds;

    const int availableLeft = monitorBounds.left + margin;
    const int availableTop = monitorBounds.top + margin;
    const int availableRight = std::max(availableLeft, monitorBounds.right - margin - width);
    const int availableBottom = std::max(availableTop, monitorBounds.bottom - margin - height);
    const int centeredX = visibleSelection.left + (visibleSelection.width() - width) / 2;
    const int x = std::clamp(centeredX, availableLeft, availableRight);

    const int below = visibleSelection.bottom + margin;
    const int above = visibleSelection.top - height - margin;
    int y{};
    if (below <= availableBottom) y = below;
    else if (above >= availableTop) y = above;
    else y = std::clamp(visibleSelection.bottom - height - margin, availableTop, availableBottom);
    return {x, y, x + width, y + height};
}

RECT ToWin32Rect(RectI value) noexcept { return RECT{value.left, value.top, value.right, value.bottom}; }
RectI FromWin32Rect(const RECT& value) noexcept { return RectI{value.left, value.top, value.right, value.bottom}; }

bool CaptureFrameSet::containsHdr(const RectI& selection) const noexcept {
    for (const auto& frame : monitors) {
        if (frame.hdrEnabled && !IntersectRectangles(frame.desktopRect, selection).empty()) return true;
    }
    return false;
}

} // namespace hdrsnapshot
