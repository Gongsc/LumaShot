#pragma once
#include "ColorPipeline.h"

namespace lumashot {
struct ClipboardBitmapPayload {
    std::vector<std::uint8_t> dibV5;
    std::vector<std::uint8_t> dib;
};

class ClipboardService {
public:
    [[nodiscard]] static ClipboardBitmapPayload buildBitmapPayload(const ImageBgra8& image);
    [[nodiscard]] static bool copy(HWND owner, const ImageBgra8& image) noexcept;
};
} // namespace lumashot
