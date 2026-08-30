#include "ColorPipeline.h"
#include "CaptureService.h"
#include "AnnotationRenderer.h"
#include "ImageExporter.h"
#include "ClipboardService.h"
#include <LumaShot/AnnotationDocument.h>
#include <LumaShot/Geometry.h>
#include <LumaShot/Localization.h>
#include <LumaShot/SettingsStore.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace lumashot;

namespace {
int failures{};

void Check(bool condition, const char* description) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}

void GeometryTests() {
    Check(NormalizeRect({8, 9, -2, -3}).left == -2, "normalizes negative rectangle");
    const auto intersection = IntersectRectangles({-100, -50, 100, 50}, {0, 0, 200, 200});
    Check(intersection.left == 0 && intersection.top == 0 && intersection.width() == 100 && intersection.height() == 50, "intersects virtual desktop coordinates");
    const auto clamped = ClampRect({-20, -20, 200, 200}, {0, 0, 100, 100});
    Check(clamped.left == 0 && clamped.right == 100, "clamps selection to desktop");
    const auto monitorToolbar = PlaceToolbar({0, 0, 3840, 2160}, {0, 0, 3840, 2160}, 524, 44);
    Check(monitorToolbar.left == 1658 && monitorToolbar.top == 2108 && monitorToolbar.bottom == 2152,
          "keeps current-display toolbar inside the physical monitor");
    const auto mixedDesktopToolbar = PlaceToolbar({0, -840, 6000, 3000}, {0, 0, 3840, 2160}, 524, 44);
    Check(mixedDesktopToolbar.left == 1658 && mixedDesktopToolbar.bottom <= 2160,
          "does not place toolbar in a mixed-monitor virtual desktop gap");
    const auto regionToolbar = PlaceToolbar({100, 100, 800, 600}, {0, 0, 1920, 1080}, 524, 44);
    Check(regionToolbar.top == 608 && regionToolbar.left == 188,
          "places a region toolbar below the selection when real monitor space exists");
    const auto magnifier = PlaceMagnifier({400, 300}, {0, 0, 1920, 1080}, 146, 146);
    Check(magnifier.left == 420 && magnifier.top == 320,
          "places the magnifier below and to the right of the pointer when space exists");
    const auto flippedMagnifier = PlaceMagnifier({1900, 1060}, {0, 0, 1920, 1080}, 146, 146);
    Check(flippedMagnifier.left == 1734 && flippedMagnifier.top == 894 &&
              flippedMagnifier.right <= 1912 && flippedMagnifier.bottom <= 1072,
          "keeps the magnifier on screen without covering the pointer near an edge");
    const RectI desktop{-2160, -1145, 3840, 2695};
    const RectI overlayClient{0, 0, 4800, 3072};
    const POINT desktopPoint = MapPointBetweenRects({2400, 1536}, overlayClient, desktop);
    const POINT clientPoint = MapPointBetweenRects(desktopPoint, desktop, overlayClient);
    Check(desktopPoint.x == 840 && desktopPoint.y == 775 && clientPoint.x == 2400 && clientPoint.y == 1536,
          "maps overlay coordinates to physical mixed-DPI desktop coordinates without DPI virtualization");
    const RectI mappedSelection = MapRectBetweenRects({-1080, -572, 1920, 1348}, desktop, overlayClient);
    Check(mappedSelection.left == 864 && mappedSelection.top == 458 &&
          mappedSelection.right == 3264 && mappedSelection.bottom == 1994,
          "maps both region-selection edges through the same physical-pixel transform");
}

void AnnotationTests() {
    AnnotationDocument document;
    document.add(PenStroke{{{0, 0}, {5, 5}}, {255, 0, 0, 255}, 2});
    document.add(ArrowAnnotation{{2, 2}, {8, 8}, {0, 255, 0, 255}, 4});
    document.add(RectangleAnnotation{{1, 1}, {10, 10}, {255, 0, 0, 255}, 2});
    document.add(TextAnnotation{{3, 3}, L"HDR", {255, 255, 255, 255}, 24, L"Consolas"});
    Check(document.items().size() == 4 && document.canUndo() &&
          std::get<TextAnnotation>(document.items()[3]).fontSize == 24 &&
          std::get<TextAnnotation>(document.items()[3]).fontFamily == L"Consolas",
          "adds all annotation types and preserves text formatting");
    Check(document.replace(3, TextAnnotation{{30, 20}, L"HDR", {255, 255, 255, 255}, 24, L"Consolas"}) &&
          std::get<TextAnnotation>(document.items()[3]).origin.x == 30,
          "moves an existing text annotation");
    Check(document.undo() && std::get<TextAnnotation>(document.items()[3]).origin.x == 3,
          "undoes text annotation movement");
    Check(document.redo() && std::get<TextAnnotation>(document.items()[3]).origin.x == 30,
          "redoes text annotation movement");
    Check(document.undo() && std::get<TextAnnotation>(document.items()[3]).origin.x == 3,
          "undoes text annotation movement after redo");
    Check(document.undo() && document.items().size() == 3 && document.canRedo(), "undoes annotation");
    Check(document.redo() && document.items().size() == 4, "redoes annotation");
    Check(!Localized(StringId::Capture, Language::SimplifiedChinese).empty() && Localized(StringId::Capture, Language::English) == L"Capture", "loads Chinese and English resources");
    Check(Localized(StringId::Font, Language::SimplifiedChinese) == L"字体" &&
          Localized(StringId::FontSize, Language::English) == L"Size",
          "loads text formatting resources");
    Check(Localized(StringId::SettingsSubtitle, Language::English) == L"Personalize your capture experience" &&
          !Localized(StringId::CaptureControls, Language::SimplifiedChinese).empty() &&
          !Localized(StringId::Behavior, Language::SimplifiedChinese).empty() &&
          !Localized(StringId::HdrCalibrationTitle, Language::SimplifiedChinese).empty() &&
          Localized(StringId::StartCalibration, Language::English) == L"Start calibration" &&
          Localized(StringId::CopyOnEnter, Language::English) == L"Press Enter to copy screenshot" &&
          !Localized(StringId::CalibrationInstructions, Language::SimplifiedChinese).empty() &&
          !Localized(StringId::HdrCalibrationUnavailable, Language::English).empty() &&
          !Localized(StringId::HdrOutputBrightness, Language::English).empty(),
          "loads Fluent settings resources");
}

void SettingsTests(const std::filesystem::path& base) {
    const auto path = base / L"settings.json";
    SettingsStore store(path);
    AppSettings settings; settings.language = Language::English; settings.lastCaptureMode = CaptureMode::VirtualDesktop;
    settings.includeCursor = true; settings.copyOnEnter = false; settings.hotkey.virtualKey = 'Q';
    settings.hdrCalibration.outputBrightnessPercent = 72;
    settings.hdrCalibration.highlightCompressionPercent = 61;
    Check(store.save(settings), "saves settings atomically");
    const auto loaded = store.load();
    Check(loaded.language == Language::English && loaded.lastCaptureMode == CaptureMode::VirtualDesktop && loaded.includeCursor && !loaded.copyOnEnter &&
          loaded.hotkey.virtualKey == 'Q' && loaded.hdrCalibration.outputBrightnessPercent == 72 &&
          loaded.hdrCalibration.highlightCompressionPercent == 61, "loads settings round trip");
    {
        FILE* legacy{};
        _wfopen_s(&legacy, path.c_str(), L"wb");
        if (legacy) {
            fputs("{\"schemaVersion\":1,\"language\":\"en-US\",\"includeCursor\":true}", legacy);
            fclose(legacy);
        }
    }
    const auto migrated = store.load();
    Check(migrated.schemaVersion == AppSettings::CurrentSchemaVersion && migrated.language == Language::English &&
          migrated.includeCursor && migrated.copyOnEnter && migrated.hdrCalibration.outputBrightnessPercent == HdrCalibration::DefaultOutputBrightness &&
          migrated.hdrCalibration.highlightCompressionPercent == HdrCalibration::DefaultHighlightCompression,
          "migrates schema 1 settings with HDR calibration defaults");
    {
        FILE* legacyCalibration{};
        _wfopen_s(&legacyCalibration, path.c_str(), L"wb");
        if (legacyCalibration) {
            fputs("{\"schemaVersion\":2,\"hdrOutputBrightnessPercent\":40,\"hdrHighlightCompressionPercent\":80}", legacyCalibration);
            fclose(legacyCalibration);
        }
    }
    const auto recalibrated = store.load();
    Check(recalibrated.hdrCalibration.outputBrightnessPercent == HdrCalibration::DefaultOutputBrightness &&
          recalibrated.hdrCalibration.highlightCompressionPercent == HdrCalibration::DefaultHighlightCompression,
          "resets schema 2 calibration after changing HDR tone-map semantics");
    {
        FILE* corrupt{};
        _wfopen_s(&corrupt, path.c_str(), L"wb");
        if (corrupt) { fputs("{ definitely not valid json", corrupt); fclose(corrupt); }
    }
    const auto recovered = store.load();
    Check(recovered.language == Language::Automatic && recovered.hotkey.virtualKey == VK_SNAPSHOT, "recovers defaults from corrupt settings");
    std::filesystem::remove(path);
}

ImageF16 SyntheticHdr() {
    ImageF16 image; image.width = 16; image.height = 16; image.hdr = true; image.maxLuminanceNits = 1000; image.sdrWhiteLevelNits = 203;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(image.width) * image.height; ++pixel) {
        const auto offset = pixel * 4;
        image.rgba[offset] = image.rgba[offset + 1] = image.rgba[offset + 2] = ColorPipeline::floatToHalf(0.18f);
        image.rgba[offset + 3] = ColorPipeline::floatToHalf(1.0f);
    }
    const auto last = image.rgba.size() - 4;
    const auto white = last - 4;
    image.rgba[white] = image.rgba[white + 1] = image.rgba[white + 2] = ColorPipeline::floatToHalf(2.5f);
    image.rgba[last] = ColorPipeline::floatToHalf(8.0f); image.rgba[last + 1] = ColorPipeline::floatToHalf(4.0f);
    image.rgba[last + 2] = ColorPipeline::floatToHalf(2.0f);
    return image;
}

void ColorTests() {
    for (float value : {0.0f, 0.18f, 1.0f, 2.5f, 8.0f}) {
        const float decoded = ColorPipeline::halfToFloat(ColorPipeline::floatToHalf(value));
        Check(std::abs(decoded - value) < std::max(0.001f, value * 0.001f), "half float round trip");
    }
    Check(ColorPipeline::floatToHalf(1.9996f) == 0x4000u &&
          ColorPipeline::floatToHalf(-1.9996f) == 0xc000u,
          "carries half-float mantissa rounding into the exponent");
    Check(std::isnan(ColorPipeline::halfToFloat(ColorPipeline::floatToHalf(
              std::numeric_limits<float>::quiet_NaN()))) &&
          std::isinf(ColorPipeline::halfToFloat(ColorPipeline::floatToHalf(
              std::numeric_limits<float>::infinity()))),
          "preserves half-float NaN and infinity classes");
    ImageF16 ramp;
    ramp.width = 7; ramp.height = 1; ramp.hdr = true;
    ramp.maxLuminanceNits = 2000.0f; ramp.sdrWhiteLevelNits = 240.0f;
    constexpr float rampValues[]{0.03f, 0.18f, 0.54f, 1.0f, 3.0f, 10.0f, 25.0f};
    ramp.rgba.resize(ramp.width * 4);
    for (std::size_t pixel = 0; pixel < ramp.width; ++pixel) {
        const auto offset = pixel * 4;
        ramp.rgba[offset] = ramp.rgba[offset + 1] = ramp.rgba[offset + 2] = ColorPipeline::floatToHalf(rampValues[pixel]);
        ramp.rgba[offset + 3] = ColorPipeline::floatToHalf(1.0f);
    }
    const auto neutralRamp = ColorPipeline::toneMapToSdr(ramp, {100, 0});
    Check(neutralRamp.pixels[2 * 4] > 115 && neutralRamp.pixels[2 * 4] < 120 &&
          neutralRamp.pixels[4 * 4] >= 254 && neutralRamp.pixels[6 * 4] >= 254,
          "undoes 240-nit Windows SDR boost before encoding an SDR screenshot");
    const auto calibratedRamp = ColorPipeline::toneMapToSdr(ramp, {60, 80});
    Check(calibratedRamp.pixels[4 * 4] < neutralRamp.pixels[4 * 4] &&
          calibratedRamp.pixels[6 * 4] < neutralRamp.pixels[6 * 4] &&
          calibratedRamp.pixels[6 * 4] > calibratedRamp.pixels[4 * 4],
          "applies HDR output brightness and highlight compression monotonically");
    ImageF16 colorful;
    colorful.width = colorful.height = 1; colorful.hdr = true; colorful.sdrWhiteLevelNits = 80.0f;
    colorful.rgba = {ColorPipeline::floatToHalf(4.0f), ColorPipeline::floatToHalf(2.0f),
                     ColorPipeline::floatToHalf(1.0f), ColorPipeline::floatToHalf(1.0f)};
    const auto huePreserved = ColorPipeline::toneMapToSdr(colorful, {60, 100});
    const auto decodeSrgb = [](std::uint8_t value) {
        const float encoded = static_cast<float>(value) / 255.0f;
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    };
    const float mappedRed = decodeSrgb(huePreserved.pixels[2]);
    const float mappedGreen = decodeSrgb(huePreserved.pixels[1]);
    const float mappedBlue = decodeSrgb(huePreserved.pixels[0]);
    Check(std::abs(mappedRed / mappedGreen - 2.0f) < 0.03f &&
          std::abs(mappedGreen / mappedBlue - 2.0f) < 0.03f,
          "compresses HDR luminance without shifting highlight hue");
    const auto thumbnail = ColorPipeline::thumbnail(ramp, 4, 4);
    Check(thumbnail.width == 4 && thumbnail.height == 1 && thumbnail.hdr &&
          thumbnail.maxLuminanceNits == ramp.maxLuminanceNits,
          "creates a bounded HDR calibration preview without dropping metadata");
    CaptureFrameSet set; set.virtualDesktop = {-2, 0, 2, 1};
    MonitorFrame left; left.desktopRect = {-2, 0, 0, 1}; left.width = 2; left.height = 1; left.rgba16f.assign(8, ColorPipeline::floatToHalf(0.25f));
    MonitorFrame right; right.desktopRect = {0, 0, 2, 1}; right.width = 2; right.height = 1; right.hdrEnabled = true;
    right.sdrWhiteLevelNits = 240.0f; right.rgba16f.assign(8, ColorPipeline::floatToHalf(3.0f));
    set.monitors = {left, right};
    const auto composed = ColorPipeline::compose(set, {-1, 0, 1, 1});
    Check(composed.width == 2 && composed.hdr, "composes mixed HDR displays across negative origin");
    const auto mixedToneMap = ColorPipeline::toneMapToSdr(composed, {100, 0});
    Check(mixedToneMap.pixels[0] > 135 && mixedToneMap.pixels[0] < 140 && mixedToneMap.pixels[4] >= 254,
          "normalizes each display's SDR white independently on a mixed desktop");
    CaptureFrameSet scaledSet; scaledSet.virtualDesktop = {100, 100, 104, 102};
    MonitorFrame scaled; scaled.desktopRect = scaledSet.virtualDesktop; scaled.width = 2; scaled.height = 1;
    scaled.rgba16f = {ColorPipeline::floatToHalf(0.25f), 0, 0, ColorPipeline::floatToHalf(1.0f),
                     ColorPipeline::floatToHalf(0.75f), 0, 0, ColorPipeline::floatToHalf(1.0f)};
    scaledSet.monitors.push_back(scaled);
    const auto scaledCrop = ColorPipeline::compose(scaledSet, {102, 100, 104, 102});
    Check(scaledCrop.width == 2 && scaledCrop.height == 2 && std::abs(ColorPipeline::halfToFloat(scaledCrop.rgba[0]) - 0.75f) < 0.01f,
          "maps a scaled capture texture to desktop selection coordinates");
    const auto sdr = ColorPipeline::toneMapToSdr(SyntheticHdr());
    Check(sdr.pixels.size() == 16 * 16 * 4 && sdr.pixels.back() == 255, "tone maps HDR to opaque BGRA8");
    const auto whitePixel = sdr.pixels.size() - 8;
    Check(sdr.pixels[whitePixel] < 254 && sdr.pixels[whitePixel + 1] < 254 && sdr.pixels[whitePixel + 2] < 254,
          "tone maps HDR diffuse white without clipping every channel");
}

void CodecTests(const std::filesystem::path& base) {
    auto hdr = SyntheticHdr();
    const auto jxr = base / L"roundtrip.jxr";
    ImageExporter::saveJxr(jxr, hdr);
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "creates WIC factory");
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Check(SUCCEEDED(factory->CreateDecoderFromFilename(jxr.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)), "decodes JXR");
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (decoder) decoder->GetFrame(0, &frame);
    GUID format{}; if (frame) frame->GetPixelFormat(&format);
    const bool half = format == GUID_WICPixelFormat64bppRGBAHalf;
    const bool full = format == GUID_WICPixelFormat128bppRGBAFloat;
    Check(half || full, "JXR preserves a floating-point pixel format");
    float highlight{};
    if (half) {
        std::vector<std::uint16_t> pixels(16 * 16 * 4);
        if (frame) frame->CopyPixels(nullptr, 16 * 8, static_cast<UINT>(pixels.size() * sizeof(std::uint16_t)), reinterpret_cast<BYTE*>(pixels.data()));
        highlight = ColorPipeline::halfToFloat(pixels[pixels.size() - 4]);
    } else if (full) {
        std::vector<float> pixels(16 * 16 * 4);
        if (frame) frame->CopyPixels(nullptr, 16 * 16, static_cast<UINT>(pixels.size() * sizeof(float)), reinterpret_cast<BYTE*>(pixels.data()));
        highlight = pixels[pixels.size() - 4];
    }
    Check(highlight > 1.0f, "JXR preserves HDR values above one");
    frame.Reset(); decoder.Reset(); factory.Reset();
    std::filesystem::remove(jxr);

    auto sdr = ColorPipeline::toneMapToSdr(hdr);
    AnnotationDocument annotations; annotations.add(RectangleAnnotation{{0, 0}, {15, 15}, {255, 0, 0, 255}, 1});
    AnnotationRenderer::renderSdr(sdr, annotations);
    const auto png = ImageExporter::encodePng(sdr);
    Check(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "encodes annotated PNG");
    const auto clipboard = ClipboardService::buildBitmapPayload(sdr);
    const auto* v5 = reinterpret_cast<const BITMAPV5HEADER*>(clipboard.dibV5.data());
    const auto* dib = reinterpret_cast<const BITMAPINFOHEADER*>(clipboard.dib.data());
    Check(clipboard.dibV5.size() == sizeof(BITMAPV5HEADER) + sdr.pixels.size() && v5->bV5Size == sizeof(BITMAPV5HEADER) &&
          v5->bV5Height == -static_cast<LONG>(sdr.height) && v5->bV5CSType == LCS_sRGB, "builds top-down sRGB CF_DIBV5 payload");
    Check(clipboard.dib.size() == sizeof(BITMAPINFOHEADER) + sdr.pixels.size() && dib->biSize == sizeof(BITMAPINFOHEADER) &&
          dib->biHeight == static_cast<LONG>(sdr.height), "builds bottom-up CF_DIB payload");

    const auto existing = base / L"existing.png";
    {
        std::ofstream original(existing, std::ios::binary | std::ios::trunc);
        original << "keep-original";
    }
    ImageBgra8 invalid{2, 2};
    bool failed = false;
    try { ImageExporter::savePng(existing, invalid); } catch (...) { failed = true; }
    std::ifstream preserved(existing, std::ios::binary);
    const std::string preservedBytes{std::istreambuf_iterator<char>(preserved),
                                     std::istreambuf_iterator<char>()};
    Check(failed && preservedBytes == "keep-original",
          "preserves an existing destination when image encoding fails");
    preserved.close();
    ImageExporter::savePng(existing, sdr);
    std::ifstream replaced(existing, std::ios::binary);
    char signature[4]{};
    replaced.read(signature, sizeof(signature));
    Check(replaced.gcount() == sizeof(signature) &&
          static_cast<unsigned char>(signature[0]) == 0x89 &&
          signature[1] == 'P' && signature[2] == 'N' && signature[3] == 'G',
          "atomically replaces an existing destination after encoding succeeds");
    replaced.close();
    std::filesystem::remove(existing);
}
}

int main(int argc, char** argv) {
    const bool captureSmoke = argc > 1 && std::string_view(argv[1]) == "--capture-smoke";
    if (captureSmoke) SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, captureSmoke ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (captureSmoke) {
        try {
            Check(CaptureService::IsSupported(), "Windows Graphics Capture is supported");
            CaptureService service;
            const auto frames = service.captureDesktop(false, std::chrono::milliseconds(3000));
            Check(!frames.monitors.empty(), "captures at least one monitor");
            for (const auto& frame : frames.monitors) {
                Check(frame.width > 0 && frame.height > 0 && frame.rgba16f.size() == static_cast<std::size_t>(frame.width) * frame.height * 4, "captures an RGBA16F monitor frame");
                std::vector<float> sampledLuminance;
                sampledLuminance.reserve(static_cast<std::size_t>(frame.width) * frame.height / 16 + 1);
                float maximumChannel{};
                for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(frame.width) * frame.height; pixel += 16) {
                    const auto offset = pixel * 4;
                    const float red = ColorPipeline::halfToFloat(frame.rgba16f[offset]);
                    const float green = ColorPipeline::halfToFloat(frame.rgba16f[offset + 1]);
                    const float blue = ColorPipeline::halfToFloat(frame.rgba16f[offset + 2]);
                    maximumChannel = std::max({maximumChannel, red, green, blue});
                    sampledLuminance.push_back(0.2126f * red + 0.7152f * green + 0.0722f * blue);
                }
                std::sort(sampledLuminance.begin(), sampledLuminance.end());
                const auto percentile = [&](double value) {
                    const std::size_t index = std::min(sampledLuminance.size() - 1,
                        static_cast<std::size_t>(value * static_cast<double>(sampledLuminance.size() - 1)));
                    return sampledLuminance[index];
                };
                std::cout << "monitor texture=" << frame.width << 'x' << frame.height
                          << " desktop=" << frame.desktopRect.width() << 'x' << frame.desktopRect.height()
                          << " hdr=" << frame.hdrEnabled << " peak=" << frame.maxLuminanceNits
                          << " white=" << frame.sdrWhiteLevelNits << " linear-p99=" << percentile(0.99)
                          << " p9999=" << percentile(0.9999) << " max-channel=" << maximumChannel << '\n';
            }
            if (argc > 2 && !frames.monitors.empty()) {
                const auto source = ColorPipeline::compose(frames, frames.monitors.front().desktopRect);
                HdrCalibration calibration;
                if (argc > 4) {
                    calibration.outputBrightnessPercent = std::stoi(argv[3]);
                    calibration.highlightCompressionPercent = std::stoi(argv[4]);
                }
                ImageExporter::savePng(std::filesystem::path(argv[2]), ColorPipeline::toneMapToSdr(source, calibration));
                std::cout << "wrote tone-map diagnostic to " << argv[2] << '\n';
            }
        } catch (const std::exception& error) { ++failures; std::cerr << "CAPTURE: " << error.what() << '\n'; }
        CoUninitialize();
        if (failures) return 1;
        std::cout << "Windows Graphics Capture smoke test passed\n";
        return 0;
    }
    const auto base = std::filesystem::temp_directory_path() / (L"LumaShotTests_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(base);
    try { GeometryTests(); AnnotationTests(); SettingsTests(base); ColorTests(); CodecTests(base); }
    catch (const std::exception& error) { ++failures; std::cerr << "UNEXPECTED: " << error.what() << '\n'; }
    std::filesystem::remove(base);
    CoUninitialize();
    if (failures) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "All LumaShot tests passed\n"; return 0;
}
