#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace lumashot {

enum class CaptureMode { Region, Window, Monitor, VirtualDesktop };
enum class ExportFormat { PngSdr, JpegXrHdr };
enum class Language { Automatic, SimplifiedChinese, English };
enum class AnnotationTool { None, Pen, Rectangle, Arrow, Text };

struct PointF {
    float x{};
    float y{};
};

struct RectI {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] int width() const noexcept { return right - left; }
    [[nodiscard]] int height() const noexcept { return bottom - top; }
    [[nodiscard]] bool empty() const noexcept { return width() <= 0 || height() <= 0; }
};

struct ColorRgba {
    std::uint8_t r{255};
    std::uint8_t g{48};
    std::uint8_t b{48};
    std::uint8_t a{255};
};

struct PenStroke {
    std::vector<PointF> points;
    ColorRgba color;
    float width{3.0f};
};

struct RectangleAnnotation {
    PointF start;
    PointF end;
    ColorRgba color;
    float width{3.0f};
};

struct ArrowAnnotation {
    PointF start;
    PointF end;
    ColorRgba color;
    float width{3.0f};
};

struct TextAnnotation {
    PointF origin;
    std::wstring text;
    ColorRgba color;
    float fontSize{20.0f};
};

using Annotation = std::variant<PenStroke, RectangleAnnotation, ArrowAnnotation, TextAnnotation>;

struct MonitorFrame {
    HMONITOR monitor{};
    RectI desktopRect;
    UINT width{};
    UINT height{};
    bool hdrEnabled{};
    float maxLuminanceNits{80.0f};
    float sdrWhiteLevelNits{80.0f};
    std::int64_t systemRelativeTime{};
    // DXGI_FORMAT_R16G16B16A16_FLOAT, tightly packed RGBA half floats.
    std::vector<std::uint16_t> rgba16f;
};

struct CaptureFrameSet {
    RectI virtualDesktop;
    std::vector<MonitorFrame> monitors;
    [[nodiscard]] bool containsHdr(const RectI& selection) const noexcept;
};

struct HotkeyBinding {
    UINT modifiers{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT};
    UINT virtualKey{VK_SNAPSHOT};
};

struct HdrCalibration {
    static constexpr int MinimumOutputBrightness = 40;
    static constexpr int MaximumOutputBrightness = 110;
    static constexpr int MinimumHighlightCompression = 0;
    static constexpr int MaximumHighlightCompression = 100;
    static constexpr int DefaultOutputBrightness = 100;
    static constexpr int DefaultHighlightCompression = 0;

    int outputBrightnessPercent{DefaultOutputBrightness};
    int highlightCompressionPercent{DefaultHighlightCompression};
};

struct AppSettings {
    static constexpr int CurrentSchemaVersion = 3;
    int schemaVersion{CurrentSchemaVersion};
    Language language{Language::Automatic};
    HotkeyBinding hotkey;
    CaptureMode lastCaptureMode{CaptureMode::Region};
    bool includeCursor{false};
    bool launchAtLogin{false};
    HdrCalibration hdrCalibration;
};

} // namespace lumashot
