#include "CaptureService.h"
#include <HDRSnapshot/Geometry.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <mutex>
#include <condition_variable>
#include <future>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace winrt;
namespace Wgc = winrt::Windows::Graphics::Capture;
namespace Wdx = winrt::Windows::Graphics::DirectX;

namespace hdrsnapshot {
namespace {

struct MonitorDescriptor {
    HMONITOR handle{};
    RectI rect{};
};

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT rect, LPARAM data) {
    auto& result = *reinterpret_cast<std::vector<MonitorDescriptor>*>(data);
    result.push_back({monitor, FromWin32Rect(*rect)});
    return TRUE;
}

struct HdrDescriptor {
    bool enabled{};
    float maximum{80.0f};
    float white{80.0f};
};

void QueryDisplaySettings(HMONITOR monitor, HdrDescriptor& result) noexcept {
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;

    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 pathCount{}, modeCount{};
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        if (status != ERROR_SUCCESS) return;
        paths.resize(pathCount);

        for (const auto& path : paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                _wcsicmp(source.viewGdiDeviceName, monitorInfo.szDevice) != 0) continue;

            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            color.header.size = sizeof(color);
            color.header.adapterId = path.targetInfo.adapterId;
            color.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) result.enabled = color.advancedColorEnabled != 0;

            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            white.header.size = sizeof(white);
            white.header.adapterId = path.targetInfo.adapterId;
            white.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0)
                result.white = static_cast<float>(white.SDRWhiteLevel) * 80.0f / 1000.0f;
            return;
        }
        return;
    }
}

HdrDescriptor QueryHdr(HMONITOR target) {
    HdrDescriptor result;
    QueryDisplaySettings(target, result);
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || desc.Monitor != target) continue;
            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output.As(&output6))) {
                DXGI_OUTPUT_DESC1 desc1{};
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    result.enabled = result.enabled || desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                     desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                    if (desc1.MaxLuminance > 0.0f) result.maximum = desc1.MaxLuminance;
                }
            }
            return result;
        }
    }
    return result;
}

RectI WindowRect(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(window, &rect);
    return FromWin32Rect(rect);
}

} // namespace

CaptureService::CaptureService() {
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device_, &level, &context_);
#ifdef _DEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &device_, &level, &context_);
    }
#endif
    if (FAILED(hr)) {
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &level, &context_));
    }
    ComPtr<IDXGIDevice> dxgi;
    winrt::check_hresult(device_.As(&dxgi));
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), reinterpret_cast<IInspectable**>(winrt::put_abi(winrtDevice_))));
}

bool CaptureService::IsSupported() noexcept {
    try { return Wgc::GraphicsCaptureSession::IsSupported(); } catch (...) { return false; }
}

CaptureService::CaptureItem CaptureService::itemForMonitor(HMONITOR monitor) const {
    auto factory = get_activation_factory<CaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    CaptureItem result{nullptr};
    check_hresult(interop->CreateForMonitor(monitor, guid_of<CaptureItem>(), put_abi(result)));
    return result;
}

CaptureService::CaptureItem CaptureService::itemForWindow(HWND window) const {
    auto factory = get_activation_factory<CaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    CaptureItem result{nullptr};
    check_hresult(interop->CreateForWindow(window, guid_of<CaptureItem>(), put_abi(result)));
    return result;
}

CaptureFrameSet CaptureService::captureDesktop(bool includeCursor, std::chrono::milliseconds timeout) {
    if (!IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable");
    std::vector<MonitorDescriptor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) throw std::runtime_error("No display was found");

    CaptureFrameSet result;
    result.virtualDesktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                             GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                             GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    result.monitors.reserve(monitors.size());
    if (monitors.size() == 1) {
        const auto& monitor = monitors.front();
        result.monitors.push_back(captureItem(itemForMonitor(monitor.handle), monitor.handle, monitor.rect, includeCursor, timeout));
        return result;
    }
    std::vector<std::future<MonitorFrame>> pending;
    pending.reserve(monitors.size());
    for (const auto monitor : monitors) {
        pending.push_back(std::async(std::launch::async, [monitor, includeCursor, timeout] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct ApartmentCleanup { ~ApartmentCleanup() { winrt::uninit_apartment(); } } cleanup;
            CaptureService service;
            return service.captureItem(service.itemForMonitor(monitor.handle), monitor.handle, monitor.rect, includeCursor, timeout);
        }));
    }
    for (auto& capture : pending) result.monitors.push_back(capture.get());
    return result;
}

MonitorFrame CaptureService::captureWindow(HWND window, bool includeCursor, std::chrono::milliseconds timeout) {
    if (!IsWindow(window)) throw std::invalid_argument("The target window is no longer available");
    const auto rect = WindowRect(window);
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    return captureItem(itemForWindow(window), monitor, rect, includeCursor, timeout);
}

MonitorFrame CaptureService::captureItem(const CaptureItem& item, HMONITOR monitor, RectI desktopRect,
                                         bool includeCursor, std::chrono::milliseconds timeout) {
    const auto size = item.Size();
    auto pool = Wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(winrtDevice_, Wdx::DirectXPixelFormat::R16G16B16A16Float, 2, size);
    auto session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(includeCursor);

    std::mutex mutex;
    std::condition_variable arrived;
    bool completed = false;
    std::exception_ptr error;
    MonitorFrame result;

    const auto token = pool.FrameArrived([&](const Wgc::Direct3D11CaptureFramePool& sender, auto&&) {
        std::scoped_lock lock(mutex);
        if (completed) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) return;
            const auto content = frame.ContentSize();
            if (content.Width <= 0 || content.Height <= 0) return;
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> source;
            check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));
            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);
            desc.Width = static_cast<UINT>(content.Width);
            desc.Height = static_cast<UINT>(content.Height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            check_hresult(device_->CreateTexture2D(&desc, nullptr, &staging));
            context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), 0, nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
            result.width = desc.Width;
            result.height = desc.Height;
            result.rgba16f.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
            const std::size_t rowBytes = static_cast<std::size_t>(desc.Width) * 8;
            for (UINT y = 0; y < desc.Height; ++y) {
                memcpy(reinterpret_cast<std::byte*>(result.rgba16f.data()) + y * rowBytes,
                       static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
            }
            context_->Unmap(staging.Get(), 0);
            result.monitor = monitor;
            // The capture texture can differ from the desktop coordinate span
            // under mixed DPI or driver scaling. Keep the authoritative desktop
            // bounds and let composition map texture pixels into that space.
            result.desktopRect = desktopRect;
            result.systemRelativeTime = frame.SystemRelativeTime().count();
            const auto hdr = QueryHdr(monitor);
            result.hdrEnabled = hdr.enabled;
            result.maxLuminanceNits = hdr.maximum;
            result.sdrWhiteLevelNits = hdr.white;
            completed = true;
        } catch (...) {
            error = std::current_exception();
            completed = true;
        }
        arrived.notify_one();
    });

    session.StartCapture();
    {
        std::unique_lock lock(mutex);
        if (!arrived.wait_for(lock, timeout, [&] { return completed; })) {
            completed = true;
            error = std::make_exception_ptr(std::runtime_error("Capture timed out"));
        }
    }
    pool.FrameArrived(token);
    session.Close();
    pool.Close();
    if (error) std::rethrow_exception(error);
    return result;
}

} // namespace hdrsnapshot
