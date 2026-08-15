#pragma once

#include <LumaShot/Types.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>
#include <chrono>

namespace lumashot {

class CaptureService {
public:
    CaptureService();
    [[nodiscard]] static bool IsSupported() noexcept;
    [[nodiscard]] CaptureFrameSet captureDesktop(bool includeCursor, std::chrono::milliseconds timeout = std::chrono::milliseconds(1800));
    [[nodiscard]] MonitorFrame captureWindow(HWND window, bool includeCursor, std::chrono::milliseconds timeout = std::chrono::milliseconds(1800));

private:
    using CaptureItem = winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
    using Direct3DDevice = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Direct3DDevice winrtDevice_{nullptr};

    [[nodiscard]] CaptureItem itemForMonitor(HMONITOR monitor) const;
    [[nodiscard]] CaptureItem itemForWindow(HWND window) const;
    [[nodiscard]] MonitorFrame captureItem(const CaptureItem& item, HMONITOR monitor, RectI desktopRect, bool includeCursor, std::chrono::milliseconds timeout);
};

} // namespace lumashot
