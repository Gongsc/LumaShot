#include "ColorPipeline.h"
#include <LumaShot/Geometry.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d2d1effects_2.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace lumashot {
namespace {

HdrCalibration EffectiveCalibration(const ImageF16& source, HdrCalibration calibration) noexcept {
    if (!source.hdr) return {100, 0};
    calibration.outputBrightnessPercent = std::clamp(calibration.outputBrightnessPercent,
        HdrCalibration::MinimumOutputBrightness, HdrCalibration::MaximumOutputBrightness);
    calibration.highlightCompressionPercent = std::clamp(calibration.highlightCompressionPercent,
        HdrCalibration::MinimumHighlightCompression, HdrCalibration::MaximumHighlightCompression);
    return calibration;
}

float SdrWhiteScale(bool hdr, float sdrWhiteLevelNits) noexcept {
    if (!hdr || !std::isfinite(sdrWhiteLevelNits)) return 1.0f;
    return D2D1_SCENE_REFERRED_SDR_WHITE_LEVEL /
           std::max(D2D1_SCENE_REFERRED_SDR_WHITE_LEVEL, sdrWhiteLevelNits);
}

bool TryDirect2DEncode(const ImageF16& source, float linearScale, ImageBgra8& result) noexcept {
    try {
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        ComPtr<ID3D11Device> d3d;
        ComPtr<ID3D11DeviceContext> d3dContext;
        D3D_FEATURE_LEVEL selected{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, &selected, &d3dContext);
        if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, &selected, &d3dContext);
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC sourceDesc{};
        sourceDesc.Width = source.width; sourceDesc.Height = source.height; sourceDesc.MipLevels = 1; sourceDesc.ArraySize = 1;
        sourceDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sourceDesc.SampleDesc.Count = 1;
        sourceDesc.Usage = D3D11_USAGE_DEFAULT; sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA sourceData{source.rgba.data(), source.width * 8, 0};
        ComPtr<ID3D11Texture2D> sourceTexture;
        if (FAILED(d3d->CreateTexture2D(&sourceDesc, &sourceData, &sourceTexture))) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(d3d.As(&dxgiDevice))) return false;
        D2D1_FACTORY_OPTIONS factoryOptions{};
        ComPtr<ID2D1Factory1> factory;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &factoryOptions,
                                     reinterpret_cast<void**>(factory.GetAddressOf())))) return false;
        ComPtr<ID2D1Device> d2dDevice;
        if (FAILED(factory->CreateDevice(dxgiDevice.Get(), &d2dDevice))) return false;
        ComPtr<ID2D1DeviceContext> context;
        if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &context))) return false;
        ComPtr<ID2D1ColorContext> scRgb;
        ComPtr<ID2D1ColorContext> sRgb;
        if (FAILED(context->CreateColorContext(D2D1_COLOR_SPACE_SCRGB, nullptr, 0, &scRgb)) ||
            FAILED(context->CreateColorContext(D2D1_COLOR_SPACE_SRGB, nullptr, 0, &sRgb))) return false;

        ComPtr<IDXGISurface> sourceSurface;
        if (FAILED(sourceTexture.As(&sourceSurface))) return false;
        D2D1_BITMAP_PROPERTIES1 sourceProperties{};
        sourceProperties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_IGNORE);
        sourceProperties.dpiX = sourceProperties.dpiY = 96.0f; sourceProperties.colorContext = scRgb.Get();
        ComPtr<ID2D1Bitmap1> sourceBitmap;
        if (FAILED(context->CreateBitmapFromDxgiSurface(sourceSurface.Get(), &sourceProperties, &sourceBitmap))) return false;

        D3D11_TEXTURE2D_DESC outputDesc{};
        outputDesc.Width = source.width; outputDesc.Height = source.height; outputDesc.MipLevels = 1; outputDesc.ArraySize = 1;
        outputDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; outputDesc.SampleDesc.Count = 1;
        outputDesc.Usage = D3D11_USAGE_DEFAULT; outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> outputTexture;
        if (FAILED(d3d->CreateTexture2D(&outputDesc, nullptr, &outputTexture))) return false;
        ComPtr<IDXGISurface> outputSurface;
        if (FAILED(outputTexture.As(&outputSurface))) return false;
        D2D1_BITMAP_PROPERTIES1 targetProperties{};
        targetProperties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        targetProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        // The final color-management effect already emits gamma-encoded sRGB.
        // Attaching an sRGB context to this UNORM target can make D2D apply a
        // second transfer function, producing the characteristic washed-out
        // result on HDR captures.
        targetProperties.dpiX = targetProperties.dpiY = 96.0f; targetProperties.colorContext = nullptr;
        ComPtr<ID2D1Bitmap1> targetBitmap;
        if (FAILED(context->CreateBitmapFromDxgiSurface(outputSurface.Get(), &targetProperties, &targetBitmap))) return false;

        // WGC captures the HDR desktop after DWM has boosted SDR surfaces to
        // the user's SDR white level. Undo that boost before encoding a normal
        // sRGB file. This preserves SDR midtones instead of feeding them into a
        // global HDR curve merely because the monitor has HDR enabled.
        ComPtr<ID2D1Effect> whiteNormalization;
        if (FAILED(context->CreateEffect(CLSID_D2D1ColorMatrix, &whiteNormalization))) return false;
        whiteNormalization->SetInput(0, sourceBitmap.Get());
        const D2D1_MATRIX_5X4_F matrix = D2D1::Matrix5x4F(
            linearScale, 0, 0, 0,
            0, linearScale, 0, 0,
            0, 0, linearScale, 0,
            0, 0, 0, 1,
            0, 0, 0, 0);
        if (FAILED(whiteNormalization->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix))) return false;

        ComPtr<ID2D1Effect> colorManagement;
        if (FAILED(context->CreateEffect(CLSID_D2D1ColorManagement, &colorManagement))) return false;
        colorManagement->SetInputEffect(0, whiteNormalization.Get());
        if (FAILED(colorManagement->SetValue(D2D1_COLORMANAGEMENT_PROP_SOURCE_COLOR_CONTEXT, scRgb.Get())) ||
            FAILED(colorManagement->SetValue(D2D1_COLORMANAGEMENT_PROP_DESTINATION_COLOR_CONTEXT, sRgb.Get())) ||
            FAILED(colorManagement->SetValue(D2D1_COLORMANAGEMENT_PROP_QUALITY, D2D1_COLORMANAGEMENT_QUALITY_BEST))) return false;

        context->SetTarget(targetBitmap.Get());
        context->BeginDraw();
        context->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        context->DrawImage(colorManagement.Get());
        if (FAILED(context->EndDraw())) return false;

        outputDesc.Usage = D3D11_USAGE_STAGING; outputDesc.BindFlags = 0; outputDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(d3d->CreateTexture2D(&outputDesc, nullptr, &staging))) return false;
        d3dContext->CopyResource(staging.Get(), outputTexture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        result = {source.width, source.height};
        result.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4);
        const std::size_t rowBytes = static_cast<std::size_t>(source.width) * 4;
        for (UINT y = 0; y < source.height; ++y) memcpy(result.pixels.data() + static_cast<std::size_t>(y) * rowBytes,
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch, rowBytes);
        d3dContext->Unmap(staging.Get(), 0);
        for (std::size_t index = 3; index < result.pixels.size(); index += 4) result.pixels[index] = 255;
        return true;
    } catch (...) { return false; }
}

} // namespace

float ColorPipeline::halfToFloat(std::uint16_t value) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
    std::uint32_t exponent = (value >> 10) & 0x1fu;
    std::uint32_t mantissa = value & 0x03ffu;
    std::uint32_t bits{};
    if (exponent == 0) {
        if (mantissa == 0) bits = sign;
        else {
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) { mantissa <<= 1; --exponent; }
            mantissa &= 0x03ffu;
            bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) bits = sign | 0x7f800000u | (mantissa << 13);
    else bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    return std::bit_cast<float>(bits);
}

std::uint16_t ColorPipeline::floatToHalf(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | ((mantissa + 0x1000u) >> 13));
}

ImageF16 ColorPipeline::compose(const CaptureFrameSet& frames, RectI selection) {
    selection = ClampRect(NormalizeRect(selection), frames.virtualDesktop);
    if (selection.empty()) return {};
    ImageF16 result;
    result.width = static_cast<UINT>(selection.width());
    result.height = static_cast<UINT>(selection.height());
    result.rgba.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);
    for (std::size_t i = 3; i < result.rgba.size(); i += 4) result.rgba[i] = floatToHalf(1.0f);

    for (const auto& frame : frames.monitors) {
        const RectI overlap = IntersectRectangles(selection, frame.desktopRect);
        if (overlap.empty()) continue;
        result.hdr = result.hdr || frame.hdrEnabled;
        result.maxLuminanceNits = std::max(result.maxLuminanceNits, frame.maxLuminanceNits);
        result.sdrWhiteLevelNits = std::max(result.sdrWhiteLevelNits, frame.sdrWhiteLevelNits);
        const int targetX = overlap.left - selection.left;
        const int targetY = overlap.top - selection.top;
        const int desktopWidth = frame.desktopRect.width();
        const int desktopHeight = frame.desktopRect.height();
        if (desktopWidth <= 0 || desktopHeight <= 0 || frame.width == 0 || frame.height == 0) continue;
        result.toneRegions.push_back({{targetX, targetY, targetX + overlap.width(), targetY + overlap.height()},
                                      frame.hdrEnabled, frame.sdrWhiteLevelNits});
        if (frame.width == static_cast<UINT>(desktopWidth) && frame.height == static_cast<UINT>(desktopHeight)) {
            const int sourceX = overlap.left - frame.desktopRect.left;
            const int sourceY = overlap.top - frame.desktopRect.top;
            for (int row = 0; row < overlap.height(); ++row) {
                const auto sourceOffset = (static_cast<std::size_t>(sourceY + row) * frame.width + sourceX) * 4;
                const auto targetOffset = (static_cast<std::size_t>(targetY + row) * result.width + targetX) * 4;
                memcpy(result.rgba.data() + targetOffset, frame.rgba16f.data() + sourceOffset,
                       static_cast<std::size_t>(overlap.width()) * 8);
            }
            continue;
        }
        for (int row = 0; row < overlap.height(); ++row) {
            const int desktopY = overlap.top + row - frame.desktopRect.top;
            const UINT sourceY = std::min(frame.height - 1, static_cast<UINT>(static_cast<std::int64_t>(desktopY) * frame.height / desktopHeight));
            for (int column = 0; column < overlap.width(); ++column) {
                const int desktopX = overlap.left + column - frame.desktopRect.left;
                const UINT sourceX = std::min(frame.width - 1, static_cast<UINT>(static_cast<std::int64_t>(desktopX) * frame.width / desktopWidth));
                const auto sourceOffset = (static_cast<std::size_t>(sourceY) * frame.width + sourceX) * 4;
                const auto targetOffset = (static_cast<std::size_t>(targetY + row) * result.width + targetX + column) * 4;
                memcpy(result.rgba.data() + targetOffset, frame.rgba16f.data() + sourceOffset, 8);
            }
        }
    }
    return result;
}

ImageF16 ColorPipeline::thumbnail(const ImageF16& source, UINT maximumWidth, UINT maximumHeight) {
    if (source.width == 0 || source.height == 0 || maximumWidth == 0 || maximumHeight == 0) return {};
    const double scale = std::min({1.0, static_cast<double>(maximumWidth) / source.width,
                                  static_cast<double>(maximumHeight) / source.height});
    const UINT width = std::max(1u, static_cast<UINT>(std::lround(source.width * scale)));
    const UINT height = std::max(1u, static_cast<UINT>(std::lround(source.height * scale)));
    ImageF16 result;
    result.width = width; result.height = height; result.hdr = source.hdr;
    result.maxLuminanceNits = source.maxLuminanceNits;
    result.sdrWhiteLevelNits = source.sdrWhiteLevelNits;
    const RectI sourceBounds{0, 0, static_cast<int>(source.width), static_cast<int>(source.height)};
    const RectI targetBounds{0, 0, static_cast<int>(width), static_cast<int>(height)};
    for (const auto& region : source.toneRegions) {
        const RectI mapped = ClampRect(MapRectBetweenRects(region.pixels, sourceBounds, targetBounds), targetBounds);
        if (!mapped.empty()) result.toneRegions.push_back({mapped, region.hdr, region.sdrWhiteLevelNits});
    }
    result.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        const UINT sourceY = std::min(source.height - 1,
            static_cast<UINT>(static_cast<std::uint64_t>(y) * source.height / height));
        for (UINT x = 0; x < width; ++x) {
            const UINT sourceX = std::min(source.width - 1,
                static_cast<UINT>(static_cast<std::uint64_t>(x) * source.width / width));
            const auto input = (static_cast<std::size_t>(sourceY) * source.width + sourceX) * 4;
            const auto output = (static_cast<std::size_t>(y) * width + x) * 4;
            memcpy(result.rgba.data() + output, source.rgba.data() + input, 8);
        }
    }
    return result;
}

ImageBgra8 ColorPipeline::toneMapToSdr(const ImageF16& source, HdrCalibration calibration) {
    calibration = EffectiveCalibration(source, calibration);
    bool uniformTone = true;
    bool uniformHdr = source.hdr;
    float uniformWhite = source.sdrWhiteLevelNits;
    if (!source.toneRegions.empty()) {
        uniformHdr = source.toneRegions.front().hdr;
        uniformWhite = source.toneRegions.front().sdrWhiteLevelNits;
        for (const auto& region : source.toneRegions) {
            if (region.hdr != uniformHdr || std::abs(region.sdrWhiteLevelNits - uniformWhite) > 0.01f) {
                uniformTone = false;
                break;
            }
        }
    }
    const float hdrOutputScale = static_cast<float>(calibration.outputBrightnessPercent) / 100.0f;
    const float hdrCompression = static_cast<float>(calibration.highlightCompressionPercent) / 100.0f;
    const float uniformOutputScale = uniformHdr ? hdrOutputScale : 1.0f;
    const float uniformCompression = uniformHdr ? hdrCompression : 0.0f;
    ImageBgra8 direct2d;
    if (uniformTone && uniformCompression == 0.0f && source.width > 0 && source.height > 0 &&
        TryDirect2DEncode(source, SdrWhiteScale(uniformHdr, uniformWhite) * uniformOutputScale, direct2d)) return direct2d;
    ImageBgra8 result{source.width, source.height};
    result.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4);
    auto encode = [](float linear) {
        linear = std::clamp(linear, 0.0f, 1.0f);
        const float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
    };
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(source.width) * source.height; ++pixel) {
        const auto input = pixel * 4;
        const auto output = pixel * 4;
        bool pixelHdr = uniformHdr;
        float pixelWhite = uniformWhite;
        if (!uniformTone) {
            const int x = static_cast<int>(pixel % source.width);
            const int y = static_cast<int>(pixel / source.width);
            pixelHdr = source.hdr;
            pixelWhite = source.sdrWhiteLevelNits;
            for (auto region = source.toneRegions.rbegin(); region != source.toneRegions.rend(); ++region) {
                if (ContainsPoint(region->pixels, x, y)) {
                    pixelHdr = region->hdr;
                    pixelWhite = region->sdrWhiteLevelNits;
                    break;
                }
            }
        }
        const float whiteScale = SdrWhiteScale(pixelHdr, pixelWhite);
        const float outputScale = pixelHdr ? hdrOutputScale : 1.0f;
        const float compression = pixelHdr ? hdrCompression : 0.0f;
        float red = std::max(0.0f, halfToFloat(source.rgba[input])) * whiteScale;
        float green = std::max(0.0f, halfToFloat(source.rgba[input + 1])) * whiteScale;
        float blue = std::max(0.0f, halfToFloat(source.rgba[input + 2])) * whiteScale;
        const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
        const float knee = 1.0f - 0.5f * compression;
        if (compression > 0.0f && std::isfinite(luminance) && luminance > knee) {
            const float shoulderRange = 1.0f - knee;
            const float excess = luminance - knee;
            const float mappedLuminance = knee + shoulderRange * excess / (excess + shoulderRange);
            const float shoulderScale = mappedLuminance / luminance;
            red *= shoulderScale;
            green *= shoulderScale;
            blue *= shoulderScale;
        }
        result.pixels[output] = encode(blue * outputScale);
        result.pixels[output + 1] = encode(green * outputScale);
        result.pixels[output + 2] = encode(red * outputScale);
        result.pixels[output + 3] = 255;
    }
    return result;
}

} // namespace lumashot
