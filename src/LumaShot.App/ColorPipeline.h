#pragma once
#include <LumaShot/Types.h>
#include <cstdint>
#include <vector>

namespace lumashot {

struct ImageToneRegion {
    RectI pixels;
    bool hdr{};
    float sdrWhiteLevelNits{80.0f};
};

struct ImageF16 {
    UINT width{};
    UINT height{};
    std::vector<std::uint16_t> rgba;
    bool hdr{};
    float maxLuminanceNits{80.0f};
    float sdrWhiteLevelNits{80.0f};
    // Regions are ordered like composition layers; the last matching region
    // owns a pixel. This keeps per-display SDR white levels for mixed desktops.
    std::vector<ImageToneRegion> toneRegions;
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
