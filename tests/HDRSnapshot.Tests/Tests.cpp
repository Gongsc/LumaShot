#include "ColorPipeline.h"
#include "CaptureService.h"
#include "AnnotationRenderer.h"
#include "ImageExporter.h"
#include "ClipboardService.h"
#include <HDRSnapshot/AnnotationDocument.h>
#include <HDRSnapshot/Geometry.h>
#include <HDRSnapshot/Localization.h>
#include <HDRSnapshot/SettingsStore.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace hdrsnapshot;

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
    document.add(TextAnnotation{{3, 3}, L"HDR", {255, 255, 255, 255}, 14});
    Check(document.items().size() == 4 && document.canUndo(), "adds all annotation types");
    Check(document.undo() && document.items().size() == 3 && document.canRedo(), "undoes annotation");
    Check(document.redo() && document.items().size() == 4, "redoes annotation");
    Check(!Localized(StringId::Capture, Language::SimplifiedChinese).empty() && Localized(StringId::Capture, Language::English) == L"Capture", "loads Chinese and English resources");
    Check(Localized(StringId::SettingsSubtitle, Language::English) == L"Personalize your capture experience" &&
          !Localized(StringId::CaptureControls, Language::SimplifiedChinese).empty() &&
          !Localized(StringId::Behavior, Language::SimplifiedChinese).empty(),
          "loads Fluent settings resources");
}

void SettingsTests(const std::filesystem::path& base) {
    const auto path = base / L"settings.json";
    SettingsStore store(path);
    AppSettings settings; settings.language = Language::English; settings.lastCaptureMode = CaptureMode::VirtualDesktop;
    settings.includeCursor = true; settings.hotkey.virtualKey = 'Q';
    Check(store.save(settings), "saves settings atomically");
    const auto loaded = store.load();
    Check(loaded.language == Language::English && loaded.lastCaptureMode == CaptureMode::VirtualDesktop && loaded.includeCursor && loaded.hotkey.virtualKey == 'Q', "loads settings round trip");
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
    CaptureFrameSet set; set.virtualDesktop = {-2, 0, 2, 1};
    MonitorFrame left; left.desktopRect = {-2, 0, 0, 1}; left.width = 2; left.height = 1; left.rgba16f.assign(8, ColorPipeline::floatToHalf(0.25f));
    MonitorFrame right; right.desktopRect = {0, 0, 2, 1}; right.width = 2; right.height = 1; right.hdrEnabled = true; right.rgba16f.assign(8, ColorPipeline::floatToHalf(2.0f));
    set.monitors = {left, right};
    const auto composed = ColorPipeline::compose(set, {-1, 0, 1, 1});
    Check(composed.width == 2 && composed.hdr, "composes mixed HDR displays across negative origin");
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
                std::cout << "monitor texture=" << frame.width << 'x' << frame.height
                          << " desktop=" << frame.desktopRect.width() << 'x' << frame.desktopRect.height()
                          << " hdr=" << frame.hdrEnabled << " peak=" << frame.maxLuminanceNits
                          << " white=" << frame.sdrWhiteLevelNits << '\n';
            }
            if (argc > 2 && !frames.monitors.empty()) {
                const auto source = ColorPipeline::compose(frames, frames.monitors.front().desktopRect);
                ImageExporter::savePng(std::filesystem::path(argv[2]), ColorPipeline::toneMapToSdr(source));
                std::cout << "wrote tone-map diagnostic to " << argv[2] << '\n';
            }
        } catch (const std::exception& error) { ++failures; std::cerr << "CAPTURE: " << error.what() << '\n'; }
        CoUninitialize();
        if (failures) return 1;
        std::cout << "Windows Graphics Capture smoke test passed\n";
        return 0;
    }
    const auto base = std::filesystem::temp_directory_path() / (L"HDRSnapshotTests_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(base);
    try { GeometryTests(); AnnotationTests(); SettingsTests(base); ColorTests(); CodecTests(base); }
    catch (const std::exception& error) { ++failures; std::cerr << "UNEXPECTED: " << error.what() << '\n'; }
    std::filesystem::remove(base);
    CoUninitialize();
    if (failures) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "All HDRSnapshot tests passed\n"; return 0;
}
