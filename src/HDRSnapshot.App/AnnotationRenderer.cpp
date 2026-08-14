#include "AnnotationRenderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace hdrsnapshot {
namespace {

D2D1_COLOR_F ToD2D(ColorRgba value) {
    return D2D1::ColorF(value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f);
}

void DrawLine(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, PointF start, PointF end, float width) {
    target->DrawLine(D2D1::Point2F(start.x, start.y), D2D1::Point2F(end.x, end.y), brush, width);
}

void RenderAnnotations(ID2D1RenderTarget* target, ID2D1Factory* factory, IDWriteFactory* writeFactory,
                       const AnnotationDocument& document) {
    for (const auto& annotation : document.items()) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            ComPtr<ID2D1SolidColorBrush> brush;
            const ColorRgba color = item.color;
            if (FAILED(target->CreateSolidColorBrush(ToD2D(color), &brush))) return;
            if constexpr (std::is_same_v<T, PenStroke>) {
                if (item.points.size() < 2) return;
                ComPtr<ID2D1PathGeometry> path;
                ComPtr<ID2D1GeometrySink> sink;
                if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
                sink->BeginFigure(D2D1::Point2F(item.points.front().x, item.points.front().y), D2D1_FIGURE_BEGIN_HOLLOW);
                for (std::size_t i = 1; i < item.points.size(); ++i) sink->AddLine(D2D1::Point2F(item.points[i].x, item.points[i].y));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (SUCCEEDED(sink->Close())) target->DrawGeometry(path.Get(), brush.Get(), item.width);
            } else if constexpr (std::is_same_v<T, RectangleAnnotation>) {
                const auto left = std::min(item.start.x, item.end.x);
                const auto top = std::min(item.start.y, item.end.y);
                const auto right = std::max(item.start.x, item.end.x);
                const auto bottom = std::max(item.start.y, item.end.y);
                target->DrawRectangle(D2D1::RectF(left, top, right, bottom), brush.Get(), item.width);
            } else if constexpr (std::is_same_v<T, ArrowAnnotation>) {
                DrawLine(target, brush.Get(), item.start, item.end, item.width);
                const float dx = item.end.x - item.start.x;
                const float dy = item.end.y - item.start.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.01f) {
                    const float ux = dx / length;
                    const float uy = dy / length;
                    const float head = std::clamp(item.width * 5.0f, 10.0f, 28.0f);
                    const float wing = head * 0.55f;
                    DrawLine(target, brush.Get(), item.end, {item.end.x - ux * head - uy * wing, item.end.y - uy * head + ux * wing}, item.width);
                    DrawLine(target, brush.Get(), item.end, {item.end.x - ux * head + uy * wing, item.end.y - uy * head - ux * wing}, item.width);
                }
            } else if constexpr (std::is_same_v<T, TextAnnotation>) {
                if (item.text.empty()) return;
                wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
                GetUserDefaultLocaleName(locale, static_cast<int>(std::size(locale)));
                ComPtr<IDWriteTextFormat> format;
                if (FAILED(writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, item.fontSize, locale, &format))) return;
                target->DrawTextW(item.text.c_str(), static_cast<UINT32>(item.text.size()), format.Get(),
                    D2D1::RectF(item.origin.x, item.origin.y, item.origin.x + 1200.0f, item.origin.y + item.fontSize * 8.0f), brush.Get());
            }
        }, annotation);
    }
}

ImageBgra8 RenderToBitmap(UINT width, UINT height, const std::vector<std::uint8_t>& initial,
                          const AnnotationDocument& annotations, bool transparent) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) throw std::runtime_error("WIC unavailable");
    ComPtr<IWICBitmap> bitmap;
    std::vector<std::uint8_t> backing = initial;
    if (backing.empty()) backing.resize(static_cast<std::size_t>(width) * height * 4, 0);
    if (FAILED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
        static_cast<UINT>(backing.size()), backing.data(), &bitmap))) throw std::runtime_error("Could not create annotation bitmap");

    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) throw std::runtime_error("Direct2D unavailable");
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target))) throw std::runtime_error("Could not create annotation target");
    ComPtr<IDWriteFactory> writeFactory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &writeFactory))) throw std::runtime_error("DirectWrite unavailable");

    target->BeginDraw();
    if (transparent) target->Clear(D2D1::ColorF(0, 0.0f));
    RenderAnnotations(target.Get(), factory.Get(), writeFactory.Get(), annotations);
    if (FAILED(target->EndDraw())) throw std::runtime_error("Could not render annotations");

    WICRect area{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&area, WICBitmapLockRead, &lock))) throw std::runtime_error("Could not read annotation bitmap");
    UINT bufferSize{};
    BYTE* data{};
    UINT stride{};
    lock->GetDataPointer(&bufferSize, &data);
    lock->GetStride(&stride);
    ImageBgra8 result{width, height};
    result.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) memcpy(result.pixels.data() + static_cast<std::size_t>(y) * width * 4, data + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(width) * 4);
    return result;
}

float SrgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void AnnotationRenderer::renderSdr(ImageBgra8& image, const AnnotationDocument& annotations) {
    if (image.width == 0 || image.height == 0 || annotations.items().empty()) return;
    image = RenderToBitmap(image.width, image.height, image.pixels, annotations, false);
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
}

void AnnotationRenderer::renderHdr(ImageF16& image, const AnnotationDocument& annotations) {
    if (image.width == 0 || image.height == 0 || annotations.items().empty()) return;
    const auto overlay = RenderToBitmap(image.width, image.height, {}, annotations, true);
    const float whiteScale = std::clamp(image.sdrWhiteLevelNits / 80.0f, 1.0f, 4.0f);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(image.width) * image.height; ++pixel) {
        const auto offset = pixel * 4;
        const float alpha = overlay.pixels[offset + 3] / 255.0f;
        if (alpha <= 0.0f) continue;
        const float divisor = std::max(alpha * 255.0f, 1.0f);
        const float blue = SrgbToLinear(overlay.pixels[offset] / divisor) * whiteScale;
        const float green = SrgbToLinear(overlay.pixels[offset + 1] / divisor) * whiteScale;
        const float red = SrgbToLinear(overlay.pixels[offset + 2] / divisor) * whiteScale;
        const float oldRed = ColorPipeline::halfToFloat(image.rgba[offset]);
        const float oldGreen = ColorPipeline::halfToFloat(image.rgba[offset + 1]);
        const float oldBlue = ColorPipeline::halfToFloat(image.rgba[offset + 2]);
        image.rgba[offset] = ColorPipeline::floatToHalf(oldRed * (1.0f - alpha) + red * alpha);
        image.rgba[offset + 1] = ColorPipeline::floatToHalf(oldGreen * (1.0f - alpha) + green * alpha);
        image.rgba[offset + 2] = ColorPipeline::floatToHalf(oldBlue * (1.0f - alpha) + blue * alpha);
        image.rgba[offset + 3] = ColorPipeline::floatToHalf(1.0f);
    }
}

} // namespace hdrsnapshot
