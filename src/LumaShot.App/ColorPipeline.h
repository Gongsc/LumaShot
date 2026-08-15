#pragma once
#include <LumaShot/Types.h>
#include <cstdint>
#include <vector>

namespace lumashot {

struct ImageF16 {
    UINT width{};
    UINT height{};
    std::vector<std::uint16_t> rgba;
    bool hdr{};
    float maxLuminanceNits{80.0f};
    float sdrWhiteLevelNits{80.0f};
};

struct ImageBgra8 {
    UINT width{};
    UINT height{};
    std::vector<std::uint8_t> pixels;
};

class ColorPipeline {
public:
    [[nodiscard]] static ImageF16 compose(const CaptureFrameSet& frames, RectI selection);
    [[nodiscard]] static ImageF16 thumbnail(const ImageF16& source, UINT maximumWidth, UINT maximumHeight);
    [[nodiscard]] static ImageBgra8 toneMapToSdr(const ImageF16& source, HdrCalibration calibration = {});
    [[nodiscard]] static float halfToFloat(std::uint16_t value) noexcept;
    [[nodiscard]] static std::uint16_t floatToHalf(float value) noexcept;
};

} // namespace lumashot
