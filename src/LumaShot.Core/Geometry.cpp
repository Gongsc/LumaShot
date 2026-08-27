#include <LumaShot/Geometry.h>
#include <algorithm>

namespace lumashot {

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

RectI PlaceMagnifier(POINT cursor, RectI bounds, int width, int height, int gap, int margin) noexcept {
    bounds = NormalizeRect(bounds);
    width = std::max(1, width);
    height = std::max(1, height);
    gap = std::max(0, gap);
    margin = std::max(0, margin);

    const int availableWidth = std::max(1, bounds.width() - margin * 2);
    const int availableHeight = std::max(1, bounds.height() - margin * 2);
    width = std::min(width, availableWidth);
    height = std::min(height, availableHeight);

    const int minimumX = bounds.left + margin;
    const int minimumY = bounds.top + margin;
    const int maximumX = std::max(minimumX, bounds.right - margin - width);
    const int maximumY = std::max(minimumY, bounds.bottom - margin - height);
    const int right = cursor.x + gap;
    const int left = cursor.x - gap - width;
    const int below = cursor.y + gap;
    const int above = cursor.y - gap - height;

    const int x = right <= maximumX ? right : left >= minimumX ? left : std::clamp(right, minimumX, maximumX);
    const int y = below <= maximumY ? below : above >= minimumY ? above : std::clamp(below, minimumY, maximumY);
    return {x, y, x + width, y + height};
}

POINT MapPointBetweenRects(POINT point, RectI sourceBounds, RectI targetBounds) noexcept {
    const int sourceWidth = sourceBounds.width();
    const int sourceHeight = sourceBounds.height();
    if (sourceWidth <= 0 || sourceHeight <= 0) return POINT{targetBounds.left, targetBounds.top};
    const auto map = [](int value, int sourceStart, int sourceSpan, int targetStart, int targetSpan) {
        return targetStart + static_cast<int>(static_cast<std::int64_t>(value - sourceStart) * targetSpan / sourceSpan);
    };
    return {map(point.x, sourceBounds.left, sourceWidth, targetBounds.left, targetBounds.width()),
            map(point.y, sourceBounds.top, sourceHeight, targetBounds.top, targetBounds.height())};
}

RectI MapRectBetweenRects(RectI value, RectI sourceBounds, RectI targetBounds) noexcept {
    value = NormalizeRect(value);
    const POINT topLeft = MapPointBetweenRects({value.left, value.top}, sourceBounds, targetBounds);
    const POINT bottomRight = MapPointBetweenRects({value.right, value.bottom}, sourceBounds, targetBounds);
    return {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

RECT ToWin32Rect(RectI value) noexcept { return RECT{value.left, value.top, value.right, value.bottom}; }
RectI FromWin32Rect(const RECT& value) noexcept { return RectI{value.left, value.top, value.right, value.bottom}; }

bool CaptureFrameSet::containsHdr(const RectI& selection) const noexcept {
    for (const auto& frame : monitors) {
        if (frame.hdrEnabled && !IntersectRectangles(frame.desktopRect, selection).empty()) return true;
    }
    return false;
}

} // namespace lumashot
